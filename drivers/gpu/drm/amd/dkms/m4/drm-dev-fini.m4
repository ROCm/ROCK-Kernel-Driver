#dnl
#dnl v4.10-rc5-1045-gf30c92576af4
#dnl drm: Provide a driver hook for drm_dev_release()
#dnl
AC_DEFUN([AC_AMDGPU_DRM_DRIVER_RELEASE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT(
			[drm_dev_fini],
			[drivers/gpu/drm/drm_drv.c],
			[AC_DEFINE(HAVE_DRM_DRIVER_RELEASE, 1,
				[drm_dev_fini() is available])]
		)
	])
])
