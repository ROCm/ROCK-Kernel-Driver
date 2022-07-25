dnl #
dnl # commit v5.12-rc7-1495-g6cba3fe43341
dnl # drm/dp: Add backpointer to drm_device in drm_dp_aux
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_AUX_DRM_DEV], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#if defined(HAVE_DRM_DISPLAY_DRM_DP_HELPER_H)
			#include <drm/display/drm_dp_helper.h>
			#elif defined(HAVE_DRM_DP_DRM_DP_HELPER_H)
			#include <drm/dp/drm_dp_helper.h>
			#else
			#include <drm/drm_dp_helper.h>
			#endif			
		], [
			struct drm_dp_aux dda;
			dda.drm_dev = NULL;
		], [],[],[
			AC_DEFINE(HAVE_DRM_DP_AUX_DRM_DEV, 1,
				[struct drm_dp_aux has member named 'drm_dev'])
		])
	])
])
