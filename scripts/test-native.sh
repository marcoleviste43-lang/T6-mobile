#!/bin/sh
set -e
cd "$(dirname "$0")/../native"
F="-std=c++20 -Wall -Wextra -fsanitize=address,undefined"
g++ $F -o /tmp/iso_test tests/iso_test.cpp loader/iso9660.cpp
/tmp/iso_test
g++ $F -o /tmp/cpu_test tests/cpu_test.cpp cpu/mips.cpp loader/elf.cpp
/tmp/cpu_test
