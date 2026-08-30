# Warmth Fabric Examples

Each example is a self-contained executable in `examples/` that links `warmth`.

| Example | Demonstrates |
|---|---|
| `example_lifecycle` | cold -> warm -> invalidate -> rewarm on a single object |
| `example_readiness` | component dimensions, composite readiness, explanation + plan |
| `example_decay_invalidation` | deterministic decay (idle, dependency change, memory pressure) |
| `example_budget_schedule` | budget admission (device memory, concurrent ops) + priority-aware plan |
| `example_persistence` | snapshot save/recover, with live-dimension revalidation |
| `example_stale_authority` | real TCP coordinator/worker; stale-epoch report is rejected |

Build all examples and run them:

    cmake --build build --config Release --target example_lifecycle ...
    build/examples/example_lifecycle.exe

The CUDA proof is exposed as `build/cuda/warmth_cuda_demo.exe` and via
`warmth_cli cuda`. The atomic multiprocess restart proof is `warmth_cli multiprocess`.
