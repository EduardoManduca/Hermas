# Security policy

## Supported versions

Before `1.0`, only the newest tagged alpha receives security fixes. Hermas is
experimental and initially supports Linux x86-64.

## Reporting a vulnerability

Do not file a public issue. Use GitHub's private vulnerability reporting for
this repository. If that channel is unavailable, contact a maintainer
privately and ask for a secure reporting channel without including exploit
details in the first message.

Include affected versions, the violated guarantee, reproduction steps, and
whether untrusted bytes, local filesystem access, or app behavior is needed.
Expect acknowledgement within seven days. We will coordinate validation,
remediation, disclosure timing, and credit with the reporter.

The [threat model](docs/THREAT_MODEL.md) defines supported trust boundaries
and explicitly unsupported distributed guarantees.
