cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(HocusNative VERSION 1.1.1 LANGUAGES CXX RC)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hocus_core
    src/audio.cpp
    src/campaign.cpp
    src/dat_archive.cpp
    src/demo.cpp
    src/level.cpp
    src/native_settings.cpp
    src/pcx.cpp
    src/registered_executable.cpp
    src/save.cpp
    src/sprite.cpp
    src/ui.cpp
)
target_compile_definitions(hocus_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(hocus_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(hocus_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(HOCUS_DAT_RESOURCE_PATH "${CMAKE_SOURCE_DIR}/hocus.dat")
configure_file(
    src/hocus_resources.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/hocus_resources.rc"
    @ONLY
)

add_executable(hocus_native WIN32
    src/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/hocus_resources.rc"
)
add_custom_command(TARGET hocus_native POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/hocus.exe"
        "$<TARGET_FILE_DIR:hocus_native>/HOCUS.EXE"
    COMMENT "Copying the required registered-v1.1 HOCUS.EXE beside the native build"
)
target_compile_definitions(hocus_native PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(hocus_native PRIVATE hocus_core gdi32 winmm dsound)
if(MSVC)
    target_compile_options(hocus_native PRIVATE /W4 /permissive-)
else()
    target_compile_options(hocus_native PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MINGW)
    target_link_options(hocus_native PRIVATE
        -municode -static -static-libgcc -static-libstdc++)
endif()

install(TARGETS hocus_native RUNTIME DESTINATION .)
install(FILES README.md LICENSE.md CREDITS.md DESTINATION .)
install(DIRECTORY docs DESTINATION . FILES_MATCHING PATTERN "*.md")

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME "HocusNative")
set(CPACK_PACKAGE_FILE_NAME "HocusNative-${PROJECT_VERSION}-windows-x64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)

include(CTest)
if(BUILD_TESTING)
    add_executable(hocus_asset_tests tests/asset_tests.cpp)
    target_include_directories(hocus_asset_tests PRIVATE src)
    target_link_libraries(hocus_asset_tests PRIVATE hocus_core)
    add_test(
        NAME registered_v1_1_asset_smoke
        COMMAND hocus_asset_tests
            "${CMAKE_SOURCE_DIR}/hocus.dat"
            "${CMAKE_SOURCE_DIR}/hocus.exe"
    )
    add_test(
        NAME registered_v1_1_standalone_runtime
        COMMAND hocus_native --verify-embedded-assets
    )
    add_test(
        NAME required_registered_v1_1_executable
        COMMAND "${CMAKE_COMMAND}"
            -DNATIVE_EXE=$<TARGET_FILE:hocus_native>
            -DHOCUS_EXE=${CMAKE_SOURCE_DIR}/hocus.exe
            -DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/required-exe-test
            -P ${CMAKE_SOURCE_DIR}/tests/required_executable_tests.cmake
    )
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME self_contained_runtime_imports
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/standalone_tests.py"
                "$<TARGET_FILE:hocus_native>"
        )
        add_test(
            NAME registered_v1_1_disassembly_inventory
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/disassembly_tests.py"
                "${CMAKE_SOURCE_DIR}/tools/disassemble_mz.py"
                "${CMAKE_SOURCE_DIR}/hocus.exe"
        )
        add_test(
            NAME registered_v1_1_extraction_pipeline
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/extraction_tests.py"
                "${CMAKE_SOURCE_DIR}/tools/extract_assets.py"
                "${CMAKE_SOURCE_DIR}/hocus.exe"
                "${CMAKE_SOURCE_DIR}/hocus.dat"
                "${CMAKE_SOURCE_DIR}/manifests/hocus_registered_v1.1.csv"
        )
        add_test(
            NAME registered_v1_1_executable_tables
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/executable_table_tests.py"
                "${CMAKE_SOURCE_DIR}/tools/extract_executable_tables.py"
                "${CMAKE_SOURCE_DIR}/hocus.exe"
        )
        add_test(
            NAME registered_v1_1_parity_ledger
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/parity_ledger_tests.py"
                "${CMAKE_SOURCE_DIR}/analysis/hocus_functions.json"
                "${CMAKE_SOURCE_DIR}/docs/PARITY_AUDIT.md"
        )
    endif()
endif()
