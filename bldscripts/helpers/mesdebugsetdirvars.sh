#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Sets MESON_BUILD_DIR and MESON_PREFIX_DIR
#
# Use by:
#   source helpers/mesdebugsetdirvars.sh

MY_HELPER_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PST_DIR_SUFFIX=".debug"
source $MY_HELPER_DIR/messetdirvarsfinish.sh
