#!/bin/bash -ve

cat << EOF > /etc/apt/sources.list.d/llvm.list
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty main
# 7 
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-7 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-7 main
# 8 
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-8 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-8 main

# Also add the following for the appropriate libstdc++
# LLVM toolchain trusts ubuntu-toolchain-r/test as libstdc++ reference
# Trusty repository is broken as this open issue shows https://github.com/stan-dev/math/issues/604
deb http://ppa.launchpad.net/ubuntu-toolchain-r/test/ubuntu trusty main
EOF

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key| sudo apt-key add -

sudo apt-get update

if [[ "$COMPILER_NAME" = "clang" ]]; then
    export C_COMPILER_PACKAGE=clang-$COMPILER_VERSION
    export C_COMPILER_BIN=clang-$COMPILER_VERSION
    export CPP_COMPILER_PACKAGE=
    export CPP_COMPILER_BIN=clang++-$COMPILER_VERSION
    export COV_TOOL_BIN=llvm-cov-$COMPILER_VERSION
    if [ ! -z "$(sudo apt-cache search llvm-$COMPILER_VERSION-tools)" ]; then export COV_TOOL_PACKAGE=$COV_TOOL_PACKAGE llvm-$COMPILER_VERSION-tools; fi
    if [ ! -z "$(sudo apt-cache search llvm-$COMPILER_VERSION)" ]; then export COV_TOOL_PACKAGE=$COV_TOOL_PACKAGE llvm-$COMPILER_VERSION-tools; fi
    export COV_TOOL_ARGS=gcov
    export CPP_STDLIB_PACKAGE=libc++-$COMPILER_VERSION-dev
elif [[ "$COMPILER_NAME" = "gcc" ]]; then
    export C_COMPILER_PACKAGE=gcc-$COMPILER_VERSION
    export C_COMPILER_BIN=gcc-$COMPILER_VERSION
    export CPP_COMPILER_PACKAGE=g++-$COMPILER_VERSION
    export CPP_COMPILER_BIN=g++-$COMPILER_VERSION
    export COV_TOOL_BIN=gcov-$COMPILER_VERSION
    export COV_TOOL_PACKAGE=gcov-$COMPILER_VERSION
    export COV_TOOL_ARGS=
    export CPP_STDLIB_PACKAGE=
fi

export CC=$C_COMPILER_BIN
export CXX=$CPP_COMPILER_BIN
export PROJECT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"