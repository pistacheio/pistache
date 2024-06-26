#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   bldscripts/cmkbuild.sh

# To install (must be run from build directory):
#   sudo make install

source bldscripts/cmksetdirvars.sh

if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    cd "${CMAKE_BUILD_DIR}"
else
    mkdir "${CMAKE_BUILD_DIR}"
    cd "${CMAKE_BUILD_DIR}"
fi

if [ "$(uname)" == "Darwin" ]; then
    # on macOS, rapidjson has been installed via brew
   cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
else
    if [ -e "../rapidjson/build" ]
    then
        cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DRapidJSON_DIR=../rapidjson/build/ ..
    else
        cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
    fi
fi

make
