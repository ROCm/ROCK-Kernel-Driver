dnl #
dnl # commit 2916059147ea38f76787d7b38dee883da2e9def2
dnl # drm/aperture: Add infrastructure for aperture ownership
dnl #
AC_DEFUN([AC_AMDGPU_DRM_APERTURE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_aperture.h>
		], [
			drm_aperture_remove_conflicting_pci_framebuffers(NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_APERTURE, 1,
				[drm_aperture_remove_* is availablea])
		])
	])
])
