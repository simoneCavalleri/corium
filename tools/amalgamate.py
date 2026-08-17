#!/usr/bin/env python3
"""
Amalgamation script for Corium.
Generates a standalone single-header distribution: single_include/corium.hpp
and single_include/corium/corium.hpp

Usage:
  python3 tools/amalgamate.py         # Generate single-header distribution
  python3 tools/amalgamate.py --check # Verify single-header is synchronized (CI check)
"""

import os
import re
import sys
import shutil
import argparse

INCLUDE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "include"))
OUTPUT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "single_include"))
OUTPUT_FILE = os.path.join(OUTPUT_DIR, "corium.hpp")
NESTED_OUTPUT_DIR = os.path.join(OUTPUT_DIR, "corium")
NESTED_OUTPUT_FILE = os.path.join(NESTED_OUTPUT_DIR, "corium.hpp")

INCLUDE_PATTERN = re.compile(r'^\s*#include\s+["<]corium/([^">]+)[">]\s*$')

visited_files = set()
processed_lines = []

def process_file(rel_path):
    abs_path = os.path.join(INCLUDE_DIR, "corium", rel_path)
    canonical = os.path.normpath(abs_path)

    if canonical in visited_files:
        return
    visited_files.add(canonical)

    if not os.path.exists(canonical):
        print(f"Warning: File not found: {canonical}", file=sys.stderr)
        return

    with open(canonical, "r", encoding="utf-8") as f:
        lines = f.readlines()

    processed_lines.append(f"\n// >>> Begin: corium/{rel_path}\n")
    for line in lines:
        match = INCLUDE_PATTERN.match(line)
        if match:
            dep_rel = match.group(1)
            process_file(dep_rel)
        elif line.strip() == "#pragma once":
            continue
        else:
            processed_lines.append(line)
    processed_lines.append(f"\n// <<< End: corium/{rel_path}\n")

def main():
    parser = argparse.ArgumentParser(description="Corium Single-Header Amalgamator")
    parser.add_argument("--check", action="store_true", help="Verify single_include files are synchronized without writing")
    args = parser.parse_args()

    process_file("corium.hpp")

    banner = """// =============================================================================
// Corium - High-Performance Zero-Heap C++20 MPSC Event Framework
// Single-Header Standalone Amalgamated Distribution
//
// Generated automatically by tools/amalgamate.py. DO NOT EDIT DIRECTLY.
// MIT License - Copyright (c) 2026 Simone Cavalleri
// =============================================================================

#pragma once

"""

    generated_content = banner + "".join(processed_lines)

    if args.check:
        if not os.path.exists(OUTPUT_FILE):
            print(f"Error: {OUTPUT_FILE} does not exist. Run 'python3 tools/amalgamate.py' to generate it.", file=sys.stderr)
            sys.exit(1)

        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            existing_content = f.read()

        if existing_content != generated_content:
            print(f"Error: {OUTPUT_FILE} is out of date with include/. Run 'python3 tools/amalgamate.py' to update it.", file=sys.stderr)
            sys.exit(1)

        print(f"Check passed: {OUTPUT_FILE} is synchronized with include/ ({len(visited_files)} headers).")
        sys.exit(0)

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    os.makedirs(NESTED_OUTPUT_DIR, exist_ok=True)

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write(generated_content)

    shutil.copyfile(OUTPUT_FILE, NESTED_OUTPUT_FILE)

    print(f"Successfully generated single-header distribution:")
    print(f"  - {OUTPUT_FILE}")
    print(f"  - {NESTED_OUTPUT_FILE}")
    print(f"Total headers amalgamated: {len(visited_files)}")

if __name__ == "__main__":
    main()
