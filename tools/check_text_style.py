#!/usr/bin/env python3
"""Reject typographic long dashes in versioned repository text files."""
from __future__ import annotations

import pathlib
import subprocess
import sys

# Only the simple ASCII hyphen-minus is allowed.
FORBIDDEN = ("\u2012", "\u2013", "\u2014", "\u2015")


def main() -> int:
    files = subprocess.check_output(["git", "ls-files", "-z"]).split(b"\0")
    problems: list[tuple[str, int]] = []
    for raw_name in files:
        if not raw_name:
            continue
        name = raw_name.decode("utf-8")
        path = pathlib.Path(name)
        if not path.is_file():
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        try:
            body = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(body.splitlines(), 1):
            if any(dash in line for dash in FORBIDDEN):
                problems.append((name, line_number))

    for name, line_number in problems:
        print(f"{name}:{line_number}: long typographic dash forbidden; use '-'")

    if problems:
        return 1
    print("Typography OK: no long dashes found in tracked text files.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
