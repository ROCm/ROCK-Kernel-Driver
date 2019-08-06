AC_DEFUN([AC_AMDGPU_2ARGS_SET_CRC_SOURCE],
		[AC_MSG_CHECKING([whether set_crc_source() wants 2 args])
		AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_crtc.h>
				int amdgpu_dm_crtc_set_crc_source(struct drm_crtc *crtc, const char *src_name);
		], [
				struct drm_crtc_funcs crtc_func;
				crtc_func.set_crc_source = amdgpu_dm_crtc_set_crc_source;
		], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_2ARGS_SET_CRC_SOURCE, 1, [set_crc_source() wants 2 args])
		], [
				AC_MSG_RESULT(no)
		])
])
