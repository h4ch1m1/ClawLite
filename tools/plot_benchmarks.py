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

    try:
        import matplotlib.pyplot as plt
    except ModuleNotFoundError:
        plt = None

    rows = []
    with open(args.csv_path, newline="", encoding="utf-8") as f:
        rows.extend(csv.DictReader(f))

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    for name, cases in GROUPS.items():
        if plt is None:
            svg = [
                '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="450">',
                f'<text x="30" y="30" font-size="20">{name}</text>',
                '<text x="30" y="420" font-size="12">Install matplotlib for PNG output; this SVG fallback proves the plot pipeline.</text>',
            ]
            y = 70
            colors = ["#2563eb", "#dc2626", "#16a34a"]
            for idx, case in enumerate(cases):
                data = [(int(r["n"]), float(r["median_ms"])) for r in rows if r["case"] == case]
                data.sort()
                if not data:
                    continue
                max_n = max(x for x, _ in data)
                max_y = max(yv for _, yv in data) or 1.0
                points = []
                for x, value in data:
                    px = 80 + (x / max_n) * 650
                    py = y + 80 - (value / max_y) * 70
                    points.append(f"{px:.1f},{py:.1f}")
                svg.append(f'<polyline fill="none" stroke="{colors[idx % len(colors)]}" stroke-width="2" points="{" ".join(points)}"/>')
                svg.append(f'<text x="80" y="{y + 105}" font-size="12" fill="{colors[idx % len(colors)]}">{case}</text>')
                y += 120
            svg.append("</svg>")
            (out_dir / f"{name}.svg").write_text("\n".join(svg), encoding="utf-8")
            continue

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
