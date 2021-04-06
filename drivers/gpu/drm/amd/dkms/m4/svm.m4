dnl #
dnl # v5.6-rc3-15-g800bb1c8dc80 mm: handle multiple owners of device private pages in migrate_vma
dnl # v5.6-rc3-14-gf894ddd5ff01 memremap: add an owner field to struct dev_pagemap
dnl #
AC_DEFUN([AC_AMDGPU_HSA_AMD_SVM], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/memremap.h>
			#if !IS_ENABLED(CONFIG_DEVICE_PRIVATE)
			#error "DEVICE_PRIVATE is a must for svm support"
			#endif
		], [
			struct dev_pagemap *pm = NULL;
			pm->owner = NULL;
		], [
			AC_DEFINE(HAVE_HSA_AMD_SVM_ENABLED, 1,
				[dev_pagemap->owner is available])
		])
	])
])

