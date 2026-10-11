# Android daemon configuration shape checklist
SUMMARY: Always keep package names as top-level JSON keys and use English rule keys; allow mixed CPU selectors and default omitted mode to scan.
READ WHEN: before changing the Android daemon configuration format

---

The supported rule shape is `{ "com.example.app": { "thread_name": "RenderThread", "cpu_cores": ["0-6", "e-core", 7], "sample_interval_ms": 1000 } }`. `cpu_cores` accepts CPU ranges, numeric IDs, numeric strings, and `e-core`, `p-core`, or `hp-core`; array entries are unioned. `mode` is optional and defaults to `scan`. Multiple rules per package remain accepted as a compatibility extension through an array value.
