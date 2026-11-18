#!/usr/bin/env python3
"""M5 Phase 1 convention gate: CCW front face, no projection Y-flip, negative-height viewports.

Runs as a CTest so a regression in any of these conventions fails the build
quickly, without needing a GPU.
"""

import pathlib
import sys


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_render_conventions.py <repo-root>", file=sys.stderr)
        return 2
    root = pathlib.Path(sys.argv[1])
    failures: list[str] = []

    pipeline = (root / "src/vulkan/vulkan_pipeline.cpp").read_text(encoding="utf-8")
    if "VK_FRONT_FACE_COUNTER_CLOCKWISE" not in pipeline:
        failures.append("vulkan_pipeline.cpp does not set VK_FRONT_FACE_COUNTER_CLOCKWISE")
    if "VK_FRONT_FACE_CLOCKWISE" in pipeline.replace("VK_FRONT_FACE_COUNTER_CLOCKWISE", ""):
        failures.append("vulkan_pipeline.cpp contains a CLOCKWISE front-face setting")

    math_header = (root / "src/math/math.hpp").read_text(encoding="utf-8")
    if "*= -1.0f" in math_header:
        failures.append("math.hpp still flips Y in the projection matrix")

    # No source may reintroduce a projection Y-flip.
    for path in (root / "src").rglob("*.cpp"):
        text = path.read_text(encoding="utf-8")
        if "proj[1][1] *=" in text:
            failures.append(f"{path.relative_to(root)} flips projection Y")

    # A positive-height VkViewport is the specific regression fixed in 5.5.5/Sun.
    for path in list((root / "src").rglob("*.cpp")) + list((root / "tests").rglob("*.cpp")):
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if "VkViewport" in line and "0.0f, 0.0f," in line:
                failures.append(
                    f"{path.relative_to(root)}:{number} uses a positive-height viewport"
                )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1
    print("Render conventions OK: CCW front face, no projection Y-flip, negative-height viewports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
