#!/usr/bin/env python3
"""Contract and integration checks for hermas-history-v2."""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


FINGERPRINT = 0x123456789ABCDEF0


def record(
    sequence: int,
    kind: int,
    *,
    request: int = 0,
    node: int = 0,
    app: int = 0,
    action: int = 0,
) -> bytes:
    value = bytearray(64)
    value[0:4] = b"HJR1"
    struct.pack_into("<HHHH", value, 4, 2, 64, kind, 0)
    struct.pack_into("<QQI", value, 16, sequence, 41, 7)
    struct.pack_into("<QHHH", value, 36, request, node, app, action)
    struct.pack_into("<Q", value, 52, FINGERPRINT)
    struct.pack_into("<I", value, 60, zlib.crc32(value[:60]))
    return bytes(value)


def inspect(executable: str, path: Path) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        [executable, "--json", os.fspath(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def decoded_lines(result: subprocess.CompletedProcess[bytes]) -> list[dict]:
    assert result.returncode == 0, result.stderr.decode()
    return [json.loads(line) for line in result.stdout.decode().splitlines()]


def test_contract(executable: str) -> None:
    with tempfile.TemporaryDirectory(prefix="hermas-history-json-") as directory:
        path = Path(directory, "journal.hj")
        path.write_bytes(
            record(1, 1)
            + record(2, 2, request=9, node=2, app=3, action=4)
            + record(3, 2, request=10, node=3, app=5, action=6)
        )
        path.chmod(0o600)

        items = decoded_lines(inspect(executable, path))
        assert all(item["format"] == "hermas-history-v2" for item in items)
        assert [item["type"] for item in items] == [
            "record",
            "record",
            "record",
            "summary",
        ]
        assert items[0]["request_id"] is None
        assert items[1]["request_id"] == "9"
        summary = items[-1]
        assert summary["workspace"] is None
        assert summary["record_count"] == "3"
        assert summary["next_execution_id"] == "42"
        assert summary["interrupted"] == [
            {
                "execution_id": "41",
                "workflow_id": 7,
                "image_fingerprint": "123456789abcdef0",
                "open_deliveries": [
                    {
                        "delivery_was_sent": False,
                        "request_id": "9",
                        "node_id": 2,
                        "app_id": 3,
                        "action_id": 4,
                    },
                    {
                        "delivery_was_sent": False,
                        "request_id": "10",
                        "node_id": 3,
                        "app_id": 5,
                        "action_id": 6,
                    },
                ],
            }
        ]

        with path.open("ab") as journal:
            journal.write(record(4, 3, request=9, node=2, app=3, action=4))
        sent = decoded_lines(inspect(executable, path))
        assert sent[-1]["interrupted"][0]["open_deliveries"][0][
            "delivery_was_sent"
        ] is True

        with path.open("ab") as journal:
            journal.write(b"\0")
        corrupt = inspect(executable, path)
        assert corrupt.returncode != 0
        assert corrupt.stdout == b""


def test_completed(
    path: Path,
    workflow: int,
    execution: str,
    outcome: str,
) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    assert lines, "history stream is empty"
    items = [json.loads(line) for line in lines]
    assert all(item["format"] == "hermas-history-v2" for item in items)
    assert all(item["type"] == "record" for item in items[:-1])

    summary = items[-1]
    assert summary["type"] == "summary"
    assert summary["journal_version"] == 2
    assert int(summary["record_count"]) == len(items) - 1
    assert summary["interrupted"] == []
    assert summary["workspace"]["workflow_id"] == workflow

    records = items[:-1]
    assert all(isinstance(item["sequence"], str) for item in records)
    assert all(isinstance(item["execution_id"], str) for item in records)
    assert all(
        item["image_fingerprint"]
        == summary["workspace"]["image_fingerprint"]
        for item in records
    )
    terminal = [
        item
        for item in records
        if item["kind"] == "execution-finished"
        and item["execution_id"] == execution
    ]
    assert len(terminal) == 1
    assert terminal[0]["outcome"] == outcome


def main() -> None:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    contract = subparsers.add_parser("contract")
    contract.add_argument("executable")
    completed = subparsers.add_parser("completed")
    completed.add_argument("path", type=Path)
    completed.add_argument("--workflow", type=int, required=True)
    completed.add_argument("--execution", required=True)
    completed.add_argument("--outcome", required=True)
    arguments = parser.parse_args()

    if arguments.command == "contract":
        test_contract(arguments.executable)
    else:
        test_completed(
            arguments.path,
            arguments.workflow,
            arguments.execution,
            arguments.outcome,
        )


if __name__ == "__main__":
    main()
