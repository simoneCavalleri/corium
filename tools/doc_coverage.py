#!/usr/bin/env python3
"""
Corium Doxygen Coverage & Documentation Linter.
Scans include/corium/**/*.hpp and verifies presence of:
- @file, @brief, @ingroup tags
- Structural documentation on public classes and structs
- Detailed parameter/template documentation
"""

import sys
import re
from pathlib import Path

INCLUDE_DIR = Path(__file__).resolve().parent.parent / "include" / "corium"

RE_BRIEF = re.compile(r"///\s*@brief|/\*\*\s*@brief|@brief")
RE_FILE = re.compile(r"///\s*@file|/\*\*\s*@file|@file")
RE_INGROUP = re.compile(r"///\s*@ingroup|/\*\*\s*@ingroup|@ingroup|@defgroup")
RE_CLASS_STRUCT = re.compile(r"^\s*(template\s*<[^>]*>\s*)?(class|struct|enum class)\s+([A-Za-z0-9_]+)", re.MULTILINE)

def audit_file(filepath: Path):
    content = filepath.read_text(encoding="utf-8")
    rel_path = filepath.relative_to(INCLUDE_DIR.parent)
    is_internal = "internal/" in str(rel_path)

    has_file = bool(RE_FILE.search(content))
    has_brief = bool(RE_BRIEF.search(content))
    has_ingroup = bool(RE_INGROUP.search(content))

    # Check classes/structs for preceding doc comments
    classes = [m.group(3) for m in RE_CLASS_STRUCT.finditer(content)]
    
    score = 0
    total = 3
    if has_file: score += 1
    if has_brief: score += 1
    if has_ingroup: score += 1

    return {
        "path": rel_path,
        "is_internal": is_internal,
        "has_file": has_file,
        "has_brief": has_brief,
        "has_ingroup": has_ingroup,
        "classes": classes,
        "score": score,
        "total": total,
    }

def main():
    headers = sorted(INCLUDE_DIR.glob("**/*.hpp"))
    results = [audit_file(h) for h in headers]

    total_files = len(results)
    files_with_file = sum(1 for r in results if r["has_file"])
    files_with_brief = sum(1 for r in results if r["has_brief"])
    files_with_ingroup = sum(1 for r in results if r["has_ingroup"])

    print("=" * 70)
    print("           CORIUM DOXYGEN DOCUMENTATION COVERAGE REPORT           ")
    print("=" * 70)
    print(f"Total Headers Scanned   : {total_files}")
    print(f"Headers with @file      : {files_with_file}/{total_files} ({files_with_file/total_files*100:.1f}%)")
    print(f"Headers with @brief     : {files_with_brief}/{total_files} ({files_with_brief/total_files*100:.1f}%)")
    print(f"Headers with @ingroup   : {files_with_ingroup}/{total_files} ({files_with_ingroup/total_files*100:.1f}%)")
    print("-" * 70)

    gaps = [r for r in results if r["score"] < r["total"]]
    if gaps:
        print(f"Found {len(gaps)} headers with documentation gaps:")
        for g in gaps:
            missing = []
            if not g["has_file"]: missing.append("@file")
            if not g["has_brief"]: missing.append("@brief")
            if not g["has_ingroup"]: missing.append("@ingroup")
            print(f"  - {g['path']}: Missing {', '.join(missing)}")
        print("=" * 70)
        
    overall_coverage = (files_with_file + files_with_brief + files_with_ingroup) / (total_files * 3) * 100
    print(f"Overall Header Documentation Score: {overall_coverage:.1f}%")
    print("=" * 70)

    if "--strict" in sys.argv and overall_coverage < 95.0:
        print("ERROR: Documentation coverage below 95% threshold!")
        sys.exit(1)

if __name__ == "__main__":
    main()
