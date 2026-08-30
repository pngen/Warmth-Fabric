enable_language(CUDA)
add_executable(warmth_cuda_demo warmth_cuda.cu)
set_target_properties(warmth_cuda_demo PROPERTIES
    CUDA_ARCHITECTURES "120"
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON)
if(MSVC)
    # Forward host-compiler warnings to cl WITHOUT letting nvcc misinterpret
    # them as nvcc input options.
    set_target_properties(warmth_cuda_demo PROPERTIES
        CUDA_HOST_COMPILATION_OPTIONS "/W4")
endif()
if(WIN32)
    target_link_libraries(warmth_cuda_demo PRIVATE cudart ws2_32)
endif()
