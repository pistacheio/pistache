#!/bin/bash -ve

cd $PROJECT_DIR

sudo apt-get update
sudo apt-get install -y coreutils apparmor-profiles libssl-dev libcurl4-openssl-dev gdb valgrind lcov python-pip python3-pip git $C_COMPILER_PACKAGE $CPP_COMPILER_PACKAGE $CPP_STDLIB_PACKAGE

sudo python -m pip install --upgrade pip
sudo python3 -m pip install --upgrade pip

sudo pip3 install cmake

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
cmake -B$PROJECT_DIR/build/debug \
    -DCMAKE_BUILD_TYPE=debug \
    -DPISTACHE_BUILD_EXAMPLES=true \
    -DPISTACHE_BUILD_TESTS=true \
    -DPISTACHE_SSL=true \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX .

# Release build
cmake -B$PROJECT_DIR/build/release \
    -DCMAKE_BUILD_TYPE=Release \
    -DPISTACHE_SSL=true \
    -DCMAKE_C_COMPILER=$CC \
    -DCMAKE_CXX_COMPILER=$CXX .