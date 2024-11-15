#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Execute this script from the parent directory by invoking:
#   bldscripts/clean.sh

MY_SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source $MY_SCRIPT_DIR/helpers/messetdirvars.sh
source $MY_SCRIPT_DIR/helpers/adjbuilddirformesbuild.sh

if [ -e "${MESON_BUILD_DIR}" ]
then
    rm -rf ${MESON_BUILD_DIR}
fi

source $MY_SCRIPT_DIR/helpers/mesdebugsetdirvars.sh
source $MY_SCRIPT_DIR/helpers/adjbuilddirformesbuild.sh
if [ -e "${MESON_BUILD_DIR}" ]
then
    rm -rf ${MESON_BUILD_DIR}
fi

source $MY_SCRIPT_DIR/helpers/cmksetdirvars.sh
if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    rm -rf ${CMAKE_BUILD_DIR}
fi

source $MY_SCRIPT_DIR/helpers/cmkdebugsetdirvars.sh
if [ -e "./${CMAKE_BUILD_DIR}" ]
then
    rm -rf ${CMAKE_BUILD_DIR}
fi
