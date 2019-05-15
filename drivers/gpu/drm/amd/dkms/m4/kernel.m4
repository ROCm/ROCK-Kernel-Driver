dnl #
dnl # Default kernel configuration
dnl #
AC_DEFUN([AC_CONFIG_KERNEL], [
	AC_KERNEL
	AC_AMDGPU_KOBJ_TO_DEV
	AC_AMDGPU_DRM_MODESET_LOCK_ALL_CTX
	AC_AMDGPU_DRM_ATOMIC_HELPER_DISABLE_ALL
	AC_AMDGPU_DRM_ATOMIC_HELPER_DUPLICATE_STATE
	AC_AMDGPU_DRM_ATOMIC_HELPER_SUSPEND
	AC_AMDGPU_DRM_ATOMIC_HELPER_RESUME
	AC_AMDGPU_DRM_CRTC_FORCE_DISABLE_ALL
	AC_AMDGPU_DRM_AUDIO_COMPONENT_HEADER
	AC_AMDGPU_DRM_FB_HELPER_REMOVE_CONFLICTING_FRAMEBUFFERS
	AC_AMDGPU_DRM_ENCODER_INIT_VALID_WITH_NAME
	AC_AMDGPU_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME
	AC_AMDGPU_NUM_ARGS_DRM_UNIVERSAL_PLANE_INIT
	AC_AMDGPU_2ARGS_DRM_GEM_OBJECT_LOOKUP
	AC_AMDGPU_IN_COMPAT_SYSCALL
	AC_AMDGPU_DRM_GET_FORMAT_NAME
	AC_AMDGPU_2ARGS_SET_CRC_SOURCE
	AC_AMDGPU_DRM_GEM_OBJECT_PUT_UNLOCKED
	AC_AMDGPU_RENAME_FENCE_TO_DMA_FENCE
	AC_AMDGPU_FENCE_ARRAY_H
	AC_AMDGPU_TIMER_SETUP
	AC_AMDGPU_DMA_FENCE_SET_ERROR
	AC_AMDGPU_PCIE_BANDWIDTH_AVAILABLE
	AC_AMDGPU_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS
	AC_AMDGPU_PCIE_GET_SPEED_AND_WIDTH_CAP
	AC_AMDGPU_PCIE_ENABLE_ATOMIC_OPS_TO_ROOT
	AC_AMDGPU_PCI_IS_THUNDERBOLD_ATTACHED
	AC_AMDGPU_PCI_PCIE_TYPE
	AC_AMDGPU_PCI_UPSTREAM_BRIDGE
	AC_AMDGPU_DRM_IS_CURRENT_MASTER
	AC_AMDGPU_RESERVATION_OBJECT_WAIT_TIMEOUT_RCU
	AC_AMDGPU_RESERVATION_OBJECT_LOCK
	AC_AMDGPU_RESERVATION_OBJECT_TRYLOCK
	AC_AMDGPU_RESERVATION_OBJECT_TEST_SIGNALED_RCU
	AC_AMDGPU_REQUEST_FIRMWARE_DIRECT
	AC_AMDGPU_4ARGS_HWMON_DEVICE_REGISTER_WITH_GROUPS
	AC_AMDGPU_MMU_NOTIFIER_CALL_SRCU
	AC_AMDGPU_FILE_INODE
	AC_AMDGPU_DRM_ATOMIC_HELPER_CONNECTOR_RESET
	AC_AMDGPU_DRM_GET_MAX_IOMEM
	AC_AMDGPU_2ARGS_MM_RELEASE_PAGES
	AC_AMDGPU_LIST_FOR_EACH_ENTRY
	AC_AMDGPU_2ARGS_DEVM_MEMREMAP_PAGES
	AC_AMDGPU_KTIME_SET
	AC_AMDGPU_INVALIDATE_RANGE_START
	AC_AMDGPU_DRM_SYNCOBJ_FENCE_GET
	AC_AMDGPU_MM_INTERVAL_TREE_DEFINE
	AC_AMDGPU_RESERVATION_OBJECT_COPY_FENCES
	AC_AMDGPU_RESERVATION_OBJECT_ADD_SHARED_FENCE
	AC_AMDGPU_LIST_BULK_MOVE_TAIL
	AC_AMDGPU_DRM_SYNCOBJ_FIND_FENCE
	AC_AMDGPU_FORMAT_IN_STRUCT_DRM_FRAMEBUFFER
	AC_AMDGPU_DRM_FREE_LARGE

	AS_IF([test "$LINUX_OBJ" != "$LINUX"], [
		KERNEL_MAKE="$KERNEL_MAKE O=$LINUX_OBJ"
	])

	AC_SUBST(KERNEL_MAKE)
])

dnl #
dnl # Detect name used for Module.symvers file in kernel
dnl #
AC_DEFUN([AC_MODULE_SYMVERS], [
	modpost=$LINUX/scripts/Makefile.modpost
	AC_MSG_CHECKING([kernel file name for module symbols])
	AS_IF([test "x$enable_linux_builtin" != xyes -a -f "$modpost"], [
		AS_IF([grep -q Modules.symvers $modpost], [
			LINUX_SYMBOLS=Modules.symvers
		], [
			LINUX_SYMBOLS=Module.symvers
		])

		AS_IF([test ! -f "$LINUX_OBJ/$LINUX_SYMBOLS"], [
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed.  If you are building with a custom kernel, make sure the
	*** kernel is configured, built, and the '--with-linux=PATH' configure
	*** option refers to the location of the kernel source.])
		])
	], [
		LINUX_SYMBOLS=NONE
	])
	AC_MSG_RESULT($LINUX_SYMBOLS)
	AC_SUBST(LINUX_SYMBOLS)
])

dnl #
dnl # Detect the kernel to be built against
dnl #
AC_DEFUN([AC_KERNEL], [
	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"])

	AC_ARG_WITH(linux-obj,
		AS_HELP_STRING([--with-linux-obj=PATH],
		[Path to kernel build objects]),
		[kernelbuild="$withval"])

	AC_MSG_CHECKING([kernel source directory])
	AS_IF([test -z "$kernelsrc"], [
		AS_IF([test -e "/lib/modules/$(uname -r)/source"], [
			headersdir="/lib/modules/$(uname -r)/source"
			sourcelink=$(readlink -f "$headersdir")
		], [test -e "/lib/modules/$(uname -r)/build"], [
			headersdir="/lib/modules/$(uname -r)/build"
			sourcelink=$(readlink -f "$headersdir")
		], [
			sourcelink=$(ls -1d /usr/src/kernels/* \
			             /usr/src/linux-* \
			             2>/dev/null | grep -v obj | tail -1)
		])

		AS_IF([test -n "$sourcelink" && test -e ${sourcelink}], [
			kernelsrc=`readlink -f ${sourcelink}`
		], [
			kernelsrc="[Not found]"
		])
	], [
		AS_IF([test "$kernelsrc" = "NONE"], [
			kernsrcver=NONE
		])
		withlinux=yes
	])

	AC_MSG_RESULT([$kernelsrc])
	AS_IF([test ! -d "$kernelsrc"], [
		AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed and then try again.  If that fails, you can specify the
	*** location of the kernel source with the '--with-linux=PATH' option.])
	])

	AC_MSG_CHECKING([kernel build directory])
	AS_IF([test -z "$kernelbuild"], [
		AS_IF([test x$withlinux != xyes -a -e "/lib/modules/$(uname -r)/build"], [
			kernelbuild=`readlink -f /lib/modules/$(uname -r)/build`
		], [test -d ${kernelsrc}-obj/${target_cpu}/${target_cpu}], [
			kernelbuild=${kernelsrc}-obj/${target_cpu}/${target_cpu}
		], [test -d ${kernelsrc}-obj/${target_cpu}/default], [
			kernelbuild=${kernelsrc}-obj/${target_cpu}/default
		], [test -d `dirname ${kernelsrc}`/build-${target_cpu}], [
			kernelbuild=`dirname ${kernelsrc}`/build-${target_cpu}
		], [
			kernelbuild=${kernelsrc}
		])
	])
	AC_MSG_RESULT([$kernelbuild])

	AC_MSG_CHECKING([kernel source version])
	utsrelease1=$kernelbuild/include/linux/version.h
	utsrelease2=$kernelbuild/include/linux/utsrelease.h
	utsrelease3=$kernelbuild/include/generated/utsrelease.h
	AS_IF([test -r $utsrelease1 && fgrep -q UTS_RELEASE $utsrelease1], [
		utsrelease=linux/version.h
	], [test -r $utsrelease2 && fgrep -q UTS_RELEASE $utsrelease2], [
		utsrelease=linux/utsrelease.h
	], [test -r $utsrelease3 && fgrep -q UTS_RELEASE $utsrelease3], [
		utsrelease=generated/utsrelease.h
	])

	AS_IF([test "$utsrelease"], [
		kernsrcver=`(echo "#include <$utsrelease>";
		             echo "kernsrcver=UTS_RELEASE") |
		             cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`

		AS_IF([test -z "$kernsrcver"], [
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([*** Cannot determine kernel version.])
		])
	], [
		AC_MSG_RESULT([Not found])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([*** Cannot find UTS_RELEASE definition.])
		else
			AC_MSG_ERROR([
	*** Cannot find UTS_RELEASE definition.
	*** Please run 'make prepare' inside the kernel source tree.])
		fi
	])

	AC_MSG_RESULT([$kernsrcver])

	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}
	LINUX_VERSION=${kernsrcver}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(LINUX_VERSION)

	AC_MODULE_SYMVERS
])

dnl #
dnl # AC_KERNEL_CONFTEST_H
dnl #
AC_DEFUN([AC_KERNEL_CONFTEST_H], [
cat - <<_ACEOF >conftest.h
$1
_ACEOF
])

dnl #
dnl # AC_KERNEL_CONFTEST_C
dnl #
AC_DEFUN([AC_KERNEL_CONFTEST_C], [
cat confdefs.h - <<_ACEOF >conftest.c
$1
_ACEOF
])

dnl #
dnl # AC_KERNEL_LANG_PROGRAM(C)([PROLOGUE], [BODY])
dnl #
AC_DEFUN([AC_KERNEL_LANG_PROGRAM], [
$1
int
main (void)
{
dnl Do *not* indent the following line: there may be CPP directives.
dnl Don't move the `;' right after for the same reason.
$2
  ;
  return 0;
}
])

dnl #
dnl # AC_KERNEL_COMPILE_IFELSE / like AC_COMPILE_IFELSE
dnl #
AC_DEFUN([AC_KERNEL_COMPILE_IFELSE], [
	m4_ifvaln([$1], [AC_KERNEL_CONFTEST_C([$1])])
	m4_ifvaln([$6], [AC_KERNEL_CONFTEST_H([$6])], [AC_KERNEL_CONFTEST_H([])])
	rm -Rf build && mkdir -p build && touch build/conftest.mod.c
	echo "obj-m := conftest.o" >build/Makefile
	modpost_flag=''
	kbuild_src_flag=''
	test "x$enable_linux_builtin" = xyes && modpost_flag='modpost=true' # fake modpost stage
	test "x$enable_linux_builtin" = xyes && kbuild_src_flag='KBUILD_SRC=' # override KBUILD_SRC
	AS_IF(
		[AC_TRY_COMMAND(cp conftest.c conftest.h build && make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror" M=$PWD/build $modpost_flag $kbuild_src_flag) >/dev/null && AC_TRY_COMMAND([$3])],
		[$4],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$5],[$5])]
	)
	rm -Rf build
])

dnl #
dnl # AC_KERNEL_TRY_COMPILE like AC_TRY_COMPILE
dnl #
AC_DEFUN([AC_KERNEL_TRY_COMPILE],
	[AC_KERNEL_COMPILE_IFELSE(
	[AC_LANG_SOURCE([AC_KERNEL_LANG_PROGRAM([[$1]], [[$2]])])],
	[modules],
	[test -s build/conftest.o],
	[$3], [$4])
])

dnl #
dnl # AC_KERNEL_CHECK_SYMBOL_EXPORT
dnl # check symbol exported or not
dnl #
AC_DEFUN([AC_KERNEL_CHECK_SYMBOL_EXPORT], [
	awk -v s="$1" '
		BEGIN {
			n = 0;
			num = split(s, symbols, " ")
		} {
			for (i in symbols)
				if (symbols[[i]] == $[2])
					n++
		} END {
			if (num == n)
				exit 0;
			else
				exit 1
		}' $LINUX_OBJ/$LINUX_SYMBOLS 2>/dev/null
	rc=$?
	if test $rc -ne 0; then
		n=0
		export=0
		for file in $2; do
			n=$(awk -v s="$1" '
				BEGIN {
					n = 0;
					split(s, symbols, " ")
				} {
					for (i in symbols) {
						s="EXPORT_SYMBOL.*"symbols[[i]];
						if ($[0] ~ s)
							n++
					}
				} END {
					print n
				}' $LINUX/$file 2>/dev/null)
			rc=$?
			if test $rc -eq 0; then
				(( export+=n ))
			fi
		done
		if test $(wc -w <<< "$1") -eq $export; then :
			$3
		else :
			$4
		fi
	else :
		$3
	fi
])

dnl #
dnl # AC_KERNEL_TRY_COMPILE_SYMBOL
dnl # like AC_KERNEL_TRY_COMPILE, except AC_KERNEL_CHECK_SYMBOL_EXPORT
dnl # is called if not compiling for builtin
dnl #
AC_DEFUN([AC_KERNEL_TRY_COMPILE_SYMBOL], [
	AC_KERNEL_TRY_COMPILE([$1], [$2], [rc=0], [rc=1])
	if test $rc -ne 0; then :
		$6
	else
		if test "x$enable_linux_builtin" != xyes; then
			AC_KERNEL_CHECK_SYMBOL_EXPORT([$3], [$4], [rc=0], [rc=1])
		fi
		if test $rc -ne 0; then :
			$6
		else :
			$5
		fi
	fi
])
