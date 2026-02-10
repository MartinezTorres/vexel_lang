#!/usr/bin/env python3
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def find_repo_root(start: Path) -> Path:
    for parent in [start] + list(start.parents):
        if (parent / ".git").exists():
            return parent
        if (parent / "docs" / "vexel-rfc.md").is_file():
            return parent
    raise RuntimeError("Could not locate repo root")


def parse_metadata(path: Path):
    command = None
    expect_exit = 0
    expect_stderr = None
    run_generated = False
    pattern = re.compile(r"^\s*//\s*@([A-Za-z0-9_-]+)\s*:\s*(.*)$")

    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        key = match.group(1).strip().lower()
        value = match.group(2).strip()
        if key == "command":
            command = value
        elif key == "expect-exit":
            try:
                expect_exit = int(value)
            except ValueError:
                raise ValueError(f"Invalid @expect-exit in {path}: {value}")
        elif key == "expect-stderr":
            expect_stderr = value
        elif key == "run-generated":
            run_generated = value.lower() in {"true", "1", "yes"}

    if not command:
        raise ValueError(f"Missing @command in {path}")

    return command, expect_exit, expect_stderr, run_generated


def replace_macros(command: str, root: Path) -> str:
    replacements = {
        "{VEXEL}": str(root / "build" / "vexel"),
        "{VEXEL_FRONTEND}": str(root / "build" / "vexel-frontend"),
    }
    for key, value in replacements.items():
        command = command.replace(key, value)
    return command


def run_command(command: str, cwd: Path):
    return subprocess.run(
        command,
        shell=True,
        cwd=cwd,
        capture_output=True,
        text=True,
    )


def compile_and_run(cwd: Path):
    compile_cmd = ["gcc", "-std=c11", "-O2", "out.c", "-o", "out", "-lm"]
    compile_res = subprocess.run(
        compile_cmd,
        cwd=cwd,
        capture_output=True,
        text=True,
    )
    if compile_res.returncode != 0:
        return compile_res, None

    run_res = subprocess.run(
        ["./out"],
        cwd=cwd,
        capture_output=True,
        text=True,
    )
    return compile_res, run_res


def run_test(test_file: Path, root: Path) -> str:
    command, expect_exit, expect_stderr, run_generated = parse_metadata(test_file)
    command = replace_macros(command, root)

    with tempfile.TemporaryDirectory(prefix="vexel_c_test_") as tmp:
        tmp_path = Path(tmp)
        for item in test_file.parent.iterdir():
            if item.is_file():
                shutil.copy(item, tmp_path / item.name)

        compile_res = run_command(command, tmp_path)
        if run_generated:
            if compile_res.returncode != 0:
                return (
                    f"compile failed (expected 0) for {test_file}.\n"
                    f"command: {command}\n"
                    f"stdout:\n{compile_res.stdout}\n"
                    f"stderr:\n{compile_res.stderr}\n"
                )
            gcc_res, run_res = compile_and_run(tmp_path)
            if gcc_res.returncode != 0:
                return (
                    f"gcc failed for {test_file}.\n"
                    f"stdout:\n{gcc_res.stdout}\n"
                    f"stderr:\n{gcc_res.stderr}\n"
                )
            if run_res is None or run_res.returncode != expect_exit:
                actual = run_res.returncode if run_res else "unknown"
                return (
                    f"runtime exit mismatch for {test_file}: expected {expect_exit}, got {actual}.\n"
                    f"stdout:\n{run_res.stdout if run_res else ''}\n"
                    f"stderr:\n{run_res.stderr if run_res else ''}\n"
                )
            return ""

        if compile_res.returncode != expect_exit:
            return (
                f"exit mismatch for {test_file}: expected {expect_exit}, got {compile_res.returncode}.\n"
                f"command: {command}\n"
                f"stdout:\n{compile_res.stdout}\n"
                f"stderr:\n{compile_res.stderr}\n"
            )
        if expect_stderr and expect_stderr not in compile_res.stderr:
            return (
                f"stderr mismatch for {test_file}: expected substring '{expect_stderr}'.\n"
                f"stderr:\n{compile_res.stderr}\n"
            )
        return ""


def main() -> int:
    root = find_repo_root(Path(__file__).resolve())
    tests_root = Path(__file__).resolve().parent
    test_files = sorted(tests_root.rglob("test.vx"))
    if not test_files:
        print("No backend C tests found.")
        return 0

    failures = []
    for test_file in test_files:
        error = run_test(test_file, root)
        if error:
            failures.append(error)

    if failures:
        for error in failures:
            print("FAIL:", error)
        print(f"{len(failures)} backend C test(s) failed.")
        return 1

    print(f"All backend C tests passed ({len(test_files)}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
