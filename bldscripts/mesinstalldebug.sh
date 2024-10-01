#!/bin/bash

#
# SPDX-FileCopyrightText: 2024 Duncan Greatwood
#
# SPDX-License-Identifier: Apache-2.0
#
# Install debug pistache build

# Execute this script from the parent directory by invoking:
#   bldscripts/mesinstalldebug.sh

source bldscripts/mesdebugsetdirvars.sh
source bldscripts/adjbuilddirformesbuild.sh

# Installs to /usr/local

sudo meson install -C ${MESON_BUILD_DIR}
