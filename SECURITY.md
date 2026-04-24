# Security Policy

## Supported versions

AJAZZ Control Center is currently in early alpha. Only the tip of `main` is supported. Security fixes will be backported to the latest tagged release once stable releases exist.

## Reporting a vulnerability

Please **do not** open public GitHub issues for security vulnerabilities.

Instead, report privately using GitHub's [Private Vulnerability Reporting](https://docs.github.com/en/code-security/security-advisories/guidance-on-reporting-and-writing-information-about-vulnerabilities/privately-reporting-a-security-vulnerability) feature on this repository, or e-mail the maintainer listed in the repository metadata.

Include:

- A description of the vulnerability and its impact.
- Steps to reproduce (ideally a minimal proof-of-concept).
- The commit or release the issue was found against.
- Your preferred credit name for the advisory (or request to remain anonymous).

We aim to acknowledge reports within **72 hours** and to publish a fix or mitigation within **30 days** for high-severity issues.

## Scope

The following are in scope:

- Memory-safety issues in the C++ core or device modules.
- Sandbox escapes from the Python plugin host.
- Privilege escalation via udev/HID permissions shipped by the project.
- Supply-chain issues in build or packaging scripts.

Out of scope:

- Issues in upstream dependencies (report them upstream; we will track and update).
- Social-engineering attacks against maintainers.
- Attacks requiring physical access to already-unlocked hardware.
