#!/bin/bash
source ../../scripts/setopts.sh $*
LIBDIR=../../LIBS/intel_osx_64/
source ../../scripts/test_libs.sh

eval make -j 4 ${SMV_MAKE_OPTS}-f ../Makefile intel_osx_64
