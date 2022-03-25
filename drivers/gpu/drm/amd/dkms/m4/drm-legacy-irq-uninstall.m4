dnl #
dnl # commit c1736b9008cb06a95231410145d0b9d2709ec86f
dnl # drm: IRQ midlayer is now legacy
dnl #
AC_DEFUN([AC_AMDGPU_DRM_LEGACY_IRQ_UNINSTALL], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_legacy.h>
		],[
			drm_legacy_irq_uninstall(NULL);
		],[
			AC_DEFINE(HAVE_DRM_LEGACY_IRQ_UNINSTALL, 1,
				[drm_legacy_irq_uninstall() is available])
		])
	])
])

