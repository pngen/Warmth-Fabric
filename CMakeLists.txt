cmake_minimum_required(VERSION 3.25)

project(WarmthFabric
        VERSION 1.0.0
        DESCRIPTION "Warmth Fabric - how ready is a runtime to execute useful work right now"
        LANGUAGES CXX)

option(WARMTH_BUILD_TESTS      "Build Warmth Fabric tests"        ON)
option(WARMTH_BUILD_EXAMPLES   "Build Warmth Fabric examples"     ON)
option(WARMTH_BUILD_BENCHMARKS "Build Warmth Fabric benchmarks"   ON)
option(WARMTH_BUILD_CONSUMER   "Build downstream find_package consumer" ON)
option(WARMTH_BUILD_CUDA       "Build CUDA validation module"     ON)

if(WARMTH_BUILD_CUDA)
    find_package(CUDAToolkit QUIET)
    if(NOT CUDAToolkit_FOUND)
        message(STATUS "CUDA toolkit not found - building without CUDA module")
        set(WARMTH_BUILD_CUDA OFF)
    endif()
endif()

include(GNUInstallDirs)
include(CTest)

set(WARMTH_PUBLIC_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(warmth STATIC
    src/identity.cpp
    src/persistence.cpp
    src/transport.cpp
    src/coordinator.cpp
    src/worker.cpp
    src/fabric.cpp
)

target_include_directories(warmth PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(warmth PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(warmth PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHsc)
else()
    target_compile_options(warmth PRIVATE -Wall -Wextra -Werror)
endif()

if(WIN32)
    target_link_libraries(warmth PRIVATE ws2_32)
endif()

install(TARGETS warmth EXPORT WarmthFabricTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/warmth DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT WarmthFabricTargets
        FILE WarmthFabricTargets.cmake
        NAMESPACE warmth::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WarmthFabric)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/WarmthFabricConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WarmthFabricConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/WarmthFabricConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WarmthFabric)
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/WarmthFabricConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/WarmthFabricConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/WarmthFabric)

if(WARMTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
if(WARMTH_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
if(WARMTH_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
if(WARMTH_BUILD_CONSUMER)
    add_subdirectory(consumer)
endif()
if(WARMTH_BUILD_CUDA)
    add_subdirectory(cuda)
endif()
add_subdirectory(tools)

configure_file(cmake/warmth_version.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/warmth_version.h" @ONLY)
