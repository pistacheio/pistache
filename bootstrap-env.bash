#!/bin/bash -ve

apt-get update

if [[ "$COMPILER_NAME" = "clang" ]]; then
    export C_COMPILER_PACKAGE=clang-$COMPILER_VERSION
    export C_COMPILER_BIN=clang-$COMPILER_VERSION
    export CPP_COMPILER_PACKAGE=
    export CPP_COMPILER_BIN=clang++-$COMPILER_VERSION
    export COV_TOOL_BIN=llvm-cov-$COMPILER_VERSION
    if [ ! -z "$(apt-cache search llvm-$COMPILER_VERSION-tools)" ]; then export COV_TOOL_PACKAGE=$COV_TOOL_PACKAGE llvm-$COMPILER_VERSION-tools; fi
    if [ ! -z "$(apt-cache search llvm-$COMPILER_VERSION)" ]; then export COV_TOOL_PACKAGE=$COV_TOOL_PACKAGE llvm-$COMPILER_VERSION-tools; fi
    export COV_TOOL_ARGS=gcov
    export CPP_STDLIB_PACKAGE=libc++-dev
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
export DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"