dnl #
dnl # commit v5.17-rc2-403-g2d3eec897033 
dnl # drm: Add drm_mode_init() 
dnl #
AC_DEFUN([AC_AMDGPU_DRM_MODE_INIT], [
	 AC_KERNEL_DO_BACKGROUND([
	 		AC_KERNEL_TRY_COMPILE_SYMBOL([
				 #include <drm/drm_modes.h>
			], [
			drm_mode_init(NULL, NULL);
			], [drm_mode_init], [drivers/gpu/drm/drm_modes.c], [
				AC_DEFINE(HAVE_DRM_MODE_INIT, 1,
					[drm_mode_init() is available])
			])
	])
])
