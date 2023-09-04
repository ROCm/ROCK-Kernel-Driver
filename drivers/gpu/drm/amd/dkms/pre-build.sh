#!/bin/bash

KCL="amd/amdkcl"
INC="include"
SRC="amd/dkms"

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

source $KCL/files

sed -i -e '/DEFINE_WD_CLASS(reservation_ww_class)/,/EXPORT_SYMBOL(reservation_ww_class)/d' \
       -e '/dma_resv_lockdep/,/subsys_initcall/d' $KCL/dma-buf/dma-resv.c
sed -i -e '/extern struct ww_class reservation_ww_class/i #include <kcl/kcl_dma-resv.h>' \
       -e '/struct dma_resv {/, /}/d' $INC/linux/dma-resv.h \
       -e '/struct dma_resv_iter {/, /}/d' $INC/linux/dma-resv.h \
       -e '/enum dma_resv_usage {/, /}/d' $INC/linux/dma-resv.h

# add amd prefix to exported symbols
for file in $FILES; do
	awk -F'[()]' '/EXPORT_SYMBOL/ {
		print "#define "$2" amd"$2" //"$0
	}' $file | sort -u >>$INC/rename_symbol.h
done

# rename CONFIG_xxx to CONFIG_xxx_AMDKCL
# otherwise kernel config would override dkms package config
AMDGPU_CONFIG=$(find -name Kconfig -exec grep -h '^config' {} + | sed 's/ /_/' | tr 'a-z' 'A-Z')
TTM_CONFIG=$(awk '/CONFIG_DRM/{gsub(".*\\(CONFIG_DRM","CONFIG_DRM");gsub("\\).*","");print $0}' ttm/Makefile)
SCHED_CONFIG=$(awk '/CONFIG_DRM/{gsub(".*\\(CONFIG_DRM","CONFIG_DRM");gsub("\\).*","");print $0}' scheduler/Makefile)
for config in $AMDGPU_CONFIG $TTM_CONFIG $SCHED_CONFIG; do
	for file in $(grep -rl $config ./); do
		sed -i "s/\<$config\>/&_AMDKCL/" $file
	done
	sed -i "/${config}$/s/$/_AMDKCL/" amd/dkms/Makefile
done

#!/bin/bash

KERNELVER=$1

#
# Kernel 5.x and Kernel 4.x scripts/Makefile.build patch
# The patch makes rules robust against "Argument list too long" error
#
if [ ${KERNELVER%%.*} -eq 5  -o  ${KERNELVER%%.*} -eq 4 ]; then
	moddir="/lib/modules/$KERNELVER"
	mkfile="scripts/Makefile.build"

	if [[ -d "$moddir/source" ]]; then
		mkfile="$moddir/source/$mkfile"
	else
		mkfile="$moddir/build/$mkfile"
	fi

	mkfile=$(readlink -e $mkfile)

	if [[ "$?" -eq 0 ]] && [[ ! -f "$mkfile~" ]]; then
		cp -a ${mkfile}{,~}
		sed -i -e "/^cmd_mod = {/,/} > \$@$/c"`
			`"cmd_mod = printf '%s\x5Cn' \$(call real-search, \$*.o, .o, -objs -y -m) | \\\\\n"`
			`"\t\$(AWK) '!x[\$\$0]++ { print(\"\$(obj)\/\"\$\$0) }' > \$@" \
			-e "s/^[[:space:]]*cmd_link_multi-m = \$(LD).*$/"`
			`"cmd_link_multi-m = \\\\\n"`
			`"\t\$(file >\$@.in,\$(filter %.o,$^)) \\\\\n"`
			`"\t\$(LD) \$(ld_flags) -r -o \$@ @\$@.in; \\\\\n"`
			`"\trm -f \$@.in/" \
			$mkfile
	fi
fi

export KERNELVER
(cd $SRC && ./configure)

# rename CFLAGS_<path>target.o / CFLAGS_REMOVE_<path> to CFLAGS_target.o
# for kernel version < 5.3
if ! grep -q 'define HAVE_AMDKCL_FLAGS_TAKE_PATH' $SRC/config/config.h; then
	for file in $(grep -rl 'CFLAGS_' amd/display/); do
		sed -i 's|\(CFLAGS_[A-Z_]*\)$(AMDDALPATH)/.*/\(.*\.o\)|\1\2|' $file
	done
fi

if ! grep -q 'define HAVE_DMA_RESV_FENCES' $SRC/config/config.h; then
 sed -i 's|dma-buf/dma-resv.o|kcl_dma-resv.o|' amd/amdkcl/Makefile
fi
