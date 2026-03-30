#!/usr/bin/env python3

import os
import subprocess
import sys
from pathlib import Path


def candidate_libraries():
    return {
        "jemalloc": [
            Path("/opt/homebrew/opt/jemalloc/lib/libjemalloc.dylib"),
            Path("/usr/local/opt/jemalloc/lib/libjemalloc.dylib"),
        ],
        "tcmalloc": [
            Path("/opt/homebrew/opt/gperftools/lib/libtcmalloc.dylib"),
            Path("/opt/homebrew/opt/gperftools/lib/libtcmalloc_minimal.dylib"),
            Path("/usr/local/opt/gperftools/lib/libtcmalloc.dylib"),
            Path("/usr/local/opt/gperftools/lib/libtcmalloc_minimal.dylib"),
        ],
    }


def find_library(name: str):
    for path in candidate_libraries()[name]:
        if path.exists():
            return path
    return None


def run_case(label: str, bench_path: Path, library):
    env = os.environ.copy()
    if library is not None:
        env["DYLD_INSERT_LIBRARIES"] = str(library)
        env["DYLD_FORCE_FLAT_NAMESPACE"] = "1"

    print(f"\n=== {label} ===")
    subprocess.run([str(bench_path)], env=env, check=True)


def main():
    repo_root = Path(__file__).resolve().parents[1]
    bench_path = repo_root / "build" / "bench_allocators"
    if not bench_path.exists():
        print("Benchmark binary not found. Build first with:", file=sys.stderr)
        print("  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release", file=sys.stderr)
        print("  cmake --build build --target bench_allocators", file=sys.stderr)
        return 1

    run_case("system malloc", bench_path, None)

    missing = []
    for allocator in ("jemalloc", "tcmalloc"):
        library = find_library(allocator)
        if library is None:
            missing.append(allocator)
            continue
        run_case(allocator, bench_path, library)

    if missing:
        print("\nSkipped allocators (library not found): " + ", ".join(missing))
        print("Expected Homebrew locations under /opt/homebrew/opt or /usr/local/opt.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
