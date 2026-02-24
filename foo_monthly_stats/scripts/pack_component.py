#!/usr/bin/env python3
"""
pack_component.py – Packages foo_monthly_stats.dll into a .fb2k-component file.

Usage:
    python scripts/pack_component.py --platform x64 --configuration Release
"""
import argparse
import pathlib
import shutil
import sys
import zipfile

ROOT = pathlib.Path(__file__).parent.parent  # foo_monthly_stats/


def main():
    parser = argparse.ArgumentParser(description="Pack foo_monthly_stats component")
    parser.add_argument("--platform", default="x64", help="Win32 or x64")
    parser.add_argument("--configuration", default="Release", help="Debug or Release")
    args = parser.parse_args()

    bin_dir = ROOT / "_result" / f"{args.platform}_{args.configuration}" / "bin"
    dll_path = bin_dir / "foo_monthly_stats.dll"

    if not dll_path.exists():
        print(f"ERROR: DLL not found at {dll_path}", file=sys.stderr)
        sys.exit(1)

    out_dir = ROOT / "_result"
    out_path = out_dir / "foo_monthly_stats.fb2k-component"

    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(dll_path, "foo_monthly_stats.dll")

    size = out_path.stat().st_size
    print(f"Packaged: {out_path}  ({size:,} bytes)")


if __name__ == "__main__":
    main()
