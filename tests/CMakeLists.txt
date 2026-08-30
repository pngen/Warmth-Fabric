set(WARMTH_TEST_SOURCES
    test_core.cpp
    test_persistence.cpp
    test_protocol_loopback.cpp
    test_concurrency.cpp
    test_property.cpp
    test_cuda.cpp
)
foreach(src ${WARMTH_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE warmth)
    if(WIN32)
        target_link_libraries(${name} PRIVATE ws2_32)
    endif()
    target_compile_features(${name} PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /EHsc)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    endif()
    if(WIN32)
        target_compile_definitions(${name} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    # No TIMEOUT properties anywhere: tests run to natural pass/fail/crash.
    add_test(NAME ${name} COMMAND ${name})
endforeach()
