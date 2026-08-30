# Warmth Fabric Benchmarks

`benchmarks/warmth_bench` measures completed operations for:

- Warmth object creation (`WarmthFabric::register_object`)
- readiness queries
- composite readiness / explanation generation
- warming-plan generation
- decay processing
- invalidation
- snapshot generation / persistence encode & decode
- 1-, 4-, and 8-thread mutation
- large object pools

Run it:

    cmake --build build --config Release --target warmth_bench
    build/benchmarks/warmth_bench.exe

The real CUDA cold and warm start timings are produced by `cuda/warmth_cuda_demo` and are
reported in milliseconds. Reported values are wall-clock measurements from steady clocks.
