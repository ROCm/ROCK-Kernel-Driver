dnl #
dnl # commit 30861ddc9cca479a7fc6a5efef4e5c69d6b274f4
dnl # perf/x86/amd: Add IOMMU Performance Counter resource management
dnl #
AC_DEFUN([AC_AMDGPU_AMD_IOMMU_PC_SUPPORTED],
	[AC_MSG_CHECKING([whether amd_iommu_pc_supported() is available])
	AC_KERNEL_CHECK_SYMBOL_EXPORT([amd_iommu_pc_supported], [drivers/iommu/amd_iommu_init.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_AMD_IOMMU_PC_SUPPORTED, 1, [amd_iommu_pc_supported() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
