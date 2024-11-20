dnl #
dnl # commit v5.13-rc3-1543-g97c9bfe3f660
dnl # drm/aperture: Pass DRM driver structure instead of driver name
dnl #
AC_DEFUN([AC_AMDGPU_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_DRM_DRIVER_ARG], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_aperture.h>
			struct drm_driver;
		], [
			const struct drm_driver *drv = NULL;
			drm_aperture_remove_conflicting_pci_framebuffers(NULL, drv);
		], [
			AC_DEFINE(HAVE_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_DRM_DRIVER_ARG, 1,
				[drm_aperture_remove_conflicting_pci_framebuffers() second arg is drm_driver*])
		])
	])
])

dnl #
dnl # v5.19-rc2-317-g7283f862bd99
dnl # drm: Implement DRM aperture helpers under video/
dnl #
AC_DEFUN([AC_AMDGPU_APERTURE_REMOVE_CONFLICTING_PCI_DEVICES], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE([
            #include <linux/aperture.h>
        ], [
            aperture_remove_conflicting_pci_devices(NULL, NULL);
        ], [
            AC_DEFINE(HAVE_APERTURE_REMOVE_CONFLICTING_PCI_DEVICES, 1,
                [aperture_remove_conflicting_pci_device() is available])
        ])
    ])
])

AC_DEFUN([AC_AMDGPU_DRM_APERTURE], [
    AC_AMDGPU_DRM_APERTURE_REMOVE_CONFLICTING_PCI_FRAMEBUFFERS_DRM_DRIVER_ARG
	AC_AMDGPU_APERTURE_REMOVE_CONFLICTING_PCI_DEVICES
])
