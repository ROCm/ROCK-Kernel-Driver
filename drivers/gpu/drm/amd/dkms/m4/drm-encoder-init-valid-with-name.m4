dnl #
dnl # 4.5 API change,
dnl # The function drm_encoder_init now must be passed a name.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ENCODER_INIT_VALID_WITH_NAME],
	[AC_MSG_CHECKING([whether drm_encoder_init() wants name])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/kernel.h>
		#ifndef SIZE_MAX
		#define SIZE_MAX (~0UL)
		#endif
		#include <drm/drm_encoder_slave.h>
	],[
		struct drm_device *dev = NULL;
		struct drm_encoder *encoder = NULL;
		const struct drm_encoder_funcs *funcs = NULL;
		int encoder_type = 0;
		const char *name = NULL;
		int error;

		error = drm_encoder_init(dev, encoder, funcs,
			encoder_type, name);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ENCODER_INIT_VALID_WITH_NAME, 1, [drm_encoder_init() wants name])
	],[
		AC_MSG_RESULT(no)
	])
])
