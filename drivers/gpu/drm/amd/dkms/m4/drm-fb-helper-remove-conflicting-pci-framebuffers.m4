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
	])
])
