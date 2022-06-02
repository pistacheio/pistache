#!/bin/sh

# SPDX-FileCopyrightText: 2019 Kip Warner
#
# SPDX-License-Identifier: Apache-2.0

# Bail on any errors...
set -e

# Enter autopkgtest's temp directory, cleaned up automatically
cd "$AUTOPKGTEST_TMP"
cat <<EOF > pistache_test.cpp
#include <pistache/endpoint.h>

int main()
{
    Pistache::Http::Endpoint endpoint(Pistache::Address("*:9080"));
    return 0;
}

EOF

c++ -o pistache_test pistache_test.cpp -std=c++17 -Wall -Werror $(pkg-config --cflags --libs libpistache)
echo "build: OK"
[ -x pistache_test ]
./pistache_test
echo "run: OK"
