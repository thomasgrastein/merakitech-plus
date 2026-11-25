#!/usr/bin/env python3
"""mqtt_broker.py — a tiny MQTT 3.1.1 mock broker for tests (stdlib only, no device contact).

It accepts clients on 127.0.0.1, answers CONNECT / PUBLISH (QoS 0 + 1) / PINGREQ / DISCONNECT and RECORDS every packet a client
sends, so tests can assert what the Meraki+ helper published — and that it never subscribed. It does not route messages to other
clients (there are none); retained messages are kept per topic. Test hooks: refuse logins (CONNACK return code), drop or refuse
connections (a broker outage), send an unsolicited PUBLISH to the client (the helper must disconnect).

    python3 sim/mqtt_broker.py --port 1883 [--log broker.jsonl]     # run it by hand and point mqtt.conf at this machine

Used by tests/test_mqtt.py and sim/run_e2e.py (scenarios with an "mqtt" block).
"""
import argparse
import json
import socket
import struct
import threading
import time

TYPES = {1: "CONNECT", 2: "CONNACK", 3: "PUBLISH", 4: "PUBACK", 5: "PUBREC", 6: "PUBREL", 7: "PUBCOMP", 8: "SUBSCRIBE",
         9: "SUBACK", 10: "UNSUBSCRIBE", 11: "UNSUBACK", 12: "PINGREQ", 13: "PINGRESP", 14: "DISCONNECT"}


def _str(b, i):
    n = struct.unpack_from(">H", b, i)[0]
    return b[i + 2:i + 2 + n], i + 2 + n


class MockBroker:
    def __init__(self, host="127.0.0.1", port=0, connack_rc=0, log_path=None):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        self.connack_rc = connack_rc
        self.packets = []            # every packet from any client, in order (dicts)
        self.retained = {}
        self.connections = 0
        self.disconnects = 0         # clean DISCONNECT packets
        self.closed = 0              # sockets that ended (any reason)
        self.accepting = True
        self.t0 = time.monotonic()
        self.lock = threading.Lock()
        self.clients = []
        self.log_path = log_path
        self._stop = False
        self.thread = threading.Thread(target=self._accept, daemon=True)
        self.thread.start()

    # ---------------------------------------------------------------- server
    def _accept(self):
        self.sock.settimeout(0.2)
        while not self._stop:
            try:
                c, _ = self.sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            if not self.accepting:       # an outage: the TCP connection is refused right away
                c.close()
                continue
            with self.lock:
                self.connections += 1
                self.clients.append(c)
            threading.Thread(target=self._client, args=(c,), daemon=True).start()

    def _record(self, d):
        d["t"] = round(time.monotonic() - self.t0, 3)
        with self.lock:
            self.packets.append(d)
            if d["type"] == "PUBLISH" and d.get("retain"):
                if d["payload"]:
                    self.retained[d["topic"]] = d["payload"]
                else:
                    self.retained.pop(d["topic"], None)
        if self.log_path:
            with open(self.log_path, "a") as f:
                f.write(json.dumps({k: (v.decode("utf-8", "replace") if isinstance(v, bytes) else v) for k, v in d.items()},
                                   ensure_ascii=False) + "\n")

    def _client(self, c):
        buf = b""
        c.settimeout(0.2)
        try:
            while not self._stop:
                try:
                    data = c.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    break
                buf += data
                while True:
                    pkt, buf = self._take(buf)
                    if pkt is None:
                        break
                    if not self._handle(c, *pkt):
                        return
        except OSError:
            pass
        finally:
            with self.lock:
                self.closed += 1
                if c in self.clients:
                    self.clients.remove(c)
            try:
                c.close()
            except OSError:
                pass

    @staticmethod
    def _take(buf):
        if len(buf) < 2:
            return None, buf
        mult, n, i = 1, 0, 1
        while True:
            if i >= len(buf):
                return None, buf
            b = buf[i]
            n += (b & 0x7F) * mult
            mult *= 128
            i += 1
            if not b & 0x80:
                break
        if len(buf) < i + n:
            return None, buf
        return (buf[0], buf[i:i + n]), buf[i + n:]

    def _handle(self, c, first, body):
        t = first >> 4
        name = TYPES.get(t, f"TYPE{t}")
        d = {"type": name, "flags": first & 0x0F, "len": len(body)}
        if t == 1:                                            # CONNECT
            proto, i = _str(body, 0)
            level, flags = body[i], body[i + 1]
            ka = struct.unpack_from(">H", body, i + 2)[0]
            i += 4
            cid, i = _str(body, i)
            d.update(proto=proto.decode(), level=level, clean=bool(flags & 2), keepalive=ka, client_id=cid.decode())
            if flags & 4:
                wt, i = _str(body, i)
                wm, i = _str(body, i)
                d.update(will_topic=wt.decode(), will_payload=wm.decode(), will_qos=(flags >> 3) & 3, will_retain=bool(flags & 0x20))
            if flags & 0x80:
                u, i = _str(body, i)
                d["user"] = u.decode()
            d["has_password"] = bool(flags & 0x40)
            if flags & 0x40:
                p, i = _str(body, i)
                d["password"] = p.decode()            # kept so a test can check it never appears ELSEWHERE
            self._record(d)
            c.sendall(bytes([0x20, 2, 0, self.connack_rc]))
            return self.connack_rc == 0
        if t == 3:                                            # PUBLISH
            qos = (first >> 1) & 3
            topic, i = _str(body, 0)
            pid = None
            if qos:
                pid = struct.unpack_from(">H", body, i)[0]
                i += 2
            d.update(topic=topic.decode(), qos=qos, retain=bool(first & 1), dup=bool(first & 8), pid=pid, payload=body[i:].decode())
            self._record(d)
            if qos == 1:
                c.sendall(bytes([0x40, 2]) + struct.pack(">H", pid))
            return True
        if t == 12:                                           # PINGREQ
            self._record(d)
            c.sendall(bytes([0xD0, 0]))
            return True
        if t == 14:                                           # DISCONNECT
            self._record(d)
            with self.lock:
                self.disconnects += 1
            return False
        self._record(d)                                       # anything else (SUBSCRIBE …) is recorded and answered with nothing
        return True

    # ---------------------------------------------------------------- test hooks
    def outage(self, on):
        """on=True: drop every client and refuse new connections; on=False: accept again."""
        self.accepting = not on
        if on:
            with self.lock:
                cl = list(self.clients)
            for c in cl:
                try:
                    c.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass

    def inject_publish(self, topic="merakiplus/cmd", payload=b"on"):
        """send a PUBLISH TO the client(s) — the helper must treat it as a protocol violation and disconnect"""
        t = topic.encode()
        body = struct.pack(">H", len(t)) + t + payload
        with self.lock:
            cl = list(self.clients)
        for c in cl:
            try:
                c.sendall(bytes([0x30, len(body)]) + body)
            except OSError:
                pass

    def pubs(self, topic=None):
        with self.lock:
            return [p for p in self.packets if p["type"] == "PUBLISH" and (topic is None or p["topic"] == topic)]

    def types(self):
        with self.lock:
            return [p["type"] for p in self.packets]

    def wait(self, cond, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if cond():
                return True
            time.sleep(0.05)
        return cond()

    def close(self):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass
        with self.lock:
            cl = list(self.clients)
        for c in cl:
            try:
                c.close()
            except OSError:
                pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--log", help="append every received packet as JSON lines")
    a = ap.parse_args()
    b = MockBroker(a.host, a.port, log_path=a.log)
    print(f"mock MQTT broker on {a.host}:{b.port} — Ctrl-C to stop", flush=True)
    seen = 0
    try:
        while True:
            time.sleep(0.5)
            ps = b.pubs()
            for p in ps[seen:]:
                pl = p["payload"]
                print(f"{p['t']:8.1f}  {p['topic']}{' (retained)' if p['retain'] else ''}  {pl[:120]}{'…' if len(pl) > 120 else ''}")
            seen = len(ps)
    except KeyboardInterrupt:
        b.close()


if __name__ == "__main__":
    main()
