#!/bin/sh
# Compile et exécute les tests hôte de espac_airton.h (clang++ du Mac ou g++).
set -eu
cd "$(dirname "$0")"
CXX="${CXX:-clang++}"
mkdir -p build
"$CXX" -std=c++17 -Wall -Wextra -Werror -O1 -o build/test_airton test_airton.cpp
./build/test_airton
