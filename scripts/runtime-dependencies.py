#!/usr/bin/env python3
"""Resolve the managed assembly closure from the packaged BCL, never host GAC."""
import argparse
import re
import shutil
import subprocess
from pathlib import Path


def references(path):
    output = subprocess.check_output(["monodis", "--assemblyref", str(path)], text=True)
    return re.findall(r"^\s*Name=(.+)$", output, re.MULTILINE)


def closure(executable, bcl):
    pending = [executable]
    selected = {}
    while pending:
        for name in references(pending.pop()):
            if name in selected:
                continue
            candidate = bcl / (name + ".dll")
            if not candidate.is_file():
                raise SystemExit(f"Missing managed dependency in pinned BCL: {candidate}")
            selected[name] = candidate
            pending.append(candidate)
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("bcl", type=Path)
    parser.add_argument("--copy-to", type=Path)
    args = parser.parse_args()
    selected = closure(args.executable, args.bcl)
    if args.copy_to:
        args.copy_to.mkdir(parents=True, exist_ok=True)
        for path in selected.values():
            shutil.copy2(path, args.copy_to / path.name)
    for name in sorted(selected):
        print(name + ".dll")
    print(f"Dependency closure: {len(selected)} assemblies")


if __name__ == "__main__":
    main()
