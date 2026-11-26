"""ota_fixture.py — a local stand-in for GitHub releases over real TLS (OTA phase 3 tests and the simulator).

Used by tests/test_ota_fetch.py and sim/fixtures/ota/upd_sim.py. Everything is generated per run in a temp dir and is
never trusted by a shipped build:

  * a TEST CA (RSA) and server certificates from it (OpenSSL CLI, `openssl ca` for fixed validity dates):
      good     github.com + release-assets / objects.githubusercontent.com, ECDSA P-256, 2026-01-01 .. 2036-01-01
      good_rsa the same names, RSA 2048 (the asset host serves this one: both TLS key types are exercised)
      expired  2020 .. 2021        future   2035 .. 2040        wrongname  example.com only
      untrusted  the right names, from ANOTHER CA the helper does not know
  * a TEST signing key (tests/sigtest.py) and signed test releases built with tools/sign_release.py build_manifest;
  * an HTTPS server (Python ssl, TLS 1.2+) that answers for every pinned host on one port, picks the certificate by
    SNI, and serves GitHub's redirect chain:
      github.com/<repo>/releases/latest/download/<f>  -302->  github.com/<repo>/releases/download/v<ver>/<f>
      github.com/<repo>/releases/download/<tag>/<f>   -302->  release-assets.githubusercontent.com/<tag>/<f>?<signed query>
    The signed query has the shape and length of GitHub's real one (sp/sv/se/rscd/.../sig/jwt, ~900-byte URLs, see
    gh_query) unless a test pads it to an exact URL length (cfg url_len).
    Behaviour switches (Server.cfg) inject failures: status codes, tampering, truncation, a stalled transfer, chunked
    bodies, bad redirects.
The helper's TEST build (build/merakiplus_upd_test) is pointed at it with MERAKIPLUS_UPD_TEST_CONNECT / _CA / _KEY.
"""
import hashlib
import http.server
import json
import os
import pathlib
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import sigtest as st        # noqa: E402
from sigtest import mp, sr  # noqa: E402

REPO = mp.REPO
GH, ASSETS = "github.com", "release-assets.githubusercontent.com"
NAMES = (GH, ASSETS, "objects.githubusercontent.com")


def openssl():
    return sr.openssl()


def _run(*a, cwd=None):
    subprocess.run([openssl(), *a], check=True, capture_output=True, cwd=cwd)


class PKI:
    """Test CA + server certificates in directory d."""

    def __init__(self, d, reuse=False):
        """reuse=True: keep what an earlier process generated in d (the simulator's wrapper runs once per check/install)"""
        self.d = pathlib.Path(d)
        self.reuse = reuse and (self.d / "ready").is_file()
        self.d.mkdir(parents=True, exist_ok=True)
        self.ca = self._ca("ca", "Merakiplus TEST CA")
        self.other = self._ca("other", "Some Other CA")
        self.certs = {
            "good": self._leaf("good", self.ca, NAMES, "20260101000000Z", "20360101000000Z", "ec"),
            "good_rsa": self._leaf("good_rsa", self.ca, NAMES, "20260101000000Z", "20360101000000Z", "rsa"),
            "expired": self._leaf("expired", self.ca, NAMES, "20200101000000Z", "20210101000000Z", "ec"),
            "future": self._leaf("future", self.ca, NAMES, "20350101000000Z", "20400101000000Z", "ec"),
            "wrongname": self._leaf("wrongname", self.ca, ("example.com",), "20260101000000Z", "20360101000000Z", "ec"),
            "untrusted": self._leaf("untrusted", self.other, NAMES, "20260101000000Z", "20360101000000Z", "ec"),
        }
        (self.d / "ready").write_text("1\n")

    def _ca(self, name, cn):
        d = self.d / name
        if self.reuse:
            return d
        d.mkdir(exist_ok=True)
        (d / "index.txt").write_text("")
        (d / "serial").write_text("1000\n")
        key = d / "ca.key"
        crt = d / "ca.crt"
        _run("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(key))
        _run("req", "-x509", "-new", "-key", str(key), "-out", str(crt), "-days", "7300", "-subj", f"/O=Merakiplus Test/CN={cn}",
             "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign")
        (d / "ca.cnf").write_text(f"""[ ca ]
default_ca = test
[ test ]
dir = {d}
database = $dir/index.txt
serial = $dir/serial
new_certs_dir = $dir
certificate = $dir/ca.crt
private_key = $dir/ca.key
default_md = sha256
policy = any
unique_subject = no
copy_extensions = copy
[ any ]
commonName = supplied
""")
        return d

    def _leaf(self, name, ca, names, start, end, kind):
        key, csr, crt = self.d / f"{name}.key", self.d / f"{name}.csr", self.d / f"{name}.crt"
        if self.reuse:
            return self.d / f"{name}.chain.crt", key
        if kind == "ec":
            _run("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", str(key))
        else:
            _run("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", str(key))
        ext = self.d / f"{name}.ext"
        ext.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\n"
                       "extendedKeyUsage=serverAuth\nsubjectAltName=" + ",".join("DNS:" + n for n in names) + "\n")
        _run("req", "-new", "-key", str(key), "-out", str(csr), "-subj", f"/CN={names[0]}")
        _run("ca", "-batch", "-config", str(ca / "ca.cnf"), "-in", str(csr), "-out", str(crt), "-startdate", start,
             "-enddate", end, "-extfile", str(ext), "-notext")
        # serve the leaf + its CA (like a real server sends its chain; the helper must still anchor it itself)
        chain = self.d / f"{name}.chain.crt"
        chain.write_bytes(crt.read_bytes() + (ca / "ca.crt").read_bytes())
        return chain, key

    def ca_pem(self):
        return self.ca / "ca.crt"


# ------------------------------------------------------------------ releases
FILLER = {"merakiplus": 3000, "merakiplus_snd": 900, "merakiplus_upd": 1500, "merakiplus_boot": 700, "switcher": 600,
          "grab_shim.so": 300}


def make_release(d, key, version, seq, notes="Signed test release\nOn-device update test\n", launcher=b"LAUNCHER-1\n",
                 tamper=None, sign_key=None, released="2026-11-27"):
    """A small synthetic signed release in d (tiny fake binaries — the helper never runs them). Returns dict."""
    d = pathlib.Path(d)
    d.mkdir(parents=True, exist_ok=True)
    for n, size in FILLER.items():
        body = launcher if n == "merakiplus_boot" else (f"{n} v{version} ".encode() * (size // 20 + 1))[:size]
        (d / n).write_bytes(body)
    for n in st.AUDIO_ASSETS:
        (d / n).write_bytes(f"RIFF {n} {version}".encode())
    (d / "notes.txt").write_text(notes)
    files = sorted(FILLER) + st.AUDIO_ASSETS + ["notes.txt"]
    (d / "MD5SUMS").write_text("".join(f"{hashlib.md5((d / n).read_bytes()).hexdigest()}  {n}\n" for n in files))
    (d / "SHA256SUMS").write_text("".join(f"{hashlib.sha256((d / n).read_bytes()).hexdigest()}  {n}\n" for n in files))
    assets = files + ["MD5SUMS", "SHA256SUMS"]
    data = sr.build_manifest(d, version, "0" * 40, released, assets, seq)
    (d / "manifest.json").write_bytes(data)
    (d / "manifest.sig").write_text((sign_key or key).sign(data))
    if tamper:
        tamper(d)
    m = json.loads(data)
    ui = next(e for e in m["release"] if e["name"] == "demo_v9")
    return {"dir": d, "version": version, "seq": seq, "manifest": m, "name": f"{version}-{ui['sha256'][:8]}",
            "launcher": launcher}


# ------------------------------------------------------------------ GitHub's signed asset URLs
UA = "merakiplus-upd/2"          # src/merakiplus_upd.c UA (request_len() must match the helper's request byte for byte)


def gh_query(f, sig_len=None):
    """A query string shaped like GitHub's signed release-asset redirect (Azure SAS + a JWT): the real one makes
    900-920-byte URLs, and its sig= part (base64, '+' and '/' escaped as %2B / %2F) varies in length per download.
    sig_len pads sig= to exactly that many characters (for exact URL lengths)."""
    q = ("sp=r&sv=2018-11-09&sr=b&spr=https&se=2026-11-27T13%3A08%3A41Z&rscd=attachment%3B+filename%3D" + f +
         "&rsct=application%2Foctet-stream&skoid=96c2d410-5711-43a1-aedd-ab1947aa7ab0&sktid=398a6654-997b-47e9-b12b-9515b896b4de"
         "&skt=2026-11-27T12%3A08%3A10Z&ske=2026-11-27T13%3A08%3A41Z&sks=b&skv=2018-11-09&sig={sig}"
         "&jwt=eyJ0eXAiOiJKV1QiLCJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmVsZWFzZS1hc3NldHMuZ2l0aHVidXNlcmNvbnRlbnQuY29t"
         "Iiwia2V5Ijoia2V5MSIsImV4cCI6MTc2NDI0ODgyMSwibmJmIjoxNzY0MjQ4NTIxLCJwYXRoIjoicmVsZWFzZWFzc2V0cHJvZHVjdGlvbi5ibG9iLmNvcmUud2lu"
         "ZG93cy5uZXQiLCJzdWIiOiIyMDA5MjI0In0.kq0Yb0ZlTq8m1QqkV2xGJ4l9cX8m0Zl1d3VxcnM0NzE3Zk1hR2xXb1V4cE9tRjZ3TkZhd2lQU2Vjb25k"
         "&response-content-disposition=attachment%3B%20filename%3D" + f + "&response-content-type=application%2Foctet-stream")
    sig = "Xk3%2BeVv0b%2FQm7Lw1pR9sTnY4cZ8aH2jD6fG0kU5iO3E%3D"
    if sig_len is not None:
        sig = ("A" * sig_len)[:sig_len]
    return q.replace("{sig}", sig)


def asset_url(tag, f, sig_len=None):
    return f"https://{ASSETS}/{tag}/{f}?{gh_query(f, sig_len)}"


def request_len(url):
    """the byte length of the helper's GET for url (src/merakiplus_upd_net.c one())"""
    host, path = url[len("https://"):].split("/", 1)
    return len(f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: {UA}\r\nAccept: */*\r\nAccept-Encoding: identity\r\n"
               "Connection: close\r\n\r\n")


def padded_asset_url(tag, f, url_len):
    """an asset URL of exactly url_len bytes (sig= padded)"""
    base = len(asset_url(tag, f, 0))
    if url_len < base:
        raise ValueError(f"url_len {url_len} < {base}")
    return asset_url(tag, f, url_len - base)


# ------------------------------------------------------------------ the server
class Server:
    """HTTPS on 127.0.0.1:<port> for every pinned host. cfg keys (all optional):
         latest: version of releases/latest            releases: {version: release dict}
         status: {path-substring: code}                tamper: {asset name: bytes}
         truncate: {asset name: n bytes}               oversize: {asset name: extra bytes}
         chunked: bool                                 redirect: {path-substring: Location}
         cert: name of the PKI certificate for every host (default: good for github.com, good_rsa for assets)
         url_len: {asset name: n} the asset-host redirect URL is exactly n bytes (default: GitHub-like, ~900)
         stall: {asset name: n bytes} send the headers + n body bytes, then nothing (stall_s seconds), then hang up
       sizes: [(host+path, request bytes)] of every request, as received (header lines rebuilt byte for byte)
    """

    def __init__(self, pki, host="127.0.0.1"):
        self.pki, self.cfg, self.log, self.sizes = pki, {"releases": {}}, [], []
        self.lock = threading.Lock()
        self.ctxs = {}
        for name, (chain, key) in pki.certs.items():
            c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            c.minimum_version = ssl.TLSVersion.TLSv1_2
            c.load_cert_chain(str(chain), str(key))
            self.ctxs[name] = c
        base = self.ctxs["good"]
        base.sni_callback = self._sni
        srv = self

        class H(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                srv._handle(self)

        self.httpd = http.server.ThreadingHTTPServer((host, 0), H)
        self.httpd.daemon_threads = True
        self.httpd.handle_error = lambda *a: None          # a client that hangs up mid-test is expected
        self.httpd.socket = base.wrap_socket(self.httpd.socket, server_side=True)
        self.port = self.httpd.server_address[1]
        self.t = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.t.start()

    def _sni(self, sock, name, ctx):
        cert = self.cfg.get("cert") or ("good_rsa" if name != GH else "good")
        sock.context = self.ctxs[cert]
        return None

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()

    def requests(self, needle=""):
        with self.lock:
            return [r for r in self.log if needle in r]

    def _send(self, h, code, body=b"", headers=()):
        h.send_response(code)
        for k, v in headers:
            h.send_header(k, v)
        if code in (301, 302, 303, 307, 308):
            h.send_header("Content-Length", "0")
            h.end_headers()
            return
        tr, ov, nm = self.cfg.get("truncate") or {}, self.cfg.get("oversize") or {}, headers and dict(headers).get("X-Name")
        if self.cfg.get("chunked") and nm not in tr:
            h.send_header("Transfer-Encoding", "chunked")
            h.end_headers()
            for i in range(0, len(body), 700):
                part = body[i:i + 700]
                h.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            h.wfile.write(b"0\r\n\r\n")
            return
        if nm in ov:
            body = body + b"X" * ov[nm]
        sl = self.cfg.get("stall") or {}
        if nm in sl:
            h.send_header("Content-Length", str(len(body)))
            h.end_headers()
            h.wfile.write(body[:sl[nm]])
            h.wfile.flush()
            time.sleep(self.cfg.get("stall_s", 8))
            h.close_connection = True
            return
        h.send_header("Content-Length", str(len(body)))
        h.end_headers()
        if nm in tr:
            h.wfile.write(body[:tr[nm]])
            h.wfile.flush()
            h.close_connection = True
            try:
                h.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            return
        h.wfile.write(body)

    def _handle(self, h):
        host = (h.headers.get("Host") or "").split(":")[0]
        path = h.path
        size = len(h.raw_requestline) + sum(len(f"{k}: {v}\r\n") for k, v in h.headers.items()) + 2
        with self.lock:
            self.log.append(f"{host}{path}")
            self.sizes.append((f"{host}{path}", size))
        cfg = self.cfg
        for k, code in (cfg.get("status") or {}).items():
            if k in f"{host}{path}":
                return self._send(h, code, b"error page")
        for k, loc in (cfg.get("redirect") or {}).items():
            if k in f"{host}{path}":
                return self._send(h, 302, headers=[("Location", loc)])
        pre = f"/{REPO}/releases/"
        if host == GH and path.startswith(pre + "latest/download/"):
            f = path[len(pre + "latest/download/"):]
            if not cfg.get("latest"):
                return self._send(h, 404, b"Not Found")
            return self._send(h, 302, headers=[("Location", f"https://{GH}{pre}download/v{cfg['latest']}/{f}")])
        if host == GH and path.startswith(pre + "download/"):
            tag, f = path[len(pre + "download/"):].split("/", 1)
            ul = (cfg.get("url_len") or {}).get(f)
            return self._send(h, 302, headers=[("Location", padded_asset_url(tag, f, ul) if ul else asset_url(tag, f))])
        if host == ASSETS:
            tag, f = path.split("?")[0].lstrip("/").split("/", 1)
            rel = cfg["releases"].get(tag.lstrip("v"))
            p = rel and rel["dir"] / f
            if not p or not p.is_file():
                return self._send(h, 404, b"Not Found")
            body = (cfg.get("tamper") or {}).get(f, p.read_bytes())
            return self._send(h, 200, body, [("Content-Type", "application/octet-stream"), ("X-Name", f)])
        return self._send(h, 404, b"Not Found")


# ------------------------------------------------------------------ a display file tree for the helper
def device_root(d, current="6.25-aaaaaaaa", max_seq=3, launcher=b"LAUNCHER-1\n", previous="", pending="", extra=""):
    d = pathlib.Path(d)
    (d / "tmp").mkdir(parents=True, exist_ok=True)
    rel = d / "new_customer/merakiplus/rel"
    (d / "new_customer/merakiplus/ota").mkdir(parents=True, exist_ok=True)
    for n in (current, previous):
        if n:
            (rel / n / "audio").mkdir(parents=True, exist_ok=True)
            (rel / n / "demo_v9").write_bytes(b"old ui " + n.encode())
            (rel / n / "MANIFEST").write_text("merakiplus-release 1\n")
    (d / "new_customer/demo_v9").write_bytes(launcher)
    state = (f"format=1\ncurrent={current}\nprevious={previous}\npending={pending}\ntries=0\nlast=\n" +
             (f"max_seq={max_seq}\n" if max_seq is not None else "") + extra)
    (d / "new_customer/merakiplus/ota/state").write_text(state)
    return d


def helper_env(root, pki, port, key, connect_host="127.0.0.1", now=None, free=None, io_timeout=None):
    env = dict(os.environ)
    env.update({"MERAKIPLUS_UPD_ROOT": str(root), "MERAKIPLUS_UPD_TEST_CA": str(pki.ca_pem()),
                "MERAKIPLUS_UPD_TEST_CONNECT": f"{connect_host}:{port}",
                "MERAKIPLUS_UPD_TEST_KEY": f"{key.id}:{key.pk.hex()}"})
    for k, v in (("MERAKIPLUS_UPD_TEST_NOW", now), ("MERAKIPLUS_UPD_TEST_FREE", free), ("MERAKIPLUS_UPD_TEST_IO_TIMEOUT", io_timeout)):
        if v is not None:
            env[k] = str(v)
        else:
            env.pop(k, None)
    return env


def read_status(p):
    try:
        return dict(l.split("=", 1) for l in pathlib.Path(p).read_text().splitlines() if "=" in l)
    except FileNotFoundError:
        return {}


class SavedKey(st.TestKey):
    """a TEST key kept in a file across processes (the simulator's wrapper)"""

    def __init__(self, d, name):            # noqa: super().__init__ would generate a new key
        self.path = pathlib.Path(d) / f"{name}.pem"
        if not self.path.is_file():
            subprocess.run([sr.openssl(), "genpkey", "-algorithm", "ed25519", "-out", str(self.path)], check=True, capture_output=True)
        self.pk = sr.public_key(self.path)
        self.keynum = sr.key_id(self.pk)
        self.id = self.keynum.hex()


class Rig:
    """PKI + key + server + a release history, for one test run (or one simulator scenario)."""

    def __init__(self, work=None):
        self.work = pathlib.Path(work or tempfile.mkdtemp(prefix="merakiplus-ota-"))
        self.pki = PKI(self.work / "pki")
        self.key = st.TestKey(self.work, "ota-test")
        self.srv = Server(self.pki)

    def release(self, version, seq, **kw):
        r = make_release(self.work / f"rel-{version}-{seq}", self.key, version, seq, **kw)
        self.srv.cfg["releases"][version] = r
        return r

    def close(self):
        self.srv.close()
        shutil.rmtree(self.work, ignore_errors=True)
