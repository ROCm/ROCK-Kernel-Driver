#!/bin/bash

KERNELVER=$1
KERNELVER_BASE=${KERNELVER%%-*}

version_lt () {
    newest=$((echo "$KERNELVER_BASE"; echo "$1") | sort -V | tail -n1)
    [ "$KERNELVER_BASE" != "$newest" ]
}

version_ge () {
    newest=$((echo "$KERNELVER_BASE"; echo "$1") | sort -V | tail -n1)
    [ "$KERNELVER_BASE" = "$newest" ]
}

version_gt () {
    oldest=$((echo "$KERNELVER_BASE"; echo "$1") | sort -V | head -n1)
    [ "$KERNELVER_BASE" != "$oldest" ]
}

version_le () {
    oldest=$((echo "$KERNELVER_BASE"; echo "$1") | sort -V | head -n1)
    [ "$KERNELVER_BASE" = "$oldest" ]
}

find ttm -name '*.c' -exec grep EXPORT_SYMBOL {} + \
    | sort -u \
    | awk -F'[()]' '{print "#define "$2" amd"$2" //"$0}'\
    > include/rename_symbol.h
