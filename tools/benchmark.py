#!/usr/bin/env python3
import argparse
import csv
import math
import random
import statistics
import time
from collections import OrderedDict, defaultdict


def timed(fn, repeat, warmup):
    for _ in range(warmup):
        fn()
    samples = []
    for _ in range(repeat):
        t0 = time.perf_counter()
        fn()
        samples.append((time.perf_counter() - t0) * 1000.0)
    samples.sort()
    return statistics.median(samples), samples[int((len(samples) - 1) * 0.95)]


def bench_prefix(n, repeat, warmup):
    lengths = [random.randint(20, 120) for _ in range(n)]
    budget = sum(lengths) // 2

    def linear():
        total = 0
        keep = 0
        for length in lengths:
            if total + length > budget:
                break
            total += length
            keep += 1
        return keep

    prefix = [0]
    for length in lengths:
        prefix.append(prefix[-1] + length)

    def binary():
        lo, hi = 0, n
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if prefix[mid] <= budget:
                lo = mid
            else:
                hi = mid - 1
        return lo

    return ("prefix_binary", n, *timed(binary, repeat, warmup)), ("linear_scan", n, *timed(linear, repeat, warmup))


def bench_topk(n, repeat, warmup):
    import heapq
    data = [random.random() for _ in range(n)]
    k = min(50, n)

    def heap_topk():
        heap = []
        for x in data:
            if len(heap) < k:
                heapq.heappush(heap, x)
            elif x > heap[0]:
                heapq.heapreplace(heap, x)
        return heap

    def full_sort():
        return sorted(data, reverse=True)[:k]

    return ("heap_topk", n, *timed(heap_topk, repeat, warmup)), ("full_sort", n, *timed(full_sort, repeat, warmup))


def bench_lru(n, repeat, warmup):
    keys = [random.randint(0, max(1, n // 5)) for _ in range(n)]

    def lru():
        cache = OrderedDict()
        cap = max(8, n // 20)
        hits = 0
        for key in keys:
            if key in cache:
                hits += 1
                cache.move_to_end(key, last=False)
            else:
                cache[key] = key
                cache.move_to_end(key, last=False)
                if len(cache) > cap:
                    cache.popitem(last=True)
        return hits

    return ("lru_cache", n, *timed(lru, repeat, warmup))


def bench_inverted(n, repeat, warmup):
    vocab = [f"t{i}" for i in range(200)]
    docs = [" ".join(random.choice(vocab) for _ in range(30)) for _ in range(n)]
    query = docs[n // 2].split()[0]

    def inverted():
        index = defaultdict(list)
        for i, doc in enumerate(docs):
            seen = set(doc.split())
            for term in seen:
                index[term].append(i)
        return index.get(query, [])

    def scan():
        return [i for i, doc in enumerate(docs) if query in doc.split()]

    return ("inverted_index", n, *timed(inverted, repeat, warmup)), ("brute_scan", n, *timed(scan, repeat, warmup))


def bench_history(n, repeat, warmup):
    turns = [f"user {i} assistant {i}" for i in range(n)]

    def full_copy():
        messages = []
        for turn in turns:
            messages = messages + [turn]
        return messages

    def incremental():
        messages = []
        for turn in turns:
            messages.append(turn)
        return messages

    return ("history_full_copy", n, *timed(full_copy, repeat, warmup)), ("history_incremental", n, *timed(incremental, repeat, warmup))


def bench_expression(n, repeat, warmup):
    exprs = [f"{i % 10}+{(i + 1) % 10}*({(i + 2) % 10}+1)" for i in range(n)]

    def parse_eval():
        total = 0
        for expr in exprs:
            total += eval(expr, {"__builtins__": {}})
        return total

    return ("expression_parser_proxy", n, *timed(parse_eval, repeat, warmup))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default="benchmark_results.csv")
    parser.add_argument("--repeat", type=int, default=7)
    parser.add_argument("--warmup", type=int, default=2)
    args = parser.parse_args()

    rows = []
    for n in [100, 1000, 10000]:
        for result in bench_prefix(n, args.repeat, args.warmup):
            rows.append(result)
        for result in bench_topk(n, args.repeat, args.warmup):
            rows.append(result)
        rows.append(bench_lru(n, args.repeat, args.warmup))
        for result in bench_inverted(n, args.repeat, args.warmup):
            rows.append(result)
        for result in bench_history(n, args.repeat, args.warmup):
            rows.append(result)
        rows.append(bench_expression(n, args.repeat, args.warmup))

    with open(args.out, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["case", "n", "median_ms", "p95_ms"])
        writer.writerows(rows)
    print(args.out)


if __name__ == "__main__":
    main()
