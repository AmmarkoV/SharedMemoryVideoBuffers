#!/bin/bash
THISDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$THISDIR"
  
make

valgrind --tool=memcheck --leak-check=yes --show-reachable=yes --track-origins=yes --num-callers=20 --track-fds=yes ./client $@ 2>error.txt
cat error.txt 

exit 0
