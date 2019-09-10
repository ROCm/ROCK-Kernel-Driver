dnl #
dnl # commit 4d18975c78f2d5c91792356501cf369e67594241
dnl # Author: Michał Mirosław <mirq-linux@rere.qmqm.pl>
dnl # Date:   Sat Sep 1 16:08:45 2018 +0200
dnl # fbdev: add remove_conflicting_pci_framebuffers()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS],
	[AC_MSG_CHECKING([whether drm_fb_helper_remove_conflicting_pci_framebuffers() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_fb_helper.h>
	],[
		drm_fb_helper_remove_conflicting_pci_framebuffers(NULL, 0, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS, 1, [drm_fb_helper_remove_conflicting_pci_framebuffers() is available])
	],[
		AC_MSG_RESULT(no)
dnl #
dnl # commit 46eeb2c144956e88197439b5ee5cf221a91b0a81
dnl # video/fb: Propagate error code from failing to unregister conflicting fb
dnl #
		AC_MSG_CHECKING([whether remove_conflicting_framebuffers() returns int])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/fb.h>
		],[
			int ret = remove_conflicting_framebuffers(NULL, NULL, false);
		],[remove_conflicting_framebuffers], [drivers/video/fbdev/core/fbmem.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_REMOVE_CONFLICTING_FRAMEBUFFERS_RETURNS_INT, 1, [kremove_conflicting_framebuffers() returns int])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
