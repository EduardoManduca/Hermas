#!/usr/bin/env python3
import argparse
import json
import pathlib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=pathlib.Path)
    parser.add_argument("--execution", required=True)
    parser.add_argument("--outcome", required=True)
    parser.add_argument("--value-hex", required=True)
    arguments = parser.parse_args()

    lines = arguments.path.read_text(encoding="utf-8").splitlines()
    if len(lines) != 1:
        raise SystemExit("expected exactly one execution result object")
    result = json.loads(lines[0])
    if set(result) != {
        "format",
        "execution_id",
        "outcome",
        "source_type",
        "destination_type",
        "value_hex",
    }:
        raise SystemExit("execution result fields differ")
    if result["format"] != "hermas-execution-result-v1":
        raise SystemExit("unsupported execution result format")
    if result["execution_id"] != arguments.execution:
        raise SystemExit("execution identity differs")
    if result["outcome"] != arguments.outcome:
        raise SystemExit("terminal outcome differs")
    if result["value_hex"] != arguments.value_hex:
        raise SystemExit("canonical result value differs")
    for field in ("source_type", "destination_type"):
        value = result[field]
        if not isinstance(value, int) or isinstance(value, bool):
            raise SystemExit(f"{field} is not an integer")
        if value < 0 or value > 65535:
            raise SystemExit(f"{field} is outside the protocol bound")


if __name__ == "__main__":
    main()
