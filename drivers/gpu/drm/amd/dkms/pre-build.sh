#!/bin/bash

KCL="amd/amdkcl"
INC="include"
SRC="amd/dkms"

KERNELVER=$1
DKMS_TREE=$2
MODULE_BUILD_DIR=$3
CC=$4
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
       -e '/dma_resv_lockdep/,/subsys_initcall/d' \
       -e '1i\#ifdef HAVE_DMA_RESV_FENCES' \
       -e '$a\#endif' $KCL/dma-buf/dma-resv.c
sed -i -e '/extern struct ww_class reservation_ww_class/i #include <kcl/kcl_dma-resv.h>' \
       -e '/struct dma_resv {/, /}/d' \
       -e '/struct dma_resv_iter {/, /}/d' \
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
	sed -i "/${config}$/s/$/_AMDKCL/" amd/dkms/Kbuild
done

export KERNELVER
ln -s $DKMS_TREE $MODULE_BUILD_DIR

if [ "$CC" == "gcc" ]; then
	# Enable gcc-toolset for kernels that are built with non-default compiler
	# perform this check only when permissions allow
	if [[ -d /opt/rh && `id -u` -eq 0 ]]; then
		for f in $(find /opt/rh -type f -a -name gcc); do
			[[ -f /boot/config-$KERNELVER ]] || continue
			config_gcc_version=$(. /boot/config-$KERNELVER && echo $CONFIG_GCC_VERSION)
			IFS='.' read -ra ver <<<$($f -dumpfullversion)
			gcc_version=$(printf "%d%02d%02d\n" ${ver[@]})
			if [[ "$config_gcc_version" = "$gcc_version" ]]; then
				. ${f%/*}/../../../enable
				break
			fi
		done
	fi
fi
echo "PATH=$PATH" >$MODULE_BUILD_DIR/.env

(cd $SRC && ./configure)

# rename CFLAGS_<path>target.o / CFLAGS_REMOVE_<path> to CFLAGS_target.o
# for kernel version < 5.3
if ! grep -q 'define HAVE_AMDKCL_FLAGS_TAKE_PATH' $SRC/config/config.h; then
	for file in $(grep -rl 'CFLAGS_' amd/display/); do
		sed -i 's|\(CFLAGS_[A-Z_]*\)$(AMDDALPATH)/.*/\(.*\.o\)|\1\2|' $file
	done
fi
