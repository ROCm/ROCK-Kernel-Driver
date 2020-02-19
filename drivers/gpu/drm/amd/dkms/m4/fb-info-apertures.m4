dnl #
dnl # commit 1471ca9aa71cd37b6a7476bb6f06a3a8622ea1bd
dnl # fbdev: allow passing more than one aperture for handoff
dnl #
AC_DEFUN([AC_AMDGPU_FB_INFO_APERTURES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/fb.h>
		],[
			struct fb_info *pfi = NULL;
			pfi->apertures = pfi->apertures;
		],[
			AC_DEFINE(HAVE_FB_INFO_APERTURES, 1,
				[fb_info_apertures() is available])
		])
	])
])
