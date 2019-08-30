dnl #
dnl # commit d219adc1228a3887486b58a430e736b0831f192c
dnl # fb: add hooks to handle KDB enter/exit
dnl #
AC_DEFUN([AC_AMDGPU_FB_OPS_FB_DEBUG_XX], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drmP.h>
				#include <drm/drm_fb_helper.h>
			], [
				struct fb_ops *ptest = NULL;
				ptest->fb_debug_enter = NULL;
				ptest->fb_debug_leave = NULL;
			], [
				AC_DEFINE(HAVE_FB_OPS_FB_DEBUG_XX, 1,
					[fb_ops->fb_debug_xx is available])
			])
		], [
			AC_DEFINE(HAVE_FB_OPS_FB_DEBUG_XX, 1,
				[fb_ops->fb_debug_xx is available])
		])
	])
])
