#-------------------------------------------------------------------------------
# Copyright (c) 2023-2025, Arm Limited or its affiliates. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
#-------------------------------------------------------------------------------

# get testsuite directory list
set(TEST_DIR_PATH ${ROOT_DIR}/test)
if(SUITE STREQUAL "all")
    # Get all the test pool components
    _get_sub_dir_list(TEST_SUITE_LIST ${TEST_DIR_PATH})
else()
    set(TEST_SUITE_LIST ${SUITE})
endif()

set(TEST_INCLUDE ${CMAKE_CURRENT_BINARY_DIR})
list(APPEND TEST_INCLUDE
    ${ROOT_DIR}/val/host/inc/
    ${ROOT_DIR}/val/host/src/
    ${ROOT_DIR}/val/common/inc/
    ${ROOT_DIR}/val/common/src/
    ${ROOT_DIR}/plat/common/inc/
    ${ROOT_DIR}/plat/common/src/
    ${ROOT_DIR}/plat/targets/${TARGET}/inc/
    ${ROOT_DIR}/plat/targets/${TARGET}/src/
    ${ROOT_DIR}/plat/driver/inc/
    ${ROOT_DIR}/plat/driver/src/
    ${ROOT_DIR}/test/database/
    ${ROOT_DIR}/test/common/
    ${COMMON_VAL_PATH}/inc/
)

foreach(SUITE ${TEST_SUITE_LIST})
    list(APPEND TEST_INCLUDE ${ROOT_DIR}/test/${SUITE}/common/)
    if(NOT ${SUITE} STREQUAL "command")
        list(APPEND TEST_INCLUDE ${ROOT_DIR}/test/command/common/)
    endif()
endforeach()

if(${TEST_COMBINE})

set(TEST_LIB ${EXE_NAME}_test_lib)

# Compile all .c/.S files from test directory
if(${SUITE} STREQUAL "all")
file(GLOB TEST_SRC
    "${ROOT_DIR}/test/*/*/*_host.c"
    "${ROOT_DIR}/test/*/*/*_host.S"
    "${ROOT_DIR}/test/planes/*/*/*_host.c"
    "${ROOT_DIR}/test/*/common/*_host.c"
    "${ROOT_DIR}/test/common/*_host.c"
)
else()
file(GLOB TEST_SRC
    "${ROOT_DIR}/test/${SUITE}/*/*_host.c"
    "${ROOT_DIR}/test/${SUITE}/*/*_host.S"
    "${ROOT_DIR}/test/${SUITE}/*/*/*_host.c"
    "${ROOT_DIR}/test/${SUITE}/common/*_host.c"
    "${ROOT_DIR}/test/common/*_host.c"
)
endif()

#Adding command test source files for SUITE_COVERAGE
if(NOT ${SUITE} STREQUAL "command")
    file(GLOB COMMAND_TEST_SRC
    "${ROOT_DIR}/test/command/*/*_host.c"
    "${ROOT_DIR}/test/command/*/*_host.S"
    "${ROOT_DIR}/test/command/*/*/*_host.c"
    "${ROOT_DIR}/test/command/common/*_host.c"
)
endif()

list(APPEND TEST_SRC ${COMMAND_TEST_SRC})
list(APPEND TEST_SRC
    ${ROOT_DIR}/test/database/test_database_host.c
)

# Create TEST library
add_library(${TEST_LIB} STATIC ${TEST_SRC})

target_link_libraries(${TEST_LIB} PUBLIC ${XLAT-LIB})

#Create compile list files
list(APPEND COMPILE_LIST ${TEST_SRC})
set(COMPILE_LIST ${COMPILE_LIST} PARENT_SCOPE)

target_include_directories(${TEST_LIB} PRIVATE ${TEST_INCLUDE})

create_executable(${EXE_NAME} ${BUILD}/output/ "")
unset(TEST_SRC)

else()

# test source directory
set(TEST_SOURCE_DIR ${ROOT_DIR}/test)
if(SUITE STREQUAL "all")
    # Get all the test pool components
    _get_sub_dir_list(SUITE_LIST ${TEST_SOURCE_DIR})
else()
    set(SUITE_LIST ${SUITE})
endif()


# Generate per test ELFs
foreach(SUITE ${SUITE_LIST})
    #message(STATUS "[ACS] : Compiling sources from ${SUITE} suite")
    # Get all the test folders from a given test component
    if(${SUITE} STREQUAL "planes")
        _get_sub_dir_list(TEST_LIST ${TEST_SOURCE_DIR}/${SUITE}/*/)
    else()
        _get_sub_dir_list(TEST_LIST ${TEST_SOURCE_DIR}/${SUITE})
    endif()
    foreach(TEST ${TEST_LIST})
        #message(STATUS "[ACS] : Compiling sources from ${TEST} Test")
        set(TEST_SRC "${TEST_SOURCE_DIR}/${SUITE}/${TEST}/${TEST}_host.c")
        if(${SUITE} STREQUAL "planes")
            list(APPEND TEST_SRC ${TEST_SOURCE_DIR}/${SUITE}/*/${TEST}/${TEST}_host.c)
        endif()
        list(APPEND TEST_SRC ${TEST_SOURCE_DIR}/database/test_database_host.c)
        file(GLOB TEST_COMMON_FILE ${TEST_SOURCE_DIR}/common/*_host.c)
        foreach(COMMON_FILE1 ${TEST_COMMON_FILE})
            list(APPEND TEST_SRC ${COMMON_FILE1})
        endforeach()
        file(GLOB SUITE_COMMON_FILE ${TEST_SOURCE_DIR}/${SUITE}/common/*_host.c)
        foreach(COMMON_FILE2 ${SUITE_COMMON_FILE})
            list(APPEND TEST_SRC ${COMMON_FILE2})
        endforeach()
        #Create compile list files
        list(APPEND COMPILE_LIST ${TEST_SRC})
        set(COMPILE_LIST ${COMPILE_LIST} PARENT_SCOPE)
        set(TEST_LIB ${EXE_NAME}_${TEST}_test_lib)
        add_library(${TEST_LIB} STATIC ${TEST_SRC})
        target_compile_definitions(${TEST_LIB} PRIVATE d_${TEST})
        target_include_directories(${TEST_LIB} PRIVATE ${TEST_INCLUDE})
        target_link_libraries(${TEST_LIB} PUBLIC ${XLAT-LIB})
        file(MAKE_DIRECTORY ${BUILD}/output/${SUITE}/${TEST})
        create_executable(${EXE_NAME} ${BUILD}/output/${SUITE}/${TEST} ${TEST})
        unset(TEST_SRC)
    endforeach()
endforeach()
endif()
