# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# This file downloads and build LLVM as part of building TVM on windows
# With Windows we are not able to use any pre-built LLVM so we build it
# locally and install it in the directory LLVM_CACHE_PATH
#
# LLVM_CACHE_PATH must be set to the location where LLVM is to be installed.
# If there is already an installed version in LLVM_CACHE_PATH then the build
# and install steps are skipped, so put LLVM_CACHE_PATH somewhere not withing
# the build directory. Once LLVM is built and cached it will not need to be
# built again unless the version of LLVM used by TVM changes. An error message
# is printed in that case and the LLVM_CACHE_PATH should be deleted.

cmake_minimum_required(VERSION 3.11)

include(FetchContent)

set(LLVM_GIT_REPOSITORY https://github.com/llvm/llvm-project.git)
set(LLVM_GIT_TAG llvmorg-11.0.0)

get_filename_component(LLVM_CACHE_PATH ${LLVM_CACHE_PATH}/${LLVM_GIT_TAG} ABSOLUTE)
message(STATUS "LLVM CACHE COMBINED PATH ${LLVM_CACHE_PATH}")

if (NOT EXISTS ${LLVM_CACHE_PATH})
  file(MAKE_DIRECTORY ${LLVM_CACHE_PATH})
endif()

# Before starting check if there is an acceptable LLVM available in the LLVM_CACHE_PATH
find_package(LLVM PATHS ${LLVM_CACHE_PATH} NO_DEFAULT_PATH)
if (${LLVM_FOUND})
  message(STATUS "LLVM found in cache")
  set(LLVM_CONFIG_EXE "${LLVM_DIR}/../../../bin/llvm-config${CMAKE_EXECUTABLE_SUFFIX}")
  if(EXISTS ${LLVM_CONFIG_EXE})
    set(USE_LLVM "${LLVM_CONFIG_EXE}")
  endif()
else()
  # LLVM not found in cache so download and configure
  FetchContent_Declare(
    llvm
    GIT_REPOSITORY  ${LLVM_GIT_REPOSITORY}
    GIT_TAG         ${LLVM_GIT_TAG}
  )

  FetchContent_GetProperties(llvm)
  if(NOT llvm_POPULATED)
    message(STATUS "LLVM not found in cache. Downloading will take several minutes so be patient.")
    FetchContent_Populate(llvm)
  endif()

  message(STATUS "Generating makefiles for LLVM")
  if(MSVC)
    execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}"
        -DCMAKE_INSTALL_PREFIX=${LLVM_CACHE_PATH}
        -A x64
        -Thost=x64
        -DCMAKE_CONFIGURATION_TYPES=Release
        -DCMAKE_BUILD_TYPE=Release
        -DLLVM_INCLUDE_EXAMPLES=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_BENCHMARKS=OFF
        -DLLVM_ENABLE_RTTI=ON
        -DLLVM_ENABLE_WARNINGS=OFF
        ${llvm_SOURCE_DIR}/llvm
      WORKING_DIRECTORY "${llvm_BINARY_DIR}"
      RESULT_VARIABLE EXEC_RESULT
    )
  else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}"
        -DCMAKE_INSTALL_PREFIX=${LLVM_CACHE_PATH}
        -DLLVM_INCLUDE_EXAMPLES=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_BENCHMARKS=OFF
        -DLLVM_ENABLE_RTTI=ON
        -DLLVM_ENABLE_WARNINGS=OFF
        ${llvm_SOURCE_DIR}/llvm
      WORKING_DIRECTORY "${llvm_BINARY_DIR}"
      RESULT_VARIABLE EXEC_RESULT
    )
  endif()
  if (EXEC_RESULT)
    message(FATAL_ERROR "Error generating project for LLVM")
  endif()

  message(STATUS "Building LLVM")
  execute_process(COMMAND "${CMAKE_COMMAND}" --build . --config Release --target install
    WORKING_DIRECTORY "${llvm_BINARY_DIR}"
    RESULT_VARIABLE EXEC_RESULT
  )
  if (EXEC_RESULT)
    message(FATAL_ERROR "Error generating project for LLVM")
  endif()

  set(USE_LLVM "${LLVM_CACHE_PATH}/bin/llvm-config${CMAKE_EXECUTABLE_SUFFIX}")
endif()
