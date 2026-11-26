cmake_minimum_required(VERSION 3.20)

project(LucidSwitch2 VERSION 0.16.0
    DESCRIPTION "Clean-room console emulation research scaffold"
    LANGUAGES CXX
)

option(LUCID_BUILD_TESTS "Build the LucidSwitch2 test suite" ON)
option(LUCID_ENABLE_VULKAN "Enable the optional Vulkan backend when the SDK is available" ON)
option(LUCID_ENABLE_SDL2 "Enable the optional SDL2 desktop presenter when available" ON)
option(LUCID_REQUIRE_NATIVE_VULKAN "Fail configuration unless Vulkan and glslc can build the native renderer" OFF)

if(LUCID_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
endif()
if(LUCID_ENABLE_SDL2)
    find_package(SDL2 QUIET)
endif()

set(LUCID_HAS_VULKAN_VALUE 0)
set(LUCID_HAS_VULKAN_PIPELINE_VALUE 0)

add_library(lucid_core
    src/core/memory_bus.cpp
    src/core/emulator.cpp
    src/kernel/runtime.cpp
    src/ipc/cmif.cpp
    src/cpu/a64_interpreter.cpp
    src/cpu/cpu_memory.cpp
    src/arch/system_registers.cpp
    src/mmu/address_space.cpp
    src/mmu/stage1_mmu.cpp
    src/devices/interrupt_controller.cpp
    src/devices/virtual_timer.cpp
    src/devices/device_bus.cpp
    src/gpu/guest_memory.cpp
    src/gpu/command_processor.cpp
    src/gpu/software_backend.cpp
    src/gpu/vulkan_backend.cpp
    src/gpu/window_presenter.cpp
    src/homebrew/package.cpp
    src/switch/executable.cpp
    src/switch/compatibility.cpp
    src/switch/homebrew_loader.cpp
    src/switch/nsp_package.cpp
)

target_include_directories(lucid_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(lucid_core PUBLIC cxx_std_20)

if(LUCID_ENABLE_VULKAN AND Vulkan_FOUND)
    set(LUCID_HAS_VULKAN_VALUE 1)
    target_link_libraries(lucid_core PRIVATE Vulkan::Vulkan)
    find_program(LUCID_GLSLC
        NAMES glslc
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/Bin32")
    if(LUCID_GLSLC)
        set(LUCID_HAS_VULKAN_PIPELINE_VALUE 1)
        set(LUCID_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        file(MAKE_DIRECTORY "${LUCID_SHADER_OUTPUT_DIR}")
        add_custom_command(
            OUTPUT "${LUCID_SHADER_OUTPUT_DIR}/lucid.vert.spv"
            COMMAND "${LUCID_GLSLC}" --target-env=vulkan1.0
                    -fshader-stage=vert
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lucid.vert"
                    -o "${LUCID_SHADER_OUTPUT_DIR}/lucid.vert.spv"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lucid.vert"
            VERBATIM
        )
        add_custom_command(
            OUTPUT "${LUCID_SHADER_OUTPUT_DIR}/lucid.frag.spv"
            COMMAND "${LUCID_GLSLC}" --target-env=vulkan1.0
                    -fshader-stage=frag
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lucid.frag"
                    -o "${LUCID_SHADER_OUTPUT_DIR}/lucid.frag.spv"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lucid.frag"
            VERBATIM
        )
        add_custom_target(lucid_shaders ALL
            DEPENDS
                "${LUCID_SHADER_OUTPUT_DIR}/lucid.vert.spv"
                "${LUCID_SHADER_OUTPUT_DIR}/lucid.frag.spv")
        add_dependencies(lucid_core lucid_shaders)
        target_compile_definitions(lucid_core PRIVATE
            LUCID_SHADER_DIR=\"${LUCID_SHADER_OUTPUT_DIR}\")
        message(STATUS "LucidSwitch2: native Vulkan shader pipeline enabled with ${LUCID_GLSLC}")
    else()
        message(WARNING "LucidSwitch2: Vulkan SDK found, but glslc was not found; native pipeline disabled")
    endif()
else()
    message(STATUS "LucidSwitch2: Vulkan SDK not found or disabled; software renderer remains available")
endif()

if(LUCID_REQUIRE_NATIVE_VULKAN AND
   (NOT LUCID_HAS_VULKAN_VALUE OR NOT LUCID_HAS_VULKAN_PIPELINE_VALUE))
    message(FATAL_ERROR "LUCID_REQUIRE_NATIVE_VULKAN is ON but Vulkan SDK plus glslc were not found.")
endif()

target_compile_definitions(lucid_core PRIVATE
    LUCID_HAS_VULKAN=${LUCID_HAS_VULKAN_VALUE}
    LUCID_HAS_VULKAN_PIPELINE=${LUCID_HAS_VULKAN_PIPELINE_VALUE})

if(LUCID_ENABLE_SDL2 AND SDL2_FOUND)
    if(TARGET SDL2::SDL2)
        target_link_libraries(lucid_core PRIVATE SDL2::SDL2)
    elseif(TARGET SDL2::SDL2-static)
        target_link_libraries(lucid_core PRIVATE SDL2::SDL2-static)
    else()
        target_include_directories(lucid_core PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(lucid_core PRIVATE ${SDL2_LIBRARIES})
    endif()
    target_compile_definitions(lucid_core PRIVATE LUCID_HAS_SDL2=1)
    message(STATUS "LucidSwitch2: SDL2 desktop presenter enabled")
else()
    target_compile_definitions(lucid_core PRIVATE LUCID_HAS_SDL2=0)
    message(STATUS "LucidSwitch2: SDL2 not found; desktop presentation unavailable")
endif()

if(MSVC)
    target_compile_options(lucid_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(lucid_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(lucidswitch2 src/main.cpp)
target_link_libraries(lucidswitch2 PRIVATE lucid_core)

if(LUCID_BUILD_TESTS)
    enable_testing()
    set(LUCID_TEST_TARGETS
        lucid_tests
        lucid_a64_tests
        lucid_architecture_tests
        lucid_exception_tests
        lucid_gpu_tests
        lucid_homebrew_tests
        lucid_switch_format_tests
        lucid_compatibility_tests
        lucid_nro_loader_tests
        lucid_nsp_package_tests
        lucid_kernel_runtime_tests
        lucid_ipc_tests
    )

    add_executable(lucid_tests tests/test_main.cpp)
    add_executable(lucid_a64_tests tests/test_a64.cpp)
    add_executable(lucid_architecture_tests tests/test_architecture.cpp)
    add_executable(lucid_exception_tests tests/test_exceptions.cpp)
    add_executable(lucid_gpu_tests tests/test_gpu.cpp)
    add_executable(lucid_homebrew_tests tests/test_homebrew.cpp)
    add_executable(lucid_switch_format_tests tests/test_switch_formats.cpp)
    add_executable(lucid_compatibility_tests tests/test_compatibility.cpp)
    add_executable(lucid_nro_loader_tests tests/test_nro_loader.cpp)
    add_executable(lucid_nsp_package_tests tests/test_nsp_package.cpp)
    add_executable(lucid_kernel_runtime_tests tests/test_kernel_runtime.cpp)
    add_executable(lucid_ipc_tests tests/test_ipc.cpp)

    foreach(test_target IN LISTS LUCID_TEST_TARGETS)
        target_link_libraries(${test_target} PRIVATE lucid_core)
        target_compile_features(${test_target} PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${test_target} PRIVATE
                -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        endif()
    endforeach()

    add_test(NAME lucid_tests COMMAND lucid_tests)
    add_test(NAME lucid_a64_tests COMMAND lucid_a64_tests)
    add_test(NAME lucid_architecture_tests COMMAND lucid_architecture_tests)
    add_test(NAME lucid_exception_tests COMMAND lucid_exception_tests)
    add_test(NAME lucid_gpu_tests COMMAND lucid_gpu_tests)
    add_test(NAME lucid_homebrew_tests COMMAND lucid_homebrew_tests)
    add_test(NAME lucid_switch_format_tests COMMAND lucid_switch_format_tests)
    add_test(NAME lucid_compatibility_tests COMMAND lucid_compatibility_tests)
    add_test(NAME lucid_nro_loader_tests COMMAND lucid_nro_loader_tests)
    add_test(NAME lucid_nsp_package_tests COMMAND lucid_nsp_package_tests)
    add_test(NAME lucid_kernel_runtime_tests COMMAND lucid_kernel_runtime_tests)
    add_test(NAME lucid_ipc_tests COMMAND lucid_ipc_tests)
endif()
