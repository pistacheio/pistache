#!/bin/sh

# SPDX-FileCopyrightText: 2019 Kip Warner
#
# SPDX-License-Identifier: Apache-2.0

# Bail on any errors...
set -e

# Create a working directory...
BUILD_DIR=$(mktemp -d)

# On exit or any other abnormality cleanup our build artifacts...
trap "rm -rf $BUILD_DIR" 0 INT QUIT ABRT PIPE TERM

# Enter our build directory...
cd $BUILD_DIR
cat <<EOF > pistache_test.cpp
#include <pistache/endpoint.h>

int main()
{
    Pistache::Http::Endpoint endpoint(Pistache::Address("*:9080"));
    return 0;
}

EOF

g++ -o pistache_test pistache_test.cpp -std=c++17 -Wall -Werror `pkg-config --cflags --libs libpistache`
echo "build: OK"
[ -x pistache_test ]
./pistache_test
echo "run: OK"

