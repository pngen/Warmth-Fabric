cmake_minimum_required(VERSION 3.25)
project(WarmthConsumer LANGUAGES CXX)
# Downstream find_package(WarmthFabric) consumer. Build against an installed
# WarmthFabric package (cmake --install <build> --prefix <prefix>) using
# -DCMAKE_PREFIX_PATH=<prefix>.
find_package(WarmthFabric QUIET)
if(WarmthFabric_FOUND)
    add_executable(warmth_consumer consumer_main.cpp)
    target_link_libraries(warmth_consumer PRIVATE warmth::warmth)
    target_compile_features(warmth_consumer PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(warmth_consumer PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHsc)
    else()
        target_compile_options(warmth_consumer PRIVATE -Wall -Wextra -Werror)
    endif()
else()
    message(STATUS "WarmthFabric package not installed - consumer target skipped (install then re-configure with -DCMAKE_PREFIX_PATH)")
endif()
