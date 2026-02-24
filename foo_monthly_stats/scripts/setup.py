#!/usr/bin/env python3
"""
setup.py – Creates necessary output directories for foo_monthly_stats.
Run once before building.
"""
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).parent.parent  # foo_monthly_stats/

dirs = [
    ROOT / "_result" / "x64_Release" / "bin",
    ROOT / "_result" / "x64_Debug" / "bin",
    ROOT / "_result" / "Win32_Release" / "bin",
    ROOT / "_result" / "Win32_Debug" / "bin",
]

for d in dirs:
    d.mkdir(parents=True, exist_ok=True)
    print(f"  Created: {d}")

print("Setup complete.")
