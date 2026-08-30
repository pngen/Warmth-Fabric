# Warmth Fabric Architecture

Warmth Fabric is a self-contained C++20 library (`libwarmth`) with a clean public header surface
under `include/warmth/`, a CLI (`tools/warmth_cli`), a real CUDA validation module
(`cuda/warmth_cuda.cu`), tests, examples, and benchmarks. It has no hard structural dependency on
Model Cache, Kernel Cache, Graph Cache, Prefix/KV Fabric, Replica Fabric, or any scheduler; it only
defines stable interfaces/adapters so it can be integrated without coupling.

## Core model

- **Identity** (`identity.hpp`): stable strongly-typed 128-bit identities (`WarmthObjectId`,
  `WorkloadId`, `ModelId`, ...) with deterministic content derivation and string round-tripping,
  plus strongly-typed monotonically increasing **authority generations** (`WarmthGeneration`,
  `DependencyGeneration`, `PolicyGeneration`, `CoordinatorEpoch`, `AttemptId`, ...). Identities
  are never silently reused across authority eras.

- **State machine** (`warmth_state.hpp`): explicit, guarded `WarmthState` transitions. Invalid
  transitions fail deterministically. `WARM` and `HOT` are execution-ready.

- **Dimensions** (`warmth_dimensions.hpp`): twelve independent readiness dimensions, each with an
  explicit status; a composite level is derived from and explainable by the components.

- **Object** (`warmth_object.hpp`): `WarmthObject` carries identity, category, owner aspects,
  state, dimensions, residency, lifecycle, freshness/confidence, generations, dependency
  generations, provenance, invalidation reason, and cost/time-to-ready metadata. A `restore_state`
  path (trusted deserialization) is distinct from the guarded `transition` path.

- **Cost** (`cost_model.hpp`): estimated vs measured vs reported vs derived vs unknown cost
  breakdowns and time-to-first-useful-work.

- **Decay & invalidation** (`decay.hpp`, `invalidation.hpp`): deterministic decay semantics and
  explicit invalidation triggers.

- **Budget** (`budget.hpp`): `BudgetTracker` with per-resource reservations, limit admission,
  double-release detection, and accounting-to-zero.

- **Scheduler** (`scheduler.hpp`): `PlanBuilder` prioritizes candidates over explicit preference
  components with a deterministic tie-break (score desc, then id asc).

- **Policy** (`policy.hpp`): all config (decay, budgets, scheduling weights) plus its generation.

- **Explanation** (`explanation.hpp`): structured readiness/plan explanations serialized to text
  and deterministic JSON.

- **Persistence** (`persistence.hpp`, `src/persistence.cpp`): versioned, checksummed
  (CRC-32), big-endian binary snapshot encoding with strict decode validation and canonical
  file save/load.

## Distributed authority

- **Transport** (`transport.hpp`/`.cpp`): Winsock-first (Windows) with a clean POSIX fallback;
  blocking TCP connections and listeners, exact read/write.

- **Protocol** (`protocol.hpp`): framed messages (magic + version + payload length + CRC-32), a
  strict `FrameReader`/`FrameWriter`, and a big-endian wire codec; malformed frames are rejected.

- **Messages** (`messages.hpp`): HELLO / HELLO_ACK / REPORT / WARM_TOPIC / WARM_RESULT / PING /
  PONG / SHUTDOWN / ERROR payloads.

- **Coordinator** (`coordinator.hpp`/`.cpp`): owns authoritative warmth metadata, worker
  registry, budget tracker, epoch and generations. Per-connection threads perform network I/O only
  outside the global lock. It validates authority on every report/result and rejects stale
  boot/epoch/generation; it never holds the master lock across network/blocking work. On a worker
  boot change it invalidates inherited live readiness; it never resurrects device-local state.

- **Worker** (`worker.hpp`/`.cpp`): connects, registers, services warming commands through a
  pluggable `WarmHook` (synthetic or real), and reports results/readiness.

An in-process loopback harness (in `test_protocol_loopback`) exercises the real framed-TCP path
across threads, and the CLI `multiprocess` command runs a coordinator plus worker OS processes.

## Concurrency

All fabric/coordinator state is guarded by an internal mutex. Blocking CUDA, persistence,
transfer, backend, and network operations are performed **outside** the global lock. Network I/O
is done only by the owning connection thread (and, for coordinator-to-worker WARM_TOPIC, under a
per-connection send mutex). A dedicated concurrency audit focuses on lock re-entrancy, lock-order
inversion, callbacks under locks, double budget release, stale completion, and us-after-lifecycle.

## Integrity guarantees

- `/W4 /WX` (MSVC) with zero warnings across the library, tests, examples, benchmarks, and CLI.
- Release and Debug build support.
- No test timeout mechanism anywhere.
- No OCR or screenshot-based validation.
