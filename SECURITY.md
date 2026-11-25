# Security policy

## Reporting a vulnerability

Please **do not** open a public issue for security problems.

- Preferred: GitHub **private vulnerability reporting** — *Security → Report a vulnerability* on
  <https://github.com/thomasgrastein/merakitech-plus/security/advisories/new>.
- Alternative: e-mail **thomasgrastein@gmail.com** with the subject `Meraki+ security`.

Please include what is affected (file/version), how to reproduce it, and the impact. You will get an
answer as soon as possible (this is a spare-time project, so please allow a few days). Fixes are
released first and the details published afterwards (coordinated disclosure); credit is given unless
you prefer otherwise.

## Scope

In scope — our own code in this repository:
- the Meraki+ UI (`src/`), the gesture switcher and shim (`boot/`),
- the installer/revert tooling (`install.sh`, `revert.sh`, `tools/`),
- the simulator and tests (`sim/`, `tests/`), and the CI configuration.

Examples: the installer writing somewhere it should not, a way to make the UI send frames outside
the frame policy, unsafe file handling on the display, leaking Wi-Fi credentials into logs.

Out of scope — the manufacturer's firmware. The display module ships with an **unauthenticated root
telnet server** (port 23) and an **unsigned OTA update channel**. These are weaknesses of the vendor
firmware, which Meraki+ uses but did not create and cannot fix; please report them to the
manufacturer. Until fixed, isolate the machine on its own network/VLAN and block inbound access (see
the README section *Security and OTA*).

## Release signatures

From v6.23, releases carry `manifest.json` + `manifest.sig` (Ed25519, signed offline by the maintainer; CI cannot
sign). The public key is [`keys/merakiplus-release.pub`](keys/merakiplus-release.pub) (key id `26f673cde8584ba0`).
A release asset that does not match its signed manifest, or a signature by any other key, is not ours — please
report it. Key custody, rotation and the threat model: [docs/signing.md](docs/signing.md).

Releases are built by CI as drafts and published only after the maintainer has signed them; the installer refuses
unsigned files by default. A published release without `manifest.json` + `manifest.sig` (other than v6.22 and older),
or a `v*` tag that no published release points to, is not ours either. How releases are made and what CI is allowed to
do: [docs/release-process.md](docs/release-process.md).

## Supported versions

Only the [latest release](https://github.com/thomasgrastein/merakitech-plus/releases/latest) receives
fixes.
