#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Install debug pistache build

# Execute this script from the parent directory by invoking:
#   bldscripts/mesinstallflibevdebug.sh

MY_SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

source $MY_SCRIPT_DIR/helpers/mesdebugflibevsetdirvars.sh
source $MY_SCRIPT_DIR/helpers/adjbuilddirformesbuild.sh

# Installs to /usr/local

sudo meson install -C ${MESON_BUILD_DIR}
