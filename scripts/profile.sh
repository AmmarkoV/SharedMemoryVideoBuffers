#!/bin/bash
THISDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$THISDIR"

cd ..
make

cd "$THISDIR"
rm callgrind.out.* 
valgrind --tool=callgrind --dump-instr=yes --collect-jumps=yes ./profile_test --profile $@
kcachegrind

exit 0
