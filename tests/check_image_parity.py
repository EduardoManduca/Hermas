#!/usr/bin/env python3
"""Require the independent Rust and C image decoders to agree."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


def accepts(command: list[str], image: pathlib.Path) -> bool:
    result = subprocess.run(
        [*command, str(image)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0


def cases(image: bytes):
    yield "valid", image, True
    truncations = {
        0,
        1,
        3,
        4,
        6,
        79,
        80,
        len(image) // 2,
        len(image) - 1,
    }
    for length in sorted(item for item in truncations if item < len(image)):
        yield f"truncated-{length}", image[:length], False
    mutations = {
        "magic": (0, 0xFF),
        "version": (4, 0xFF),
        "header-size": (6, 0xFF),
        "total-size": (8, 0xFF),
        "flags": (12, 0x01),
        "header-reserved": (70, 0x01),
        "tail-reserved": (76, 0x01),
    }
    for name, (offset, value) in mutations.items():
        mutated = bytearray(image)
        mutated[offset] = value
        yield name, bytes(mutated), False
    yield "trailing-byte", image + b"\0", False


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: check_image_parity.py HERMAS C_CHECK IMAGE...",
            file=sys.stderr,
        )
        return 2
    rust = [sys.argv[1], "image", "check"]
    c = [sys.argv[2]]
    with tempfile.TemporaryDirectory(prefix="hermas-image-parity-") as root:
        candidate = pathlib.Path(root, "candidate.hgi")
        for source_text in sys.argv[3:]:
            source = pathlib.Path(source_text)
            image = source.read_bytes()
            for name, data, expected in cases(image):
                candidate.write_bytes(data)
                rust_accepts = accepts(rust, candidate)
                c_accepts = accepts(c, candidate)
                if rust_accepts != c_accepts or rust_accepts != expected:
                    print(
                        f"{source}:{name}: expected={expected} "
                        f"rust={rust_accepts} c={c_accepts}",
                        file=sys.stderr,
                    )
                    return 1
    print("Rust/C graph-image parity passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
