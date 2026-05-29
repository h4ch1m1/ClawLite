#!/usr/bin/env python3
import argparse
import csv
from collections import defaultdict
from pathlib import Path


GROUPS = {
    "skill_prompt": ["prefix_binary", "linear_scan"],
    "topk": ["heap_topk", "full_sort"],
    "inverted": ["inverted_index", "brute_scan"],
    "lru": ["lru_cache"],
    "history": ["history_full_copy", "history_incremental"],
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path")
    parser.add_argument("--out-dir", default="benchmark_plots")
    args = parser.parse_args()

    import matplotlib.pyplot as plt

    rows = []
    with open(args.csv_path, newline="", encoding="utf-8") as f:
        rows.extend(csv.DictReader(f))

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    for name, cases in GROUPS.items():
        fig, ax = plt.subplots(figsize=(8, 4.5))
        for case in cases:
            data = [(int(r["n"]), float(r["median_ms"])) for r in rows if r["case"] == case]
            data.sort()
            if not data:
                continue
            ax.plot([x for x, _ in data], [y for _, y in data], marker="o", label=case)
        ax.set_xscale("log")
        ax.set_xlabel("N")
        ax.set_ylabel("median ms")
        ax.set_title(name)
        ax.legend()
        ax.grid(True, alpha=0.25)
        fig.tight_layout()
        fig.savefig(out_dir / f"{name}.png", dpi=160)
        plt.close(fig)

    print(out_dir)


if __name__ == "__main__":
    main()
