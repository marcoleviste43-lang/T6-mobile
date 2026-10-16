#!/bin/sh
set -e
cd "$(dirname "$0")/../native"
g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -o /tmp/iso_test tests/iso_test.cpp loader/iso9660.cpp
/tmp/iso_test
