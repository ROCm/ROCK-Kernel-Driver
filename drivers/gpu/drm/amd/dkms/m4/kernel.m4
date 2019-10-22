dnl #
dnl # Default kernel configuration
dnl #
AC_DEFUN([AC_CONFIG_KERNEL], [
	AC_KERNEL
	AC_AMDGPU_ASM_FPU_API_H
	AC_AMDGPU_DRM_ATOMIC
	AC_AMDGPU_DRM_CACHE
	AC_AMDGPU_DRM_CALLOC_LARGE
	AC_AMDGPU_DRM_FREE_LARGE
	AC_AMDGPU_DRM_EDID
	AC_AMDGPU_DRM_HDMI_VENDOR_INFOFRAME_FROM_DISPLAY_MODE
	AC_AMDGPU_DRM_MALLOC_AB
	AC_AMDGPU_DRM_CRTC_HELPER
	AC_AMDGPU_DRM_PROBE_HELPER_H
	AC_AMDGPU_KOBJ_TO_DEV
	AC_AMDGPU_DRM_MODESET_LOCK_ALL_CTX
	AC_AMDGPU_DRM_ATOMIC_HELPER_DISABLE_ALL
	AC_AMDGPU_DRM_ATOMIC_HELPER_DUPLICATE_STATE
	AC_AMDGPU_DRM_ATOMIC_HELPER_SUSPEND
	AC_AMDGPU_DRM_ATOMIC_HELPER_RESUME
	AC_AMDGPU_DRM_CRTC_FORCE_DISABLE_ALL
	AC_AMDGPU_DRM_CRTC_ACCURATE_VBLANK_COUNT
	AC_AMDGPU_DRM_AUDIO_COMPONENT_HEADER
	AC_AMDGPU_DRM_ENCODER_INIT_VALID_WITH_NAME
	AC_AMDGPU_DRM_CRTC_INIT_WITH_PLANES_VALID_WITH_NAME
	AC_AMDGPU_NUM_ARGS_DRM_UNIVERSAL_PLANE_INIT
	AC_AMDGPU_2ARGS_DRM_GEM_OBJECT_LOOKUP
	AC_AMDGPU_DRM_GET_FORMAT_NAME
	AC_AMDGPU_DRM_GEM_OBJECT_PUT_UNLOCKED
	AC_AMDGPU_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS
	AC_AMDGPU_DRM_IS_CURRENT_MASTER
	AC_AMDGPU_DRM_ATOMIC_HELPER_CONNECTOR_RESET
	AC_AMDGPU_DRM_GET_MAX_IOMEM
	AC_AMDGPU_DRM_SYNCOBJ_FIND_FENCE
	AC_AMDGPU_DRM_FRAMEBUFFER_FORMAT
	AC_AMDGPU_DRM_SEND_EVENT_LOCKED
	AC_AMDGPU_DRM_COLOR_LUT
	AC_AMDGPU_DRM_ATOMIC_GET_CRTC_STATE
	AC_AMDGPU_DRM_ATOMIC_GET_NEW_PLANE_STATE
	AC_AMDGPU_DRM_ENCODER_FIND_VALID_WITH_FILE
	AC_AMDGPU_DRM_ATOMIC_HELPER_BEST_ENCODER
	AC_AMDGPU_2ARGS_SET_CRC_SOURCE
	AC_AMDGPU_DRM_DP_CEC_CORRELATION_FUNCTIONS
	AC_AMDGPU_DRM_CONNECTOR_PUT
	AC_AMDGPU_DRM_VMA_NODE_VERIFY_ACCESS
	AC_AMDGPU_2ARGS_DRM_GEM_MAP_ATTACH
	AC_AMDGPU_DRM_DRIVER_UNLOAD
	AC_AMDGPU_DRM_CONNECTOR_UPDATE_EDID_PROPERTY
	AC_AMDGPU_DRM_CONNECTOR_ATTACH_ENCODER
	AC_AMDGPU_DRM_CONNECTOR_SET_PATH_PROPERTY
	AC_AMDGPU_DRM_DISPLAY_INFO_HDMI_SCDC_SCRAMBLING
	AC_AMDGPU_DRM_DRIVER_FEATURE
	AC_AMDGPU_DRM_DEVICE_DRIVER_FEATURES
	AC_AMDGPU_DRM_DEV_UNPLUG
	AC_AMDGPU_DRM_ATOMIC_STATE_PLANE_STATES
	AC_AMDGPU_DRM_ATOMIC_HELPER_SHUTDOWN
	AC_AMDGPU_DRM_FILE_H
	AC_AMDGPU_DRM_PRINT_H
	AC_AMDGPU_DRM_DEVICE_H
	AC_AMDGPU_DRM_SYNCOBJ_FENCE_GET
	AC_AMDGPU_KVCALLOC
	AC_AMDGPU_KVFREE
	AC_AMDGPU_KVMALLOC_ARRAY
	AC_AMDGPU_KVZALLOC_KVMALLOC
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY
	AC_AMDGPU_2ARGS_VIRTUAL_MM_FAULT_FUNCTION
	AC_AMDGPU_DMA_FENCE_HEADERS
	AC_AMDGPU_RESERVATION_OBJECT_RESERVE_SHARED
	AC_AMDGPU_RESERVATION_OBJECT_LOCK_INTERRUPTIBLE
	AC_AMDGPU_RESERVATION_OBJECT_LOCK
	AC_AMDGPU_RESERVATION_OBJECT_TRYLOCK
	AC_AMDGPU_RESERVATION_OBJECT_TEST_SIGNALED_RCU
	AC_AMDGPU_RESERVATION_OBJECT_COPY_FENCES
	AC_AMDGPU_RESERVATION_OBJECT_ADD_SHARED_FENCE
	AC_AMDGPU_RESERVATION_OBJECT_WAIT_TIMEOUT_RCU
	AC_AMDGPU_KSYS_SYNC_HELPER
	AC_AMDGPU_KALLSYMS_LOOKUP_NAME
	AC_AMDGPU_MMU_NOTIFIER
	AC_AMDGPU_OVERFLOW_H
	AC_AMDGPU_PERF_EVENT_UPDATE_USERPAGE
	AC_AMDGPU_2ARGS_DEVM_MEMREMAP_PAGES
	AC_AMDGPU_PTRACE_PARENT
	AC_AMDGPU_PCI
	AC_AMDGPU_GET_SCANOUT_POSITION_IN_DRM_DRIVER
	AC_AMDGPU_GET_VBLANK_TIMESTAMP_IN_DRM_DRIVER
	AC_AMDGPU_VGA_SWITCHEROO_SET_DYNAMIC_SWITCH
	AC_AMDGPU_VGA_SWITCHEROO_REGISTER_HANDLER
	AC_AMDGPU_VGA_SWITCHEROO_GET_CLIENT_ID
	AC_AMDGPU_VGA_SWITCHEROO_REGISTER_CLIENT
	AC_AMDGPU_CHUNK_ID_SYNOBJ_IN_OUT
	AC_AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES
	AC_AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT_SIGNAL
	AC_AMDGPU_PCIE_GET_SPEED_AND_WIDTH_CAP
	AC_AMDGPU_KTHREAD_PARK_XX
	AC_AMDGPU_FILE_INODE
	AC_AMDGPU_SCHED_MM_H
	AC_AMDGPU_MM_ACCESS
	AC_AMDGPU_DRM_FB_HELPER_FILL_INFO
	AC_AMDGPU_DRM_DEV_SUPPORTED
	AC_AMDGPU_TIMER_SETUP
	AC_AMDGPU_PCIE_ENABLE_ATOMIC_OPS_TO_ROOT
	AC_AMDGPU_PCI_IS_THUNDERBOLD_ATTACHED
	AC_AMDGPU_PCI_PCIE_TYPE
	AC_AMDGPU_PCI_UPSTREAM_BRIDGE
	AC_AMDGPU_DRM_CALC_VBLTIMESTAMP_FROM_SCANOUTPOS
	AC_AMDGPU_REQUEST_FIRMWARE_DIRECT
	AC_AMDGPU_4ARGS_HWMON_DEVICE_REGISTER_WITH_GROUPS
	AC_AMDGPU_MMU_NOTIFIER_CALL_SRCU
	AC_AMDGPU_2ARGS_MM_RELEASE_PAGES
	AC_AMDGPU_LIST_FOR_EACH_ENTRY
	AC_AMDGPU_KTIME_SET
	AC_AMDGPU_KTIME_GET_NS
	AC_AMDGPU_KTIME_GET_RAW_NS
	AC_AMDGPU_KTIME_GET_BOOTTIME_NS
	AC_AMDGPU_INVALIDATE_RANGE_START
	AC_AMDGPU_MM_INTERVAL_TREE_DEFINE
	AC_AMDGPU_LIST_BULK_MOVE_TAIL
	AC_AMDGPU_INT_REGISTER_SHRINKER
	AC_AMDGPU_IRQ_DOMAIN
	AC_AMDGPU_ZONE_MANAGED_PAGES
	AC_AMDGPU_ACCESS_OK_WITH_TWO_ARGUMENTS
	AC_AMDGPU_BIN_ATTRS_IN_ATTRIBUTE_GROUP
	AC_AMDGPU_SYSTEM_HIGHPRI_WQ
	AC_AMDGPU_ALLOC_ORDERED_WORKQUEUE
	AC_AMDGPU_WQ_HIGHPRI
	AC_AMDGPU_STRSCPY
	AC_AMDGPU_DRM_PRINTER
	AC_AMDGPU_DEV_PM_SET_DRIVER_FLAGS
	AC_AMDGPU_ARCH_IO_RESERVE_FREE_MEMTYPE_WC
	AC_AMDGPU_KREF_READ
	AC_AMDGPU_MEMALLOC_NOFS_SAVE
	AC_AMDGPU_VMF_INSERT
	AC_AMDGPU_DRM_FENCE_TO_HANDLE
	AC_AMDGPU_DRM_FB_HELPER_LASTCLOSE
	AC_AMDGPU_2ARGS_PM_GENPD_REMOVE_DEVICE
	AC_AMDGPU_SI_MEM_AVAILABLE
	AC_AMDGPU_MMGRAB
	AC_AMDGPU_VERIFY_SET_BUSID_IN_STRUCT_DRM_DRIVER
	AC_AMDGPU_MM_H
	AC_AMDGPU_TASK_H
	AC_AMDGPU_SCHED_TYPES_H
	AC_AMDGPU_LINUX_NOSPEC
	AC_AMDGPU_DRM_DRIVER_GEM_FREE_OBJECT_UNLOCKED
	AC_AMDGPU_PCIE_BANDWIDTH_AVAILABLE
	AC_AMDGPU_DRM_MM_INSERT_MODE
	AC_AMDGPU_AMD_IOMMU_PC_SUPPORTED
	AC_AMDGPU_4ARGS_HASH_FOR_EACH_POSSIBLE
	AC_AMDGPU_4ARGS_HASH_FOR_EACH_POSSIBLE_RCU
	AC_AMDGPU_4ARGS_HASH_FOR_EACH_RCU
	AC_AMDGPU_5ARGS_HASH_FOR_EACH_SAFE
	AC_AMDGPU_DRM_ATOMIC_STATE_ASYNC_UPDATE
	AC_AMDGPU_DRM_MM_PRINT
	AC_AMDGPU_DRM_DEBUG_PRINTER
	AC_AMDGPU_DRM_ATOMIC_NONBLOCKING_COMMIT
	AC_AMDGPU_DRM_ATOMIC_STATE_PUT
	AC_AMDGPU_DRM_ATOMIC_UAPI_HEADER
	AC_AMDGPU_TYPE__POLL_T
	AC_AMDGPU_USE_UNSIGNED_INT_PIPE
	AC_AMDGPU_HW_PERF_EVENT_CONF_MEMBER
	AC_AMDGPU_INTERVAL_TREE_INSERT_HAVE_RB_ROOT_CACHED
	AC_AMDGPU_KMAP_ATOMIC
	AC_AMDGPU_SCHED_CLOCK_H
	AC_AMDGPU_KFIFO_NEW_H
	AC_AMDGPU_DRM_FB_HELPER_CFB_XX
	AC_AMDGPU_DRM_FB_HELPER_XX_FBI
	AC_AMDGPU_DRM_FB_HELPER_SET_SUSPEND_UNLOCKED
	AC_AMDGPU_DRM_ATOMIC_HELPER_UPDATE_LEGACY_MODESET_STATE
	AC_AMDGPU_DEV_PAGEMAP_STRUCT
	AC_AMDGPU_DRM_DEVICE_FILELIST_MUTEX
	AC_AMDGPU_DEV_ERR_RATELIMITED
	AC_AMDGPU_SEQ_HEX_DUMP
	AC_AMDGPU_FB_INFO_APERTURES
	AC_AMDGPU_IDR_REMOVE
	AC_AMDGPU_LINUX_BITS
	AC_AMDGPU_I2C_LOCK_OPERATIONS_STRUCT
	AC_AMDGPU_SET_MEMORY_H
	AC_AMDGPU_DOWN_WRITE_KILLABLE
	AC_AMDGPU_BACKLIGHT_DEVICE_REGISTER_WITH_5ARGS
	AC_AMDGPU_BACKLIGHT_PROPERTIES_TYPE
	AC_AMDGPU_FB_OPS_FB_DEBUG_XX
	AC_AMDGPU_MANAGED_PAGES_IN_STRUCT_ZONE
	AC_AMDGPU_DEVCGROUP_CHECK_PERMISSION
	AC_AMDGPU_DRIVER_ATOMIC
	AC_AMDGPU_IOMMU_GET_DOMAIN_FOR_DEV
	AC_AMDGPU_VM_FAULT_ADDRESS_VMA
	AC_AMDGPU_HLIST_FOR_EACH_ENTRY_WITH_4ARGS
	AC_AMDGPU_GET_USER_PAGES
	AC_AMDGPU_GET_USER_PAGES_REMOTE
	AC_AMDGPU_KTIME_GET_REAL_SECONDS
	AC_AMDGPU_DRM_CONNECTOR_LIST_ITER_BEGIN
	AC_AMDGPU_DRM_UTIL_H
	AC_AMDGPU_DRM_MODE_IS_420_XXX
	AC_AMDGPU_IN_COMPAT_SYSCALL
	AC_AMDGPU_DRM_ERROR
	AC_AMDGPU_SIGNAL_H
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_CBS

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
	AS_IF([test -f "$modpost"], [
		AS_IF([grep -q Modules.symvers $modpost], [
			LINUX_SYMBOLS=Modules.symvers
		], [
			LINUX_SYMBOLS=Module.symvers
		])

		AS_IF([test "x$enable_linux_builtin" != xyes -a ! -f "$LINUX_OBJ/$LINUX_SYMBOLS"], [
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
		AS_IF([test -e "/lib/modules/$KERNELVER/source"], [
			headersdir="/lib/modules/$KERNELVER/source"
			sourcelink=$(readlink -f "$headersdir")
		], [test -e "/lib/modules/$KERNELVER/build"], [
			headersdir="/lib/modules/$KERNELVER/build"
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
		AS_IF([test x$withlinux != xyes -a -e "/lib/modules/$KERNELVER/build"], [
			kernelbuild=`readlink -f /lib/modules/$KERNELVER/build`
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
dnl # $1: contents to be filled in conftest.h
dnl #
AC_DEFUN([AC_KERNEL_CONFTEST_H], [
cat - <<_ACEOF >conftest.h
$1
_ACEOF
])

dnl #
dnl # AC_KERNEL_CONFTEST_C
dnl # fill in contents of conftest.h and $1 to conftest.c
dnl # $1: contents to be filled in conftest.c
dnl #
AC_DEFUN([AC_KERNEL_CONFTEST_C], [
cat confdefs.h - <<_ACEOF >conftest.c
$1
_ACEOF
])

dnl #
dnl # AC_KERNEL_LANG_PROGRAM([PROLOGUE], [BODY])
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
dnl # $1: contents to be filled in conftest.c
dnl # $2: make target. "modules" for most case
dnl # $3: user defined commands. It "AND" the make command to check the result. If true, expands to $4. Otherwise $5.
dnl # $4: run it if make & $3 pass.
dnl # $5: run it if make & $3 fail.
dnl # $6: contents to be filled in conftest.h. Could be null.
dnl #
AC_DEFUN([AC_KERNEL_COMPILE_IFELSE], [
	m4_ifvaln([$1], [AC_KERNEL_CONFTEST_C([$1])])
	m4_ifvaln([$6], [AC_KERNEL_CONFTEST_H([$6])], [AC_KERNEL_CONFTEST_H([])])
	rm -Rf build && mkdir -p build && touch build/conftest.mod.c
	echo "obj-m := conftest.o" >build/Makefile
	modpost_flag=''
	kbuild_src_flag=''
	kbuild_workaround_flag=''
	test "x$enable_linux_builtin" = xyes && modpost_flag='modpost=true' # fake modpost stage
	test "x$enable_linux_builtin" = xyes && kbuild_src_flag='KBUILD_SRC=' # override KBUILD_SRC
	test "x$enable_linux_builtin" = xyes && kbuild_workaround_flag='sub_make_done=' # override sub_make_done
	AS_IF(
		[AC_TRY_COMMAND(cp conftest.c conftest.h build && make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror -Wno-error=uninitialized -Wno-error=unused-variable" M=$PWD/build $modpost_flag $kbuild_src_flag $kbuild_workaround_flag) >/dev/null && AC_TRY_COMMAND([$3])],
		[$4],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$5],[$5])]
	)
	rm -Rf build
])

dnl #
dnl # AC_KERNEL_TRY_COMPILE like AC_TRY_COMPILE
dnl # $1: Prologue for conftest.c. including header files, extends, etc
dnl # $2: Body for conftest.c.
dnl # $3: run it if compile pass.
dnl # $4: run it if compile fail.
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
dnl # $1: symbol list to look for
dnl # $2: file list to look for $1
dnl # $3: run it if pass.
dnl # $4: run it if fail.
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
						s="EXPORT_SYMBOL.*\\("symbols[[i]]"\\);"
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
dnl # $1: Prologue for conftest.c. including header files, extends, etc
dnl # $2: Body for conftest.c.
dnl # $3: AC_KERNEL_CHECK_SYMBOL_EXPORT $1
dnl # $4: AC_KERNEL_CHECK_SYMBOL_EXPORT $2
dnl # $5: run it if checking pass
dnl # $6: run it if checking fail
dnl #
AC_DEFUN([AC_KERNEL_TRY_COMPILE_SYMBOL], [
	AC_KERNEL_TRY_COMPILE([$1], [$2], [rc=0], [rc=1])
	if test $rc -ne 0; then :
		$6
	else
		AC_KERNEL_CHECK_SYMBOL_EXPORT([$3], [$4], [rc=0], [rc=1])
		if test $rc -ne 0; then :
			$6
		else :
			$5
		fi
	fi
])

dnl #
dnl # AC_KERNEL_TEST_HEADER_FILE_EXIST
dnl # check header file exist
dnl # $1: header file to check
dnl # $2: run it if header file exist
dnl # $3: run it if header file nonexistent
dnl #
AC_DEFUN([AC_KERNEL_TEST_HEADER_FILE_EXIST], [
	header_file=m4_normalize([$1])
	header_file_obj=$LINUX_OBJ/include/$header_file
	header_file_src=$LINUX/include/$header_file
	AS_IF([test -e $header_file_obj -o -e $header_file_src], [
		$2
	], [
		$3
	])
])
