#!/usr/bin/env python3
"""Compare cache-off/cache-on CLI runs with exact Document JSON equivalence.

Linux only (/usr/bin/time). Each sample starts a new process, including model
initialization; this is not a warm-engine or cold-filesystem benchmark.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pdfs", nargs="+", type=Path)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--baseline-engine", type=Path,
                        help="Optional older executable, run without cache flags")
    parser.add_argument("--output", required=True, type=Path, help="New directory for logs, artifacts and report")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--cache-bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--layout-backend", default="doclaynet")
    parser.add_argument("--table-backend", default="table-transformer")
    parser.add_argument("--ocr-backend", default="auto")
    parser.add_argument("--dpi", type=int, default=200)
    args = parser.parse_args()
    if args.repeats < 1 or args.cache_bytes < 0 or args.dpi < 1:
        parser.error("repeats and dpi must be positive; cache-bytes must be non-negative")
    if len({pdf.stem for pdf in args.pdfs}) != len(args.pdfs):
        parser.error("PDF filenames must have unique stems")
    for path in [args.engine, args.baseline_engine, *args.pdfs]:
        if path is not None and not path.is_file():
            parser.error(f"file does not exist: {path}")
    args.output.mkdir(parents=True, exist_ok=False)
    report = {"measurement": "fresh CLI processes; filesystem cache not flushed",
              "cache_bytes": args.cache_bytes, "repeats": args.repeats,
              "engine": str(args.engine.resolve()),
              "baseline_engine": str((args.baseline_engine or args.engine).resolve()),
              "backends": {"ocr": args.ocr_backend, "layout": args.layout_backend, "table": args.table_backend},
              "dpi": args.dpi, "documents": []}
    all_equal = True
    for pdf in args.pdfs:
        samples = {"before": [], "after": []}
        for repeat in range(args.repeats):
            # Alternate order to reduce systematic effects of a warmer machine.
            for variant in (("before", "after") if repeat % 2 == 0 else ("after", "before")):
                out = args.output / pdf.stem / f"{repeat}-{variant}"
                out.mkdir(parents=True)
                executable = args.baseline_engine if variant == "before" and args.baseline_engine else args.engine
                command = [str(executable.resolve()), str(pdf.resolve()), "--out", str(out.resolve()),
                           "--dpi", str(args.dpi), "--ocr-backend", args.ocr_backend,
                           "--layout-backend", args.layout_backend, "--table-backend", args.table_backend]
                if variant == "after" or not args.baseline_engine:
                    command += ["--image-cache-bytes", str(args.cache_bytes if variant == "after" else 0)]
                load_before = os.getloadavg()
                with (out / "run.log").open("w") as log:
                    subprocess.run(["/usr/bin/time", "-o", str(out / "time.txt"), "-f", "%e %M %U %S %F", *command],
                                   stdout=log, stderr=subprocess.STDOUT, check=True)
                seconds, rss, user_seconds, system_seconds, major_faults = (out / "time.txt").read_text().split()
                document_bytes = (out / "document.json").read_bytes()
                samples[variant].append({"seconds": float(seconds), "peak_rss_kib": int(rss),
                                         "user_seconds": float(user_seconds), "system_seconds": float(system_seconds),
                                         "major_page_faults": int(major_faults), "load_average_before": load_before,
                                         "load_average_after": os.getloadavg(),
                                         "document_sha256": hashlib.sha256(document_bytes).hexdigest(),
                                         "pages": len(json.loads(document_bytes)["pages"])})
                print(f"{pdf.name} {variant} {repeat + 1}/{args.repeats}: {seconds}s, {rss} KiB", flush=True)
        hashes = {sample["document_sha256"] for group in samples.values() for sample in group}
        equal = len(hashes) == 1
        all_equal = all_equal and equal
        summary = {variant: {"median_seconds": statistics.median(sample["seconds"] for sample in group),
                             "median_peak_rss_kib": statistics.median(sample["peak_rss_kib"] for sample in group)}
                   for variant, group in samples.items()}
        report["documents"].append({"pdf": str(pdf.resolve()), "identical_document_json": equal,
                                    "summary": summary, "samples": samples})
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if not all_equal:
        raise SystemExit("Document JSON differed; inspect retained artifacts before interpreting timing")
    print(f"All Document JSON outputs are byte-identical. Report: {args.output / 'report.json'}")


if __name__ == "__main__":
    main()
