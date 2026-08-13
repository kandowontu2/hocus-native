if(NOT DEFINED NATIVE_EXE OR NOT DEFINED HOCUS_EXE OR
   NOT DEFINED TEST_DIRECTORY)
    message(FATAL_ERROR "NATIVE_EXE, HOCUS_EXE, and TEST_DIRECTORY are required")
endif()

file(MAKE_DIRECTORY "${TEST_DIRECTORY}")
set(TEST_NATIVE "${TEST_DIRECTORY}/hocus_native.exe")
set(TEST_HOCUS "${TEST_DIRECTORY}/HOCUS.EXE")
file(REMOVE "${TEST_NATIVE}" "${TEST_HOCUS}")
file(COPY_FILE "${NATIVE_EXE}" "${TEST_NATIVE}")

execute_process(
    COMMAND "${TEST_NATIVE}" --verify-embedded-assets
    RESULT_VARIABLE MISSING_RESULT
)
if(MISSING_RESULT EQUAL 0)
    message(FATAL_ERROR "Native runtime accepted a missing HOCUS.EXE")
endif()

file(WRITE "${TEST_HOCUS}" "MZ-not-the-registered-game")
execute_process(
    COMMAND "${TEST_NATIVE}" --verify-embedded-assets
    RESULT_VARIABLE INVALID_RESULT
)
if(INVALID_RESULT EQUAL 0)
    message(FATAL_ERROR "Native runtime accepted an unsupported HOCUS.EXE")
endif()

file(COPY_FILE "${HOCUS_EXE}" "${TEST_HOCUS}")
execute_process(
    COMMAND "${TEST_NATIVE}" --verify-embedded-assets
    RESULT_VARIABLE REGISTERED_RESULT
)
if(NOT REGISTERED_RESULT EQUAL 0)
    message(FATAL_ERROR "Native runtime rejected registered-v1.1 HOCUS.EXE")
endif()
