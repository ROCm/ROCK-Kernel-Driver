AC_DEFUN([AC_AMDGPU_2ARGS_SET_CRC_SOURCE],
		[AC_MSG_CHECKING([whether set_crc_source() wants 2 args])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drm_crtc.h>
		], [
				struct drm_crtc *crtc = NULL;
				const char *source = NULL;
				int obj;

				obj = set_crc_source(crtc, source);
		], [set_crc_source], [include/drm/drm_crtc.h], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_2ARGS_SET_CRC_SOURCE, 1, [set_crc_source() wants 2 args])
		], [
				AC_MSG_RESULT(no)
		])
])
