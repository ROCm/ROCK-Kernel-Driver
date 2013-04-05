#!/bin/sh
out="$1"
shift
in="$@"

for i in $in; do
	$CPP -dD -imacros $i -x c /dev/null
done | sed -n 's,#define __HYPERVISOR_\([a-z0-9_]*\)[[:space:]]\+\(__HYPERVISOR_arch_\)\?[0-9].*,HYPERCALL(\1),p' \
     | grep -v '(arch_[0-9]\+)' | sort | uniq >$out
