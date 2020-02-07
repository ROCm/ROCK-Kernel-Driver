dnl #
dnl # commit v2.6.32-rc6-131-gc8e0f93a381d
dnl # drm/i915: Replace a calloc followed by copying data over it with malloc.
dnl #
dnl # commit v4.12-rc1-110-gc4f51dc87296
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MALLOC_AB], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
			], [
				drm_malloc_ab(0, 0);
			], [
				AC_DEFINE(HAVE_DRM_MALLOC_AB, 1,
					[drm_malloc_ab() is available])
			])
		])
	])
])
