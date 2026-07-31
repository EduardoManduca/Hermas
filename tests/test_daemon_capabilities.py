#!/usr/bin/env python3
"""Contract checks for hermas-daemon-capabilities-v1."""

import json
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"test_daemon_capabilities: {message}")


def main() -> None:
    if len(sys.argv) != 2:
        fail("expected hermasd executable")
    result = subprocess.run(
        [sys.argv[1], "--capabilities"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0 or result.stderr:
        fail("capability query did not exit cleanly")
    try:
        document = json.loads(result.stdout)
    except (UnicodeDecodeError, json.JSONDecodeError):
        fail("capability output is not one UTF-8 JSON document")
    if document.get("format") != "hermas-daemon-capabilities-v1":
        fail("unexpected capability format")
    if not isinstance(document.get("hermas_version"), str):
        fail("missing Hermas version")
    if document.get("graph_image_version") != 1:
        fail("unexpected graph-image version")
    if document.get("protocol_version") != 1:
        fail("unexpected protocol version")
    if document.get("limits") != {
        "actions": 80,
        "active_executions": 16,
    }:
        fail("daemon limits differ")
    if document.get("flows") != {
        "action": True,
        "match": True,
        "within": True,
        "saga": True,
        "all": False,
        "each": False,
    }:
        fail("advertised flow capabilities differ")


if __name__ == "__main__":
    main()
