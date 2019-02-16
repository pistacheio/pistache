#!/bin/bash -ve

cd $DIR

apt-get update

apt-get install -y coreutils apparmor-profiles libssl-dev libcurl4-openssl-dev gdb valgrind lcov python-pip python3-pip git $C_COMPILER_PACKAGE $CPP_COMPILER_PACKAGE $CPP_STDLIB_PACKAGE

python -m pip install --upgrade pip
python3 -m pip install --upgrade pip

pip3 install cmake

git submodule update --init --recursive

# Enable core dumps
ulimit -c
ulimit -a -S
ulimit -a -H

# Print debug system information
cat /proc/sys/kernel/core_pattern
cat /etc/default/apport || true
service --status-all || true
initctl list || true

# Debug build
cmake -B$DIR/build/debug \
    -DCMAKE_BUILD_TYPE=debug \
    -DPISTACHE_BUILD_EXAMPLES=true \
    -DPISTACHE_BUILD_TESTS=true \
    -DPISTACHE_SSL=true \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX

# Release build
cmake -B$DIR/build/release \
    -DCMAKE_BUILD_TYPE=Release \
    -DPISTACHE_SSL=true \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX