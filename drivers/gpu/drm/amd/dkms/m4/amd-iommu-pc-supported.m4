dnl #
dnl # commit 30861ddc9cca479a7fc6a5efef4e5c69d6b274f4
dnl # perf/x86/amd: Add IOMMU Performance Counter resource management
dnl #
AC_DEFUN([AC_AMDGPU_AMD_IOMMU_PC_SUPPORTED], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <generated/autoconf.h>
		], [
			#ifndef CONFIG_AMD_IOMMU
			#error CONFIG_AMD_IOMMU not enabled
			#endif
		], [amd_iommu_pc_supported], [drivers/iommu/amd_iommu_init.c], [
			AC_DEFINE(HAVE_AMD_IOMMU_PC_SUPPORTED, 1,
				[amd_iommu_pc_supported() is available])
		])
	])
])
