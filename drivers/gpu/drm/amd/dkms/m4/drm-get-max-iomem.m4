dnl #
dnl # 4.17 API
dnl # commit 82626363a217d79128c447ab296777b461e9f050
dnl # drm: add func to get max iomem address v2
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GET_MAX_IOMEM], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_cache.h>
		], [
			u64 addr;
			addr = drm_get_max_iomem();
		], [drm_get_max_iomem], [drivers/gpu/drm/drm_memory.c], [
			AC_DEFINE(HAVE_DRM_GET_MAX_IOMEM, 1,
				[ddrm_get_max_iome() is available])
		])
	])
])
