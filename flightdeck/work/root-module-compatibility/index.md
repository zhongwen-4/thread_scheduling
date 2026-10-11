# Index — root module compatibility

## State

Active. Native daemon configuration and Android build compatibility are implemented for the current phase.

## Next

Add the shared Magisk/KernelSU/APatch module wrapper (`module.prop`, `service.sh`, packaging) after the daemon behavior is settled.

## Read now

- flightdeck/knowledge/android/root-module-compatibility.md
- flightdeck/knowledge/android/thread-binding-power.md
- flightdeck/knowledge/project/requirements-clarity.md

## Read if

- root_module_compatibility_guide.md — when adding module files or installer behavior
- thread_cpu_binding_design.md — when changing binding or sampling semantics

## Progress

Done:
- Top-level package keyed JSON rules with English keys.
- CPU selector parsing for ranges, numeric IDs, numeric strings, and e-core/p-core/hp-core combinations.
- Optional mode with `scan` default.
- Restored CMake target and included topology/event sources.
- Android arm64-v8a build completed with NDK 27.0.12077973.

Current:
- Daemon runtime and Root module wrapper remain to be tested on a rooted device.

## Verification

- `applist.json` parses with the requested shape.
- Android CMake/Ninja build succeeds for `threadschedd`.
- `git diff --check` passes.

## Open questions

- Target device CPU topology and proc connector availability still need device-side validation.
