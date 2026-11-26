cmake_minimum_required(VERSION 3.20)

project(LucidSwitch2
    VERSION 0.5.0
    DESCRIPTION "Clean-room console emulation research scaffold"
    LANGUAGES CXX
)

option(LUCID_BUILD_TESTS "Build the LucidSwitch2 test suite" ON)

add_library(lucid_core
    src/core/memory_bus.cpp
    src/core/emulator.cpp
    src/cpu/a64_interpreter.cpp
    src/cpu/cpu_memory.cpp
    src/arch/system_registers.cpp
    src/mmu/address_space.cpp
    src/devices/interrupt_controller.cpp
    src/devices/virtual_timer.cpp
    src/devices/device_bus.cpp
)

target_include_directories(lucid_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(lucid_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(lucid_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(lucid_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(lucidswitch2 src/main.cpp)
target_link_libraries(lucidswitch2 PRIVATE lucid_core)

if(LUCID_BUILD_TESTS)
    enable_testing()

    add_executable(lucid_tests tests/test_main.cpp)
    target_link_libraries(lucid_tests PRIVATE lucid_core)
    target_compile_features(lucid_tests PRIVATE cxx_std_20)

    add_executable(lucid_a64_tests tests/test_a64.cpp)
    add_executable(lucid_architecture_tests tests/test_architecture.cpp)
    target_link_libraries(lucid_a64_tests PRIVATE lucid_core)
    target_link_libraries(lucid_architecture_tests PRIVATE lucid_core)
    target_compile_features(lucid_a64_tests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(lucid_tests PRIVATE /W4 /permissive-)
        target_compile_options(lucid_a64_tests PRIVATE /W4 /permissive-)
        target_compile_options(lucid_architecture_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(lucid_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        target_compile_options(lucid_a64_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        target_compile_options(lucid_architecture_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()

    add_test(NAME lucid_tests COMMAND lucid_tests)
    add_test(NAME lucid_a64_tests COMMAND lucid_a64_tests)
    add_test(NAME lucid_architecture_tests COMMAND lucid_architecture_tests)
endif()
