# Warmth Fabric

**Warmth Fabric** is a production-grade, open-source, vendor-neutral **C++20 runtime** for
governing *operational warmth* across heterogeneous AI serving infrastructure. It answers the
question:

> **How ready is this runtime to execute useful work right now, what is already prepared, what
> remains cold, and what will it cost to become ready?**

Warmth is treated as a **first-class runtime state**, not a boolean, and is never silently
conflated with the artifacts, residency, or replica lifecycle that surround it.

---

## Systems boundary

Warmth Fabric is deliberately **not** Model Cache, Model Residency, or Replica Fabric. Each of
those addresses a distinct question:

| System            | Question                                                                    |
|-------------------|-----------------------------------------------------------------------------|
| **Model Cache**   | What reusable artifact exists? (weights, adapters, tokenizers, kernels, maps) |
| **Model Residency** | Where do active model weights and adapters reside?                         |
| **Replica Fabric**  | Which live replica is allowed to serve?                                    |
| **Warmth Fabric**   | How ready is that runtime to execute useful work RIGHT NOW, what is already prepared, what remains cold, and what will it cost to become ready? |

Warmth Fabric governs the *degree of execution preparedness within and around* those replicas: it
is the readiness dimension that Model Cache, Model Residency, and Replica Fabric leave open.

### Example questions Warmth Fabric answers

- Are model weights already resident?
- Is the CUDA context initialized?
- Are required adapters active?
- Are kernels compiled and loaded?
- Are execution graphs captured and valid?
- Is KV/prefix state already reusable?
- Is tokenizer/runtime state initialized?
- Is allocator state prepared?
- Is the serving engine primed?
- How much work remains before first useful execution?
- How quickly is existing warmth decaying?
- Has a dependency generation changed and invalidated prior warmth?

---

## Warmth dimensions

Warmth is tracked across **twelve independent dimensions** that are never collapsed into a single
opaque number:

`artifact_availability`, `artifact_validation`, `model_residency`, `adapter_residency`,
`tokenizer_readiness`, `cuda_context_readiness`, `kernel_readiness`, `graph_readiness`,
`allocator_readiness`, `prefix_kv_reuse`, `engine_readiness`, `local_dependency_readiness`.

Each dimension carries an explicit status (`COLD`, `DISCOVERED`, `PARTIAL`, `READY`, `VALID`,
`SUSPECT`). A composite level exists for convenience but is always derivable from the components
and remains explainable.

## Warmth lifecycle

Explicit, guarded states: `COLD -> DISCOVERED -> PREPARING -> PARTIALLY_WARM -> WARM -> HOT`, plus
`STALE`, `INVALIDATED`, `EVICTED`, `FAILED`. Transitions are validated against a deterministic
table; **invalid transitions fail** rather than silently succeeding.

## Readiness & warming cost

For each workload/replica, both estimated and measured cost are kept *distinct*: bytes to
transfer, artifacts to validate, kernels to compile/load, graphs to capture, context init,
adapter activation, tokenizer init, prefix/KV reconstruction, allocator init, engine startup,
synchronization, and dependency checks. `estimated`, `measured`, `reported`, `derived`, and
`unknown` are never conflated.

## Decay & invalidation

Warmth decays deterministically with configurable semantics (idle time, memory pressure,
reclamation, process restart, device reset, dependency generation change, artifact replacement,
adapter change, runtime upgrade, kernel/graph invalidation, topology change, policy change).
Invalidation is explicit; stale or invalid objects are never treated as execution-ready.
Identical state + policy always produces identical decay.

## Budgets & scheduling

Explicit budgets for device memory, pinned host memory, host memory, storage footprint,
concurrent warming operations, transfer bandwidth, active engines, and warm replicas. Admission
respects budgets and never silently overcommits. A scheduler builds an explainable, deterministic
(score-desc, then id-asc) warming plan over explicit preference components.

## Distributed authority

A real coordinator plus workers run over **framed TCP**. Workers register with `WorkerId`,
`WorkerBootId`, `NodeId`, capabilities, device inventory, observed warmth objects, supported
actions, relevant generations, and protocol version. The coordinator owns authoritative metadata
and validates `CoordinatorEpoch`, `WorkerBootId`, `WarmthGeneration`, `DependencyGeneration`,
`ReplicaGeneration`, and `AttemptId`. **Stale authority is rejected**; it can never mark stale
state warm, restore invalidated readiness, complete obsolete warming, mutate current measured
costs, overwrite fresh generations, revive pre-restart device state, or release budgets wrongly.

## Persistence & recovery

Authoritative metadata is persisted with a **versioned, checksummed binary encoding**
(big-endian, CRC-32 over the payload) that rejects malformed lengths, truncation, checksum
corruption, duplicate IDs/fields, invalid enums, impossible transitions, invalid generation
relations, NaN/Inf, overflow, trailing garbage, and incompatible versions. On recovery,
process-local and device-local ephemeral state (CUDA contexts, loaded modules, graph instances,
allocator pools) is **revalidated** and never assumed alive.

## CUDA proof

A real validation proof runs on the available **NVIDIA GeForce RTX 5090 / Blackwell sm_120**
through **CUDA 13.1**: cold -> init CUDA -> allocate -> load artifact -> host->device -> run -> sync ->
verify -> record cold timing -> retain and repeat (warm) -> record warm timing -> capture/instantiate/
replay a CUDA graph -> invalidate/teardown -> prove warmth falls -> rewarm -> verify fresh readiness ->
release -> verify cleanup and bounded memory recovery. Where hardware is unavailable, explicitly
labeled synthetic models are used.

## Build, install, use

Requirements: CMake >= 3.25, a C++20 compiler (MSVC 2022 / Clang / GCC), optional CUDA 13.1
toolkit. Windows-first with clean cross-platform abstractions.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release

Install and use as a downstream package:

    cmake --install build --prefix <prefix>
    # then in a consumer project
    find_package(WarmthFabric REQUIRED)
    target_link_libraries(app PRIVATE warmth::warmth)

See `docs/architecture.md`, `docs/examples.md`, and `docs/benchmarks.md`.

## CLI

`tools/warmth_cli` provides `list`, `inspect`, `readiness`, `warm`, `invalidate`, `demote`,
`evict`, `explain`, `plan`, `budgets`, `snapshot`, `save`, `recover`, `synthetic`, `multiprocess`,
`cuda`, `benchmark`, plus `--coordinator` and `--worker` modes, with `--json` output.

## Testing, no timeouts

The build is configured so **no test timeout mechanism exists anywhere**: no `timeoutMs`, no CTest
`TIMEOUT`, no watchdog, no process time limits. Tests run to natural pass, fail, crash, or manual
termination after a diagnosed genuine hang. The suite covers the full lifecycle, guarded
transitions, dimensions, decay, invalidation, dependency changes, plans, warming, budgets,
fairness, priority, eviction/demotion, persistence, recovery, stale process-local state, stale
CUDA readiness, worker restart, stale boot/epoch/generation, malformed protocol, corruption and
truncation rejection, deterministic replay, concurrency, accounting-to-zero, real TCP
coordinator/worker, and CUDA cold-warm-invalidate-rewarm.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. See `LICENSE`.
