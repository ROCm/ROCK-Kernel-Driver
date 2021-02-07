dnl #
dnl # commit v5.3-rc1-541-g35616a4aa919
dnl # fbdev: drop res_id parameter from remove_conflicting_pci_framebuffers
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#ifdef HAVE_DRM_DRMP_H
			struct vm_area_struct;
			#include <drm/drmP.h>
			#endif
			#include <drm/drm_fb_helper.h>
		], [
			drm_fb_helper_remove_conflicting_pci_framebuffers(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PP, 1,
				[drm_fb_helper_remove_conflicting_pci_framebuffers() wants p,p args])
			AC_DEFINE(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS, 1,
				[drm_fb_helper_remove_conflicting_pci_framebuffers() is available])
		], [
			dnl #
			dnl # commit v4.19-rc1-110-g4d18975c78f2
			dnl # Author: Michał Mirosław <mirq-linux@rere.qmqm.pl>
			dnl # Date:   Sat Sep 1 16:08:45 2018 +0200
			dnl # fbdev: add remove_conflicting_pci_framebuffers()
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#ifdef HAVE_DRM_DRMP_H
				struct vm_area_struct;
				#include <drm/drmP.h>
				#endif
				#include <drm/drm_fb_helper.h>
			], [
				drm_fb_helper_remove_conflicting_pci_framebuffers(NULL, 0, NULL);
			], [
				AC_DEFINE(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_PIP, 1,
					[drm_fb_helper_remove_conflicting_pci_framebuffers() wants p,i,p args])
				AC_DEFINE(HAVE_DRM_FB_HELPER_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS, 1,
					[drm_fb_helper_remove_conflicting_pci_framebuffers() is available])
			], [
				dnl #
				dnl # commit 46eeb2c144956e88197439b5ee5cf221a91b0a81
				dnl # video/fb: Propagate error code from failing to unregister conflicting fb
				dnl #
				AC_KERNEL_TRY_COMPILE_SYMBOL([
					#include <linux/fb.h>
				], [
					int ret = remove_conflicting_framebuffers(NULL, NULL, false);
				], [remove_conflicting_framebuffers], [drivers/video/fbdev/core/fbmem.c], [
					AC_DEFINE(HAVE_REMOVE_CONFLICTING_FRAMEBUFFERS_RETURNS_INT, 1,
						[remove_conflicting_framebuffers() returns int])
				])
			])
		])
	])
])
