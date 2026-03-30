#!/usr/bin/env python3

import argparse
import json
import os
import platform
import subprocess
import sys
from glob import glob
from pathlib import Path
from statistics import mean


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run allocator benchmark across system malloc, jemalloc, tcmalloc, and mimalloc."
    )
    parser.add_argument(
        "--iterations",
        type=int,
        default=5,
        help="Measured runs per allocator (default: 5).",
    )
    parser.add_argument(
        "--warmup",
        type=int,
        default=1,
        help="Warmup runs per allocator before measurement (default: 1).",
    )
    parser.add_argument(
        "--allocators",
        nargs="+",
        default=["malloc", "jemalloc", "tcmalloc", "mimalloc"],
        help="Allocators to benchmark.",
    )
    parser.add_argument(
        "--output",
        default="build/allocator_benchmark_results.json",
        help="Path to write raw benchmark results JSON.",
    )
    return parser.parse_args()


def candidate_libraries():
    system = platform.system()
    if system == "Darwin":
        return {
            "jemalloc": [
                "/opt/homebrew/opt/jemalloc/lib/libjemalloc.dylib",
                "/usr/local/opt/jemalloc/lib/libjemalloc.dylib",
            ],
            "tcmalloc": [
                "/opt/homebrew/opt/gperftools/lib/libtcmalloc.dylib",
                "/opt/homebrew/opt/gperftools/lib/libtcmalloc_minimal.dylib",
                "/usr/local/opt/gperftools/lib/libtcmalloc.dylib",
                "/usr/local/opt/gperftools/lib/libtcmalloc_minimal.dylib",
            ],
            "mimalloc": [
                "/opt/homebrew/opt/mimalloc/lib/libmimalloc.dylib",
                "/usr/local/opt/mimalloc/lib/libmimalloc.dylib",
            ],
        }

    return {
        "jemalloc": [
            "/usr/lib/*/libjemalloc.so*",
            "/usr/local/lib/libjemalloc.so*",
            "/lib/*/libjemalloc.so*",
        ],
        "tcmalloc": [
            "/usr/lib/*/libtcmalloc.so*",
            "/usr/lib/*/libtcmalloc_minimal.so*",
            "/usr/local/lib/libtcmalloc.so*",
            "/usr/local/lib/libtcmalloc_minimal.so*",
            "/lib/*/libtcmalloc.so*",
            "/lib/*/libtcmalloc_minimal.so*",
        ],
        "mimalloc": [
            "/usr/lib/*/libmimalloc.so*",
            "/usr/local/lib/libmimalloc.so*",
            "/lib/*/libmimalloc.so*",
        ],
    }


def find_library(name: str):
    for pattern in candidate_libraries().get(name, []):
        for match in sorted(glob(pattern)):
            path = Path(match)
            if path.exists():
                return path
    return None


def preload_env_var():
    return "DYLD_INSERT_LIBRARIES" if platform.system() == "Darwin" else "LD_PRELOAD"


def parse_results(output: str):
    rows = []
    for line in output.splitlines():
        if not line.startswith("RESULT "):
            continue
        fields = {}
        for token in line.split()[1:]:
            key, value = token.split("=", 1)
            fields[key] = value
        rows.append(
            {
                "workload": fields["workload"],
                "threads": int(fields["threads"]),
                "live_set": int(fields["live_set"]),
                "min_size": int(fields["min_size"]),
                "max_size": int(fields["max_size"]),
                "total_ops": int(fields["total_ops"]),
                "ops_per_sec": float(fields["ops_per_sec"]),
                "ns_per_op": float(fields["ns_per_op"]),
                "checksum": int(fields["checksum"]),
            }
        )
    if not rows:
        raise RuntimeError("No RESULT lines found in benchmark output.")
    return rows


def run_case(label: str, bench_path: Path, library, warmup: int, iterations: int):
    env = os.environ.copy()
    if library is not None:
        env[preload_env_var()] = str(library)
        if platform.system() == "Darwin":
            env["DYLD_FORCE_FLAT_NAMESPACE"] = "1"

    print(f"\n=== {label} ===")
    for idx in range(warmup):
        print(f"warmup {idx + 1}/{warmup} ...")
        subprocess.run([str(bench_path)], env=env, check=True, stdout=subprocess.DEVNULL)

    runs = []
    for idx in range(iterations):
        completed = subprocess.run(
            [str(bench_path)],
            env=env,
            check=True,
            capture_output=True,
            text=True,
        )
        print(f"run {idx + 1}/{iterations} done")
        runs.append(parse_results(completed.stdout))

    return runs


def summarize_runs(runs_by_allocator):
    summary = {}
    for allocator, runs in runs_by_allocator.items():
        workloads = {}
        for run in runs:
            for row in run:
                workload = workloads.setdefault(
                    row["workload"],
                    {
                        "threads": row["threads"],
                        "live_set": row["live_set"],
                        "min_size": row["min_size"],
                        "max_size": row["max_size"],
                        "ns_per_op_runs": [],
                        "ops_per_sec_runs": [],
                    },
                )
                workload["ns_per_op_runs"].append(row["ns_per_op"])
                workload["ops_per_sec_runs"].append(row["ops_per_sec"])

        for workload in workloads.values():
            workload["ns_per_op_mean"] = mean(workload["ns_per_op_runs"])
            workload["ns_per_op_min"] = min(workload["ns_per_op_runs"])
            workload["ns_per_op_max"] = max(workload["ns_per_op_runs"])
            workload["ops_per_sec_mean"] = mean(workload["ops_per_sec_runs"])

        summary[allocator] = workloads
    return summary


def print_summary(summary):
    baseline = summary.get("malloc")
    if baseline is None:
        baseline = {}

    all_workloads = []
    for allocator_workloads in summary.values():
        for workload in allocator_workloads.keys():
            if workload not in all_workloads:
                all_workloads.append(workload)

    print("\n=== Summary (mean across measured runs) ===")
    for workload in all_workloads:
        print(f"\n[{workload}]")
        print(
            f"{'allocator':<12} {'M ops/s':>12} {'ns/op':>12} {'vs malloc':>12} {'min..max ns':>18}"
        )
        baseline_ns = baseline.get(workload, {}).get("ns_per_op_mean")
        for allocator, allocator_workloads in summary.items():
            if workload not in allocator_workloads:
                continue
            row = allocator_workloads[workload]
            ratio = baseline_ns / row["ns_per_op_mean"] if baseline_ns else 1.0
            print(
                f"{allocator:<12} "
                f"{row['ops_per_sec_mean'] / 1e6:>12.2f} "
                f"{row['ns_per_op_mean']:>12.2f} "
                f"{ratio:>12.2f}x "
                f"{row['ns_per_op_min']:>8.2f}..{row['ns_per_op_max']:<8.2f}"
            )


def main():
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    bench_path = repo_root / "build" / "bench_allocators"
    if not bench_path.exists():
        print("Benchmark binary not found. Build first with:", file=sys.stderr)
        print("  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release", file=sys.stderr)
        print("  cmake --build build --target bench_allocators", file=sys.stderr)
        return 1

    runs_by_allocator = {}
    missing = []

    for allocator in args.allocators:
        if allocator == "malloc":
            runs_by_allocator["malloc"] = run_case(
                "system malloc", bench_path, None, args.warmup, args.iterations
            )
            continue

        library = find_library(allocator)
        if library is None:
            missing.append(allocator)
            continue
        runs_by_allocator[allocator] = run_case(
            allocator, bench_path, library, args.warmup, args.iterations
        )

    summary = summarize_runs(runs_by_allocator)
    print_summary(summary)

    output_path = repo_root / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(
            {
                "platform": platform.platform(),
                "iterations": args.iterations,
                "warmup": args.warmup,
                "summary": summary,
            },
            indent=2,
        )
        + "\n"
    )
    print(f"\nWrote raw summary to {output_path}")

    if missing:
        print("\nSkipped allocators (library not found): " + ", ".join(missing))
        if platform.system() == "Linux":
            print("On Ubuntu, install them with:")
            print("  sudo apt install libjemalloc-dev libgoogle-perftools-dev libmimalloc-dev")
        else:
            print("Expected Homebrew locations under /opt/homebrew/opt or /usr/local/opt.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
