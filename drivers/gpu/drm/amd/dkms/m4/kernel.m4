dnl #
dnl # Default kernel configuration
dnl #
AC_DEFUN([AC_CONFIG_KERNEL], [
	AC_KERNEL
	AC_KERNEL_SINGLE_TARGET
	AC_AMDGPU_LINUX_HEADERS
	AC_AMDGPU_DRM_HEADERS
	AC_AMDGPU_KALLSYMS_LOOKUP_NAME
	AC_KERNEL_SUPPORTED_AMD_CHIPS
	AC_AMDGPU_IDR
	AC_AMDGPU_TYPE__POLL_T
	AC_AMDGPU_DMA_MAP_SGTABLE
	AC_AMDGPU_I2C_NEW_CLIENT_DEVICE
	AC_AMDGPU_I2C_LOCK_OPERATIONS_STRUCT
	AC_AMDGPU_BACKLIGHT_DEVICE_SET_BRIGHTNESS
	AC_AMDGPU_DEV_PM_SET_DRIVER_FLAGS
	AC_AMDGPU_COMPAT_PTR_IOCTL
	AC_AMDGPU___KTHREAD_SHOULD_PARK
	AC_AMDGPU_LIST_ROTATE_TO_FRONT
	AC_AMDGPU_LIST_IS_FIRST
	AC_AMDGPU_ARCH_IO_RESERVE_FREE_MEMTYPE_WC
	AC_AMDGPU_ACCESS_OK_WITH_TWO_ARGUMENTS
	AC_AMDGPU_IN_COMPAT_SYSCALL
	AC_AMDGPU_SEQ_HEX_DUMP
	AC_AMDGPU_KSYS_SYNC_HELPER
	AC_AMDGPU_PCI_UPSTREAM_BRIDGE
	AC_AMDGPU_PCI_CONFIGURE_EXTENDED_TAGS
	AC_AMDGPU_PCI_DEV_ID
	AC_AMDGPU_PCI_IS_THUNDERBOLD_ATTACHED
	AC_AMDGPU_PCI_REBAR_BYTES_TO_SIZE
	AC_AMDGPU_KTIME_GET_BOOTTIME_NS
	AC_AMDGPU_KTIME_GET_RAW_NS
	AC_AMDGPU_VGA_SWITCHEROO_SET_DYNAMIC_SWITCH
	AC_AMDGPU_MEMALLOC_NOFS_SAVE
	AC_AMDGPU_ZONE_MANAGED_PAGES
	AC_AMDGPU_FAULT_FLAG_ALLOW_RETRY_FIRST
	AC_AMDGPU_FSLEEP
	AC_AMDGPU_VMF_INSERT
	AC_AMDGPU_VMF_INSERT_MIXED_PROT
	AC_AMDGPU_VMF_INSERT_PFN_PROT
	AC_AMDGPU_VM_OPERATIONS_STRUCT_FAULT
	AC_AMDGPU_MMU_NOTIFIER
	AC_AMDGPU_MMU_NOTIFIER_SYNCHRONIZE
	AC_AMDGPU_MMU_NOTIFIER_CALL_SRCU
	AC_AMDGPU_MM_RELEASE_PAGES
	AC_AMDGPU_SCHED_SET_FIFO_LOW
	AC_AMDGPU_DMA_RESV
	AC_AMDGPU_TTM_BUFFER_OBJECT
	AC_AMDGPU_DEVCGROUP_CHECK_PERMISSION
	AC_AMDGPU_HMM
	AC_AMDGPU_INVALIDATE_RANGE_START
	AC_AMDGPU_DOWN_WRITE_KILLABLE
	AC_AMDGPU_INTERVAL_TREE_INSERT_HAVE_RB_ROOT_CACHED
	AC_AMDGPU_GET_USER_PAGES_REMOTE
	AC_AMDGPU_GET_USER_PAGES
	AC_AMDGPU_DMA_BUF
	AC_AMDGPU_LIST_FOR_EACH_ENTRY
	AC_AMDGPU_AMD_IOMMU_PC_SUPPORTED
	AC_AMDGPU_AMD_IOMMU_INVALIDATE_CTX
	AC_AMDGPU_DEV_PAGEMAP
	AC_AMDGPU_DOWN_READ_KILLABLE
	AC_AMDGPU_DRM_CACHE
	AC_AMDGPU_DRM_DEBUG_ENABLED
	AC_AMDGPU_DRM_GEM_OBJECT_PUT
	AC_AMDGPU_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS
	AC_AMDGPU_DRM_CONNECTOR_INIT_WITH_DDC
	AC_AMDGPU_DRM_DP_CALC_PBN_MODE
	AC_AMDGPU_DRM_DP_ATOMIC_FUNCS
	AC_AMDGPU_DRM_DP_SEND_REAL_EDID_CHECKSUM
	AC_AMDGPU_DRM_DP_CEC_CORRELATION_FUNCTIONS
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_MGR_RESUME
	AC_AMDGPU___DRM_ATOMIC_HELPER_CRTC_RESET
	AC_AMDGPU_DRM_DRIVER_GEM_PRIME_RES_OBJ
	AC_AMDGPU_DRM_DRV_GEM_PRIME_EXPORT
	AC_AMDGPU_DRM_PRINT_BITS
	AC_AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES
	AC_AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT_SIGNAL
	AC_AMDGPU_STRUCT_DRM_DEVICE
	AC_AMDGPU_DRM_DRIVER_FEATURE
	AC_AMDGPU___DRM_ATOMIC_HELPER_CRTC_RESET
	AC_AMDGPU_PCI_PR3_PRESENT
	AC_AMDGPU_KTHREAD_USE_MM
	AC_AMDGPU_DRM_WRITEBACK_CONNECTOR_INIT
	AC_AMDGPU_DRM_CONNECTOR_HELPER_FUNCS_PREPARE_WRITEBACK_JOB
	AC_AMDGPU_DRM_FB_HELPER_FILL_INFO
	AC_AMDGPU_DRM_FB_HELPER_INIT
	AC_AMDGPU_DRM_HELPER_FORCE_DISABLE_ALL
	AC_AMDGPU_STRUCT_DRM_CRTC_FUNCS
	AC_AMDGPU_DRM_CONNECTOR_FOR_EACH_POSSIBLE_ENCODER
	AC_AMDGPU_DRM_EDID
	AC_AMDGPU_DRM_EDID_OVERRIDE_CONNECTOR_UPDATE
	AC_AMDGPU_DRM_MODE_INIT
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_CBS
	AC_AMDGPU_DRM_ATOMIC_PRIVATE_OBJ_INIT
	AC_AMDGPU_DRM_DISPLAY_INFO
	AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS
	AC_AMDGPU_DRM_DP_MST_ATOMIC_CHECK
	AC_AMDGPU_DRM_DP_MST_ATOMIC_ENABLE_DSC
	AC_AMDGPU_DRM_CONNECTOR_HELPER_FUNCS
	AC_AMDGPU_DRM_CONNECTOR_EDID_OVERRIDE
	AC_AMDGPU_DRM_DP_MST_DETECT_PORT
	AC_AMDGPU_STRUCT_DRM_CRTC_STATE
	AC_AMDGPU_DRM_DP_MST_DSC_AUX_FOR_PORT
	AC_AMDGPU_DRM_DP_MST_ADD_AFFECTED_DSC_CRTCS
	AC_AMDGPU_DRM_CONNECTOR_HAVE_HDR_SINK_METADATA
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_MGR_INIT
	AC_AMDGPU_DRM_MODE_CONFIG
	AC_AMDGPU_DRM_CONNECTOR_STATE_HDCP_CONTENT_TYPE
	AC_AMDGPU_DRM_HDCP_UPDATE_CONTENT_PROTECTION
	AC_AMDGPU_DRM_MODE_CREATE_COLORSPACE_PROPERTY_FUNCS
	AC_AMDGPU_STRUCT_DRM_CONNECTOR_STATE
	AC_AMDGPU_JIFFIES64_TO_MSECS
	AC_AMDGPU_ACPI_PUT_TABLE
	AC_AMDGPU_ACPI_DEV_GET_FIRST_MATCH_DEV
	AC_AMDGPU_DRM_ATOMIC_HELPER_CALC_TIMESTAMPING_CONSTANTS
	AC_AMDGPU_DRM_FORMAT_INFO
	AC_AMDGPU_STRUCT_DRM_CONNECTOR_STATE_COLORSPACE
	AC_AMDGPU_STRUCT_DRM_ATOMIC_STATE_DUPLICATED
	AC_AMDGPU_DRM_DP_SUBCONNECTOR
	AC_AMDGPU_DRM_PRIME_SG_TO_DMA_ADDR_ARRAY
	AC_AMDGPU_DRM_PRIME_PAGES_TO_SG
	AC_AMDGPU_DRM_CRTC_HELPER_FUNCS
	AC_AMDGPU_DEBUGFS_CREATE_FILE_SIZE
	AC_AMDGPU_DRM_DRIVER_GEM_OPEN_OBJECT
	AC_AMDGPU_FS_RECLAIM_ACQUIRE
	AC_AMDGPU_MEMALLOC_NORECLAIM_SAVE
	AC_AMDGPU_PM_SUSPEND_VIA_FIRMWARE
	AC_AMDGPU_SYSFS_EMIT
	AC_AMDGPU_KTIME_IS_UNION
	AC_AMDGPU_CHECK_SMCA_UMC_V2
	AC_AMDGPU_PXM_TO_NODE
	AC_AMDGPU_ACPI_SRAT_GENERIC_AFFINITY
	AC_AMDGPU_KERNEL_WRITE
	AC_AMDGPU_STRUCT_XARRAY
	AC_AMDGPU_MMPUT_ASYNC
	AC_AMDGPU_DRM_MEMCPY_FROM_WC
	AC_AMDGPU_IS_COW_MAPPING
	AC_AMDGPU_VGA_REMOVE_VGACON
	AC_AMDGPU_PCI_DRIVER_DEV_GROUPS
	AC_AMDGPU_IO_MAPPING_UNMAP_LOCAL
	AC_AMDGPU_IO_MAPPING_MAP_LOCAL_WC
	AC_AMDGPU_KMAP_LOCAL
	AC_AMDGPU_DRM_DP_AUX_DRM_DEV
	AC_AMDGPU_DRM_DP_LINK_TRAIN_CLOCK_RECOVERY_DELAY
	AC_AMDGPU_DRM_DP_LINK_TRAIN_CHANNEL_EQ_DELAY
	AC_AMDGPU_DRM_CONNECTOR_ATOMIC_HDR_METADATA_EQUAL
	AC_AMDGPU_DRM_CONNECTOR_ATTACH_HDR_OUTPUT_METADATA_PROPERTY
	AC_AMDGPU_DRM_CONNECTOR_STATE_HDR_OUTPUT_METADATA
	AC_AMDGPU_DRM_DEVICE_PDEV
	AC_AMDGPU_DRM_CONNECTOR_SET_PANEL_ORIENTATION_WITH_QUIRK
	AC_AMDGPU_DRM_SIMPLE_ENCODER_INIT
	AC_AMDGPU_DRM_DP_UPDATE_PAYLOAD_PART1_START_SLOT_ARG
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_STATE_TOTAL_AVAIL_SLOTS
	AC_AMDGPU_DRM_DISPLAY_INFO_IS_HDMI
	AC_AMDGPU_DRM_BITMAP_FUNCS
	AC_AMDGPU_STRUCT_KOBJ_TYPE
	AC_AMDGPU_ATTRIBUTE_GROUP_IS_BIN_VISIBLE
	AC_AMDGPU_MIGRATE_DISABLE
	AC_AMDGPU_CLOSE_FD
	AC_AMDGPU_DRM_DP_MST_HPD_IRQ_HANDLE_EVENT
	AC_AMDGPU_DRM_VMA_OFFSET_NODE_READONLY_FIELD
	AC_AMDGPU_WW_MUTEX_TRYLOCK_CONTEXT_ARG
	AC_AMDGPU_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_DRM_DRIVER_ARG
	AC_AMDGPU_SYNCHRONIZE_SHRINKERS
	AC_AMDGPU_KREALLOC_ARRAY
	AC_AMDGPU_VGA_CLIENT_REGISTER_NOT_PASS_COOKIE
	AC_AMDGPU_VMA_LOOKUP
	AC_AMDGPU_DMA_FENCE_CHAIN_ALLOC
	AC_AMDGPU_DMA_FENCE_CHAIN_STRUCT
	AC_AMDGPU_DMA_FENCE_OPS_USE_64BIT_SEQNO
	AC_AMDGPU_GENERIC_HANDLE_DOMAIN_IRQ
	AC_AMDGPU__DMA_FENCE_IS_LATER
	AC_AMDGPU_DRM_FIRMWARE_DRIVERS_ONLY
	AC_AMDGPU_DMA_FENCE_DESCRIBE
	AC_AMDGPU_DRM_KMS_HELPER_CONNECTOR_HOTPLUG_EVENT
	AC_AMDGPU_PCIE_ASPM_ENABLED
	AC_AMDGPU_PM_SUSPEND_TARGET_STATE
	AC_AMDGPU_SMCA_GET_BANK_TYPE
	AC_AMDGPU_MCE_PRIO_UC
	AC_AMDGPU_DRM_LEGACY_IRQ_UNINSTALL
	AC_AMDGPU_X86_HYPERVISOR_TYPE
	AC_AMDGPU_HYPERVISOR_IS_TYPE
	AC_AMDGPU_PCI_DEV_LTR_PATH
	AC_AMDGPU_CANCEL_WORK
	AC_AMDGPU_DMA_FENCE_IS_CONTAINER
	AC_AMDGPU_STR_YES_NO
	AC_AMDGPU_TOTALRAM_PAGES
	AC_AMDGPU_DMA_FENCE_CHAIN_CONTAINED
	AC_AMDGPU_DRM_GEM_OBJECT_FUNCS_VMAP_HAS_IOSYS_MAP_ARG
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_MGR_BASE
	AC_AMDGPU_DRM_DSC_CONFIG_SIMPLE_422
	AC_AMDGPU_DRM_DP_READ_DPCD_CAPS
	AC_AMDGPU_DRM_DP_REMOVE_RAYLOAD_PART
	AC_AMDGPU_DRM_DSC_PPS_PAYLOAD_PACK
	AC_AMDGPU_DRM_DSC_COMPUTE_RC_PARAMETERS
	AC_AMDGPU_DRM_GEM_PLANE_HELPER_PREPARE_FB
	AC_AMDGPU_BITMAP_TO_ARR32
	AC_AMDGPU_REGISTER_SHRINKER
	AC_AMDGPU_DRM_DP_MST_POST_PASSTHROUGH_AUX
	AC_AMDGPU_DRM_DP_MST_PORT_FULL_PBN
	AC_AMDGPU_ACPI_VIDEO_FUNCS
	AC_AMDGPU_DRM_PLANE_HELPER_FUNCS
	AC_AMDGPU_MEMORY_DEVICE_COHERENT
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_STATE_PAYLOADS
	AC_AMDGPU_DRM_DP_MST_TOPOLOGY_STATE_PBN_DIV
	AC_AMDGPU_MIGRATE_VMA_FAULT_PAGE
	AC_AMDGPU_RB_ADD_CACHED
	AC_AMDGPU_WANT_INIT_ON_FREE
	AC_AMDGPU_APPLE_GMUX_DETECT
	AC_AMDGPU_MM_KMALLOC_SIZE_ROUNDUP
	AC_AMDGPU_ZONE_DEVICE_PAGE_INIT
	AC_AMDGPU_DRM_SUBALLOC_MANAGER_INIT
	AC_AMDGPU_VM_FLAGS_SET
	AC_AMDGPU_MMAP_ASSERT_WRITE_LOCKED
	AC_AMDGPU_PID_TYPE
	AC_AMDGPU_DMA_FENCE_OPS_SET_DEADLINE
	AC_AMDGPU_LINUX_DEVICE_CLASS

	AC_KERNEL_WAIT
	AS_IF([test "$LINUX_OBJ" != "$LINUX"], [
		KERNEL_MAKE="$KERNEL_MAKE O=$LINUX_OBJ"
	])

	AC_SUBST(KERNEL_MAKE)
	AH_BOTTOM([#include "config-amd-chips.h"])
	AH_BOTTOM([#define AMDGPU_VERSION PACKAGE_VERSION])
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
	build_dir_root=$(cd "${0%/*}" && pwd)

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
cat $build_dir_root/confdefs.h - <<_ACEOF >conftest.c
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
dnl # AC_KERNEL_COMPILE_MODULE_IFELSE / like AC_COMPILE_IFELSE
dnl # $1: contents to be filled in conftest.c
dnl # $2: make target.
dnl # $3: user defined commands. It "AND" the make command to check the result. If true, expands to $4. Otherwise $5.
dnl # $4: run it if make & $3 pass.
dnl # $5: run it if make & $3 fail.
dnl # $6: contents to be filled in conftest.h. Could be null.
dnl #
AC_DEFUN([AC_KERNEL_COMPILE_MODULE_IFELSE], [
	m4_ifvaln([$1], [AC_KERNEL_CONFTEST_C([$1])])
	m4_ifvaln([$6], [AC_KERNEL_CONFTEST_H([$6])], [AC_KERNEL_CONFTEST_H([])])
	touch conftest.mod.c
	if test "x$SINGLE_TARGET_BUILD_NO_TMP_VERSIONS" = x1; then
		test -d $SINGLE_TARGET_BUILD_MODVERDIR || mkdir $SINGLE_TARGET_BUILD_MODVERDIR
		rm -f $SINGLE_TARGET_BUILD_MODVERDIR/*
	fi
	echo "obj-m := conftest.o" >Makefile
	kbuild_src_flag=''
	kbuild_modpost_flag='KBUILD_MODPOST_NOFINAL=1 KBUILD_MODPOST_WARN=1'
	kbuild_workaround_flag=''
	test "x$enable_linux_builtin" = xyes && kbuild_src_flag='KBUILD_SRC=' # override KBUILD_SRC
	test "x$enable_linux_builtin" = xyes && kbuild_workaround_flag='sub_make_done=' # override sub_make_done
	AS_IF(
		[AC_TRY_COMMAND(make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror -Wno-error=array-bounds" M=$PWD $kbuild_src_flag $kbuild_workaround_flag $kbuild_modpost_flag) >/dev/null && AC_TRY_COMMAND([$3])],
		[$4],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$5],[$5])]
	)
])

dnl #
dnl # AC_KERNEL_TMP_BUILD_DIR
dnl # $1: contents to be executed in a temporary directory
dnl #
AC_DEFUN([AC_KERNEL_TMP_BUILD_DIR], [
	build_dir=$(mktemp -d -t build_XXXXXXXX -p $build_dir_root)
	cd $build_dir
	$1
	AS_IF([test -s confdefs.h], [
		cat confdefs.h >>$build_dir_root/confdefs.h
	])
	cd $build_dir_root
	rm -rf $build_dir
])

dnl #
dnl # AC_KERNEL_TRY_COMPILE_MODULE like AC_TRY_COMPILE
dnl # $1: Prologue for conftest.c. including header files, extends, etc
dnl # $2: Body for conftest.c.
dnl # $3: run it if compile pass.
dnl # $4: run it if compile fail.
dnl #
AC_DEFUN([AC_KERNEL_TRY_COMPILE_MODULE],
	target='conftest.o'
	[AC_KERNEL_COMPILE_MODULE_IFELSE(
	[AC_LANG_SOURCE([AC_KERNEL_LANG_PROGRAM([[$1]], [[$2]])])],
	[$target],
	[test -s conftest.o],
	[$3], [$4])
])

dnl #
dnl # AC_KERNEL_COMPILE_IFELSE / like AC_COMPILE_IFELSE
dnl # $1: contents to be filled in conftest.c
dnl # $2: user defined commands. It "AND" the make command to check the result. If true, expands to $4. Otherwise $5.
dnl # $3: run it if make & $3 pass.
dnl # $4: run it if make & $3 fail.
dnl # $5: contents to be filled in conftest.h. Could be null.
dnl #
AC_DEFUN([AC_KERNEL_COMPILE_IFELSE], [
	m4_ifvaln([$1], [AC_KERNEL_CONFTEST_C([$1])])
	m4_ifvaln([$5], [AC_KERNEL_CONFTEST_H([$5])], [AC_KERNEL_CONFTEST_H([])])
	AS_IF(
		[AC_TRY_COMMAND(eval $CC $CFLAGS) > /dev/null && AC_TRY_COMMAND([$2])],
		[$3],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$4],[$4])]
	)
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
	[test -s conftest.o || test -s .tmp_conftest.o],
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
						s="EXPORT_SYMBOL.*\\("symbols[[i]]"\\)"
						if ($[0] ~ s)
							n++
					}
				} END {
					print n
				}' $LINUX/$file 2>/dev/null)
			rc=$?
			if test $rc -eq 0; then
				export=$(( $export+$n ))
			fi
		done
		if test $(echo "$1" | wc -w) -eq $export; then :
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

dnl #
dnl # AC_KERNEL_CHECK_HEADERS
dnl # check whether header file(s) is(are) present
dnl # $1: header filei(s) to check
dnl #
AC_DEFUN([AC_KERNEL_CHECK_HEADERS], [
	AC_CHECK_HEADERS([$1],[AS_TR_CPP([HAVE_$1])=1],,[-])
])

dnl #
dnl # AC_KERNEL_DO_BACKGROUND
dnl # $1: contents to be executed
dnl #
AC_DEFUN([AC_KERNEL_DO_BACKGROUND], [
	do_background() {
		AC_KERNEL_TMP_BUILD_DIR([$1])
	}

	AC_CHECK_PROG(NPROC, nproc, yes)
	AS_IF([test x"$NPROC" != x"yes"], [
		ncpu=1
	], [
		ncpu=$(nproc)
	])

	while [[ $(jobs | wc -l) -gt $ncpu ]]
	do
		sleep 0.1
	done

	do_background &
	procs="$! $procs"
])

dnl #
dnl # AC_KERNEL_WAIT
dnl # wait for all tests to be finished
dnl #
AC_DEFUN([AC_KERNEL_WAIT], [
	AC_MSG_CHECKING([for module configuration])
	wait $procs
	AS_IF([[[ $? -eq 0 ]]], [
		AC_MSG_RESULT([done])
	], [
		AC_MSG_RESULT([failed])
	])
])

dnl #
dnl # AC_KERNEL_SUPPORTED_AMD_CHIPS
dnl # get list of graphics chips supported by the amdgpu kernel driver
dnl #
AC_DEFUN([AC_KERNEL_SUPPORTED_AMD_CHIPS], [
	AC_MSG_CHECKING([for supported chips])
	AS_IF([test $HAVE_DRM_AMD_ASIC_TYPE_H], [
		chips=$(awk 'BEGIN {enum = 0} {
			if ($[0] ~ "^enum amd_asic_type")
				enum = 1;
			if (enum && $[1] ~ "CHIP_") {
				gsub(",", "");
				if ($[1] == "CHIP_LAST")
					exit;
				print $[1];
			}
		}' ../../include/drm/amd_asic_type.h)

		for i in $chips; do
			$as_echo "#define HAVE_$i" >>config/config-amd-chips.h
		done
		AC_MSG_RESULT([done])
	], [
		AC_MSG_RESULT([failed])
	])
])
