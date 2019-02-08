#!/usr/bin/env bash

apt-get update
apt-get install -y software-properties-common wget

add-apt-repository -y ppa:ubuntu-toolchain-r/test

cat << EOF >> /etc/apt/sources.list.d/llvm-toolchain.list
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty main
# 3.4
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.4 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.4 main
# 3.5
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.5 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.5 main
# 3.6
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.6 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.6 main
# 3.7
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.7 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.7 main
# 3.8
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.8 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.8 main
# 3.9
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.9 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-3.9 main
# 5 
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-5.0 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-5.0 main
# 6 
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-6.0 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-6.0 main
# 7 
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-7 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-7 main
# 8 
deb http://apt.llvm.org/trusty/ llvm-toolchain-trusty-8 main
deb-src http://apt.llvm.org/trusty/ llvm-toolchain-trusty-8 main
# Also add the following for the appropriate libstdc++
# deb http://ppa.launchpad.net/ubuntu-toolchain-r/test/ubuntu trusty main
EOF

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -

apt-get update

apt-get install -y lcov valgrind cmake-data cmake libyajl-dev libxml2-dev libxqilla-dev libssl-dev libcurl4-openssl-dev libc++-dev libgtest-dev
#apt-get install g++-4.6 g++-4.7 g++-4.8 g++-4.9 g++-5 g++-6 g++-7 g++-8 clang-3.3 clang-3.4 clang-3.5 clang-3.6 clang-3.7 clang-3.8 clang-3.9 clang-4.0 clang-5.0 clang-6.0 clang-7 clang-8