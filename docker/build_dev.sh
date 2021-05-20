#!/bin/bash
set -e
set -u

cd $TVM_HOME
mkdir -p build
cd build
cmake .. -DUSE_LLVM=llvm-config-11 -DUSE_GRAPH_EXECUTOR=ON -DUSE_BLAS=openblas
echo "making with -j$(grep -c ^processor /proc/cpuinfo)"
make -j$(grep -c ^processor /proc/cpuinfo)
