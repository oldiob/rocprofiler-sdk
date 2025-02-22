#
# Integration tests
#
cmake_minimum_required(VERSION 3.21.0 FATAL_ERROR)

if(COMMAND rocprofiler_deactivate_clang_tidy)
    rocprofiler_deactivate_clang_tidy()
endif()

project(rocprofiler-tests LANGUAGES C CXX)

set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "tests")
set(ROCPROFILER_SDK_TESTS_SOURCE_DIR "${PROJECT_SOURCE_DIR}")
set(ROCPROFILER_SDK_TESTS_BINARY_DIR "${PROJECT_BINARY_DIR}")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE
        "Release"
        CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_HIP_STANDARD 17)
set(CMAKE_HIP_EXTENSIONS OFF)
set(CMAKE_HIP_STANDARD_REQUIRED ON)

enable_testing()
include(CTest)

include(GNUInstallDirs)
# always use lib instead of lib64
set(CMAKE_INSTALL_LIBDIR "lib")

# needed for validation
find_package(Python3 REQUIRED)

# configure python module <BINARY_DIR>/rocprofiler_sdk/pytest_utils
add_subdirectory(pytest-packages)

# common utilities
add_subdirectory(common)

# tool libraries used for data collection during integration tests
add_subdirectory(tools)

# libraries used by integration test applications
add_subdirectory(lib)

# applications used by integration tests
add_subdirectory(bin)

# validation tests
add_subdirectory(kernel-tracing)
add_subdirectory(async-copy-tracing)
add_subdirectory(scratch-memory-tracing)
add_subdirectory(c-tool)
add_subdirectory(page-migration)
add_subdirectory(pc_sampling)
add_subdirectory(thread-trace)
add_subdirectory(hip-graph-tracing)

# rocprofv3 validation tests
add_subdirectory(rocprofv3)
add_subdirectory(counter-collection)
