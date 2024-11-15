#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#

# Run the test suite for Meson force-libevent debug build

# Execute this script from the parent directory by invoking:
#   bldscripts/mestestflibevdebug.sh

MY_SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source $MY_SCRIPT_DIR/helpers/mesdebugflibevsetdirvars.sh
source $MY_SCRIPT_DIR/helpers/adjbuilddirformesbuild.sh

if [ -e "${MESON_BUILD_DIR}" ]
then
    echo "Using build dir ${MESON_BUILD_DIR}"
    meson test -C ${MESON_BUILD_DIR}
else
    echo "Build dir ${MESON_BUILD_DIR} doesn't exist"
    exit 1
fi
