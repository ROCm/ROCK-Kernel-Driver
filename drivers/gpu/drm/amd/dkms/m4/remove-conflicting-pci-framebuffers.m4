dnl #
dnl # v5.3-rc1-540-g0a8459693238
dnl # fbdev: drop res_id parameter from remove_conflicting_pci_framebuffers
dnl #
AC_DEFUN([AC_AMDGPU_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/fb.h>
		],[
			remove_conflicting_pci_framebuffers(NULL, NULL);
		],[
			AC_DEFINE(HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_NO_RES_ID_ARG, 1,
				[remove_conflicting_pci_framebuffers() is available and doesn't have res_id arg])
		],[
                	AC_KERNEL_TRY_COMPILE([
                        	#include <linux/fb.h>
                	], [
                        	remove_conflicting_pci_framebuffers(NULL, 0, NULL);
                	], [
                        	AC_DEFINE(HAVE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_WITH_RES_ID_ARG, 1,
                                	[remove_conflicting_pci_framebuffers() is available and has res_id arg])
                	])
		])
	])
])

