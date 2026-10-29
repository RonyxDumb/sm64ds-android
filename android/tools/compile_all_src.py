#!/usr/bin/env python3
"""Compile every src/ translation unit for Android ARM64, retaining all failures.

By default this compiles the raw recovered code. --apply-projections selects
complete, checked Android source views where available. Both modes are compiler
censuses, not a linkable game target or proof of gameplay correctness.
"""

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
EXTENSIONS = {".c", ".cc", ".cpp", ".cxx"}


def run_one(index, path, clang, clangxx, timeout, out, projections, keep_objects=False):
    rel = path.relative_to(ROOT).as_posix()
    projected = projections.get(rel, path)
    source = projected["path"] if isinstance(projected, dict) else projected
    include_dirs = (
        projected.get("include_dirs", []) if isinstance(projected, dict) else []
    )
    is_cpp = source.suffix != ".c"
    object_path = (
        out / "objects" / ("%06d.o" % index) if keep_objects else Path(os.devnull)
    )
    cmd = [
        str(clangxx if is_cpp else clang),
        "--target=aarch64-linux-android26",
        "-c",
        "-o",
        str(object_path),
        "-O0",
        "-ferror-limit=4",
        "-std=gnu++17" if is_cpp else "-std=gnu11",
        "-DSM64DS_PLATFORM_PC",
        *["-I" + str(directory) for directory in include_dirs],
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "port/ntr/include"),
        "-I" + str(ROOT / "android/app/src/main/cpp"),
        "-I" + str(ROOT),
        "-Wno-unknown-pragmas",
        str(source),
    ]
    try:
        p = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            cwd=ROOT,
            errors="replace",
        )
        status = "ok" if p.returncode == 0 else "failed"
        output = p.stdout
    except subprocess.TimeoutExpired as exc:
        status = "timeout"
        output = exc.stdout or b""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        output += "\nCOMPILER TIMEOUT after %d seconds\n" % timeout
    except OSError as exc:
        status = "tool-error"
        output = str(exc)
    # Every file gets a log, including clean files, to account for the census.
    (out / "logs" / ("%06d.txt" % index)).write_text(
        "FILE: "
        + rel
        + "\nSTATUS: "
        + status
        + "\nCOMMAND: "
        + " ".join(cmd)
        + "\n\n"
        + output,
        encoding="utf-8",
    )
    errors = re.findall(r"^.*?(?:fatal error:|error:).*$", output, re.M)
    first = (
        errors[0].strip() if errors else (output.strip().splitlines()[:1] or [""])[0]
    )
    return {
        "file": rel,
        "projected": rel in projections,
        "status": status,
        "errors": errors[:8],
        "first": first[:600],
        "log": "logs/%06d.txt" % index,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--shard", type=int, default=0)
    ap.add_argument("--shards", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument(
        "--apply-projections",
        action="store_true",
        help="compile audited Android-only projected units where available",
    )
    ap.add_argument(
        "--require-projections-pass",
        action="store_true",
        help="fail the job if a projected source still has errors",
    )
    ap.add_argument(
        "--require-all-pass",
        action="store_true",
        help="fail if any source, including unported platform assembly, fails",
    )
    ap.add_argument(
        "--keep-objects",
        action="store_true",
        help="retain successful AArch64 object files beside the complete logs",
    )
    ap.add_argument(
        "--ndk", type=Path, default=Path(os.environ.get("ANDROID_NDK_HOME", ""))
    )
    args = ap.parse_args()
    if not (0 <= args.shard < args.shards) or args.jobs < 1:
        ap.error("invalid shard or jobs")
    llvm = args.ndk / "toolchains/llvm/prebuilt/linux-x86_64/bin"
    clang, clangxx = llvm / "clang", llvm / "clang++"
    if not clang.is_file() or not clangxx.is_file():
        ap.error("Android NDK LLVM toolchain unavailable: " + str(llvm))
    all_files = sorted(
        p
        for p in (ROOT / "src").rglob("*")
        if p.is_file() and p.suffix.lower() in EXTENSIONS
    )
    work = [
        (i, path) for i, path in enumerate(all_files) if i % args.shards == args.shard
    ]
    out = args.out.resolve()
    (out / "logs").mkdir(parents=True, exist_ok=True)
    if args.keep_objects:
        (out / "objects").mkdir(parents=True, exist_ok=True)
    projections = {}
    if args.apply_projections:
        from arm64_projections import generate

        projections = generate(out / "projected-src")
        from arm64_migration import generate as generate_migrations

        migrations = generate_migrations(out / "migration-src", clang)
        overlap = projections.keys() & migrations.keys()
        if overlap:
            ap.error("ambiguous source projections: " + ", ".join(sorted(overlap)))
        projections.update(migrations)
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        tasks = [
            pool.submit(
                run_one,
                i,
                p,
                clang,
                clangxx,
                args.timeout,
                out,
                projections,
                args.keep_objects,
            )
            for i, p in work
        ]
        for future in concurrent.futures.as_completed(tasks):
            result = future.result()
            results.append(result)
            print(
                "[%s] %s %s"
                % (result["status"].upper(), result["file"], result["first"]),
                flush=True,
            )
    results.sort(key=lambda row: row["file"])
    counts = {
        key: sum(r["status"] == key for r in results)
        for key in ("ok", "failed", "timeout", "tool-error")
    }
    summary = {
        "target": "aarch64-linux-android26",
        "raw_src_total": len(all_files),
        "shard": args.shard,
        "shards": args.shards,
        "tested": len(results),
        "projection_mode": args.apply_projections,
        "counts": counts,
        "results": results,
    }
    (out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    with (out / "failures.txt").open("w", encoding="utf-8") as f:
        for row in results:
            if row["status"] != "ok":
                f.write(
                    row["file"] + "\n  " + row["status"] + ": " + row["first"] + "\n"
                )
    print(
        "SUMMARY "
        + json.dumps(
            {
                "total": len(all_files),
                "tested": len(results),
                "shard": args.shard,
                **counts,
            }
        ),
        flush=True,
    )
    projections_failed = [r for r in results if r["projected"] and r["status"] != "ok"]
    if projections_failed:
        print(
            "PROJECTED FAILURES " + ", ".join(r["file"] for r in projections_failed),
            flush=True,
        )
    step_summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if step_summary:
        with open(step_summary, "a", encoding="utf-8") as f:
            f.write(
                "### Raw src Android ARM64 audit shard %d/%d\n\n"
                % (args.shard + 1, args.shards)
            )
            f.write(
                "Total files: %d; tested: %d; OK: %d; failed: %d; timed out: %d; tool errors: %d.\n\n"
                % (
                    len(all_files),
                    len(results),
                    counts["ok"],
                    counts["failed"],
                    counts["timeout"],
                    counts["tool-error"],
                )
            )
            f.write(
                "Audited projections: %d, of which %d failed; this audit does not link an APK.\n"
                % (sum(r["projected"] for r in results), len(projections_failed))
            )
    if (
        args.require_projections_pass
        and projections_failed
        or args.require_all_pass
        and any(counts[key] for key in ("failed", "timeout", "tool-error"))
    ):
        sys.exit(1)


if __name__ == "__main__":
    main()
