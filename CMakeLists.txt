cmake_minimum_required(VERSION 3.20)
project(faset LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -O3
    -march=x86-64-v2
    -mtune=sandybridge
    -Wall
    -Wextra
    -Werror
    -fno-exceptions
    -fno-rtti
)

set(FASET_CORE_SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/camera.cpp")
    list(APPEND FASET_CORE_SOURCES src/camera.cpp)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/stl_loader.cpp")
    list(APPEND FASET_CORE_SOURCES src/stl_loader.cpp)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/culling.cpp")
    list(APPEND FASET_CORE_SOURCES src/culling.cpp)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/uri_dir.cpp")
    list(APPEND FASET_CORE_SOURCES src/uri_dir.cpp)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/matcap.cpp")
    list(APPEND FASET_CORE_SOURCES src/matcap.cpp)
endif()

if(FASET_CORE_SOURCES)
    add_library(faset_core STATIC ${FASET_CORE_SOURCES})
    target_include_directories(faset_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(faset_core PUBLIC pthread m)
endif()

set(FASET_TEST_SOURCES tests/test_main.cpp)
foreach(tf
    tests/test_math_camera.cpp
    tests/test_stl_loader.cpp
    tests/test_culling.cpp
    tests/test_xdnd_and_dir.cpp
    tests/test_matcap_font.cpp
    tests/test_perf_bench.cpp
)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${tf}")
        list(APPEND FASET_TEST_SOURCES "${tf}")
    endif()
endforeach()

add_executable(faset_tests ${FASET_TEST_SOURCES})
target_include_directories(faset_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/tests")
if(TARGET faset_core)
    target_link_libraries(faset_tests PRIVATE faset_core)
endif()
target_link_libraries(faset_tests PRIVATE pthread m)

enable_testing()
add_test(NAME faset_unit_tests COMMAND faset_tests)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    add_executable(faset
        src/main.cpp
        src/gl_loader.cpp
        src/renderer.cpp
        src/hud.cpp
        src/platform_x11.cpp
    )
    target_include_directories(faset PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(faset PRIVATE faset_core X11 dl pthread m)
endif()
