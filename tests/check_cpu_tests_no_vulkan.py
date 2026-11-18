#!/usr/bin/env python3
"""M5 Phase 1 gate: the CPU test binary must not need a Vulkan runtime loader.

On Linux this inspects the ELF DT_NEEDED entries. On other platforms it is a
no-op (the decoupling itself is platform independent).
"""

import pathlib
import shutil
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_cpu_tests_no_vulkan.py <starforge_tests>", file=sys.stderr)
        return 2

    binary = pathlib.Path(sys.argv[1])
    if not binary.exists():
        print(f"FAIL: {binary} does not exist", file=sys.stderr)
        return 1

    readelf = shutil.which("readelf")
    if readelf is None or not sys.platform.startswith("linux"):
        print("SKIP: no readelf / non-Linux platform")
        return 0

    result = subprocess.run(
        [readelf, "-d", str(binary)], capture_output=True, text=True, check=False
    )
    needed = [
        line.split("[")[-1].split("]")[0]
        for line in result.stdout.splitlines()
        if "NEEDED" in line and "[" in line
    ]
    forbidden = [name for name in needed if "vulkan" in name.lower()]
    if forbidden:
        print(f"FAIL: CPU test binary still links Vulkan runtime: {forbidden}")
        return 1
    print(f"CPU test binary needs no Vulkan runtime (NEEDED: {', '.join(needed)})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
