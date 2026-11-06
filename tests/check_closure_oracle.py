#!/usr/bin/env python3
"""Exhaustive small oracle for completed, excluded and unfinished alternatives.

Run separately from the regression suite:
    python3 tests/check_closure_oracle.py build/vexel
"""
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1] if len(sys.argv) > 1 else "build/vexel").resolve()
    count = 0
    with tempfile.TemporaryDirectory(prefix="vexel-closure-oracle-") as directory:
        source = Path(directory) / "main.vx"
        report = Path(directory) / "search.json"
        # None denotes an unfinished proof, not a third possible result value.
        for mapping in itertools.product((None, 0, 1), repeat=3):
            expressions = ["a" if value is None else f"(#u8){value}" for value in mapping]
            for mask in range(8):
                allowed = [index for index in range(3) if mask & (1 << index)]
                condition = "||".join(f"(n=={index})" for index in allowed) or "0"
                source.write_text(
                    "a=b;b=a;^answer:#u8=?{n=?(#u8,[0,1,2]);"
                    f"?({condition});size:#u8[n+1];"
                    f"(n==0)?{expressions[0]}:((n==1)?{expressions[1]}:{expressions[2]})"
                    "};"
                )
                possible = {mapping[index] for index in allowed}
                known = possible - {None}
                if len(known) > 1:
                    expected = "ambiguous"
                elif None in possible:
                    expected = "unresolved"
                elif known:
                    expected = "resolved"
                else:
                    expected = "rejected"
                for schedule, branch in itertools.product(
                    ("fifo", "lifo", "random"), ("forward", "reverse")
                ):
                    result = subprocess.run(
                        [str(executable), str(source), "--json", "--search-report", str(report),
                         "--schedule", schedule, "--branch-order", branch],
                        capture_output=True, text=True, timeout=30,
                    )
                    evidence = json.loads(report.read_text())
                    context = (mapping, mask, schedule, branch, expected, evidence, result.stderr)
                    assert evidence["status"] == expected, context
                    assert result.returncode == (0 if expected == "resolved" else 1), context
                    if expected == "resolved":
                        value = int(json.loads(result.stdout)["roots"][0]["value"])
                        assert value == next(iter(known)), context
                    count += 1
    print(f"{count} independent outcome oracle cases passed")


if __name__ == "__main__":
    main()
