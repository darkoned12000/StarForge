# Baseline vs. budget comparison (4.4b)

- Budgets: ADR-004 `benchmarks/budgets.json` — p95, 1920×1080, interval 6.94 ms, 20% headroom
- Baseline: `competent_10k`
- Hardware scope: AMD Radeon RX 7900 XTX / AMD Ryzen 9 7950X — Development target only. Numbers do not transfer to other hardware.
- Hardware match: no — Baseline CPU 'Intel(R) Core(TM) i7-14700K' does not match budget reference 'AMD Ryzen 9 7950X' — CPU-side budgets may not transfer
- Summary: 2 met, 0 over, 8 unknown (10 lines)

| Budget line | Side | Budget | Measured (p95) | Status | Margin | Milestone | Notes |
|---|---|---|---|---|---|---|---|
| frame_interval | frame | 6.940 ms | 0.211 ms | met | 6.729 ms | — | ADR-004 target frame interval (p95) |
| draw_submission | cpu | 0.800 ms | 0.005 ms | met | 0.795 ms | — | Competent path (CPU culling). Naive path is baseline to beat at 2.00 ms. |
| frustum_culling | cpu | 0.300 ms | — | unknown | — | M5 | 3 ns/instance at 100k assumes early-out, SoA layout. Revisit if naive. |
| transform_composition | cpu | 0.200 ms | — | unknown | — | M4.3 | Double-to-float cast for camera-relative positions. |
| depth_prepass | gpu | 0.300 ms | — | unknown | — | M5 | Optional. |
| gpu_culling | gpu | 0.500 ms | — | unknown | — | M5 | M5 compute shader. Placeholder until measured. |
| main_geometry_pass | gpu | 2.500 ms | — | unknown | — | M4.3 | Reverse-Z, D32_SFLOAT. M4 harness reports whole-frame GPU time only; per-pass timestamps are required before this line can be measured, so it is reported as unknown. |
| reserved_future_subsystems | gpu | 0.860 ms | — | unknown | — | M7-M8 | Single block for planets, atmosphere, environment, effects. Starting placeholder; will need to grow as M7-M8 land. |
| vram_max_bytes | memory | 4294967296 bytes | — | unknown | — | M4.5 | At 100k total / 10k visible, camera-relative positions are 12 B each = 17 MB/s at 144 fps. Full upload would be 173 MB/s, exceeding budget. GPU-resident scene updates only moving objects. |
| upload_bandwidth_max_bytes_per_sec | memory | 104857600 bytes/s | — | unknown | — | M5 | At 100k total / 10k visible, camera-relative positions are 12 B each = 17 MB/s at 144 fps. Full upload would be 173 MB/s, exceeding budget. GPU-resident scene updates only moving objects. |
