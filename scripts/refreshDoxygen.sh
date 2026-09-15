#!/bin/bash
# Generates the documentation: doc/html/, doc/latex/ and SharedMemoryVideoBuffers.pdf in the project root.
# Needs doxygen, graphviz (dot) and pdflatex.

STARTDIR=`pwd`
#Switch to this directory
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$DIR/.."
ROOT=`pwd`

# Read by doc/doxyfile
export DOXYGEN_INPUT="$ROOT"
export DOXYGEN_OUTPUT="$ROOT/doc"

# Start clean, so documentation of removed files doesn't linger
rm -rf doc/html doc/latex

if ! doxygen doc/doxyfile; then
  echo "doxygen failed"
  cd "$STARTDIR"
  exit 1
fi

if ! make -C doc/latex; then
  echo "Building the PDF failed, see doc/latex/refman.log"
  cd "$STARTDIR"
  exit 1
fi

rm -f SharedMemoryVideoBuffers.pdf
cp doc/latex/refman.pdf SharedMemoryVideoBuffers.pdf
echo "Generated $ROOT/SharedMemoryVideoBuffers.pdf and $ROOT/doc/html/index.html"

cd "$STARTDIR"

exit 0
