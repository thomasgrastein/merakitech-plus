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

## Supported versions

Only the [latest release](https://github.com/thomasgrastein/merakitech-plus/releases/latest) receives
fixes.
