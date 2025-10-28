# (C) Copyright 2025, SECO Mind Srl
#
# SPDX-License-Identifier: Apache-2.0

function(concat_if_exists FILE_PATH STRING_NAME)
    # Check if the file exists
    file(GLOB FILE_TO_ADD ${FILE_PATH})

    # If the file exists, add it to the list
    if(EXISTS ${FILE_TO_ADD})
        set(${STRING_NAME} "${${STRING_NAME}};${FILE_PATH}" PARENT_SCOPE)
    endif()
endfunction()

cmake_minimum_required(VERSION 3.20.0)

# add the sample specific private configuration
concat_if_exists(${CMAKE_SOURCE_DIR}/private.conf EXTRA_CONF_FILE)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cpu_metrics_app)

test_sysbuild()

# create source files list
FILE(GLOB app_common_sources ${CMAKE_CURRENT_LIST_DIR}/src/*.c)
if(NOT CONFIG_WIFI)
    LIST(REMOVE_ITEM app_common_sources ${CMAKE_CURRENT_LIST_DIR}/src/wifi.c)
endif()
target_sources(app PRIVATE ${app_common_sources})
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# add generated sources and includes for the interfaces
FILE(GLOB app_interfaces_sources ${CMAKE_CURRENT_LIST_DIR}/interfaces/*.c)
target_sources(app PRIVATE ${app_interfaces_sources})
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/interfaces)
