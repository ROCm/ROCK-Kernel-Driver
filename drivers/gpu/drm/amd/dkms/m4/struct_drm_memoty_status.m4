dnl #
dnl # commit v6.4-rc1-193-g686b21b5f6ca
dnl # drm: Add fdinfo memory stats
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PRINT_MEMORY_STATS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_file.h>
			#include <drm/drm_gem.h>
		],[
			enum drm_gem_object_status res = 0;
			res |= DRM_GEM_OBJECT_RESIDENT;
			drm_print_memory_stats(NULL, NULL, res, NULL);
		],[drm_print_memory_stats], [drivers/gpu/drm/drm_file.c], [
			AC_DEFINE(HAVE_DRM_PRINT_MEMORY_STATS, 1,
				[drm_print_memory_stats() is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_FILE], [
		AC_AMDGPU_DRM_PRINT_MEMORY_STATS
])
