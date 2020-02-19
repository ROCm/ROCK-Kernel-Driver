dnl #
dnl # commit 2c1296d92ac03
dnl # iommu: Add iommu_get_domain_for_dev function
dnl #
AC_DEFUN([AC_AMDGPU_IOMMU_GET_DOMAIN_FOR_DEV], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/iommu.h>
		], [
			iommu_get_domain_for_dev(NULL);
		], [iommu_get_domain_for_dev],[drivers/iommu/iommu.c],[
			AC_DEFINE(HAVE_IOMMU_GET_DOMAIN_FOR_DEV, 1,
				[iommu_get_domain_for_dev() is available])
		])
	])
])
