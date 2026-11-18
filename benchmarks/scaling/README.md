# M5.5 thread-scaling result

`thread_scaling.json` is the committed 5.5.6c proof. It is produced by:

```sh
./build/linux-release/bin/starforge_scaling_bench --output benchmarks/scaling/thread_scaling.json
```

The tool times a fixed, deterministic workload (parallel chunk generation plus
terrain-height evaluation) at 1, 2, 4, 8 and 16 worker threads. Every thread
count must produce the same FNV-1a checksum (`bit_identical: true`), which
proves parallel execution does not change generated content.

Interpretation:

- Absolute milliseconds are machine-specific (recorded in the JSON); only
  speedup ratios are portable across hardware, per ADR-004's hardware scope.
- This run (Intel i7-14700K, 28 logical threads, mixed P/E cores) reached
  4.56× at 16 threads and 3.15× at 8. The aspirational M5.5 "4× on 4 cores"
  target is **not** met by this mixed-core machine at 4 threads (2.21×);
  the scaling proof is that performance rises monotonically with worker count
  while output stays bit-identical. Re-measure on a homogeneous-core host
  before claiming a specific per-core multiplier.
- A reduced smoke run is wired into CTest as `scaling_bench_smoke` (1/2/4
  threads, small workload).
