dnl #
dnl # commit v4.7-rc5-1465-g34a67dd7f33f
dnl # drm: Extract&Document drm_irq.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_IRQ_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_irq.h], [
		AC_DEFINE(HAVE_DRM_IRQ_H, 1, [drm/drm_irq.h is available])
	])
])

dnl #
dnl # commit v4.8-rc2-342-g522171951761
dnl # drm: Extract drm_connector.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_connector.h], [
		AC_DEFINE(HAVE_DRM_CONNECTOR_H, 1,
			[drm/drm_connector.h is available])
	])
])

dnl #
dnl # commit v4.8-rc2-384-g321a95ae35f2
dnl # drm: Extract drm_encoder.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ENCODER_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_encoder.h], [
		AC_DEFINE(HAVE_DRM_ENCODER_H, 1,
			[drm/drm_encoder.h is available])
	])
])

dnl #
dnl # v4.8-rc2-798-g43968d7b806d
dnl # drm: Extract drm_plane.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PLANE_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_plane.h], [
		AC_DEFINE(HAVE_DRM_PLANE_H, 1, [drm/drm_plane.h is available])
	])
])

dnl #
dnl # commit 1e53724100df15bb83e614879fedbc4914e9f3a1
dnl # Subject: drm/amdgpu: Redo XGMI reset synchronization.
dnl #
AC_DEFUN([AC_AMDGPU_TASK_BARRIER_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/task_barrier.h], [
		AC_DEFINE(HAVE_TASK_BARRIER_H, 1,
			[include/drm/task_barrier.h is available])
	])
])

dnl #
dnl # commit a8f8b1d9b8701465f1309d551fba2ebda6760f49
dnl # drm: Extract drm_file.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FILE_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_file.h],[
		AC_DEFINE(HAVE_DRM_FILE_H, 1, [drm/drm_file.h is available])
	])
])

dnl #
dnl # commit f3804203306e098dae9ca51540fcd5eb700d7f40
dnl # array_index_nospec: Sanitize speculative array de-references
dnl #
AC_DEFUN([AC_AMDGPU_DRM_AUTH_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_auth.h], [
		AC_DEFINE(HAVE_DRM_AUTH_H, 1, [drm/drm_auth.h is available])
	])
])

dnl #
dnl # commit d8187177b0b195368699ba12b5fa8cd5fdc39b79
dnl # drm: add helper for printing to log or seq_file
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PRINT_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_print.h], [
		AC_DEFINE(HAVE_DRM_PRINT_H, 1, [drm/drm_print.h is available])
	])
])

dnl #
dnl # commit 72fdb40c1a4b48f5fa6f6083ea7419b94639ed57
dnl # drm: extract drm_atomic_uapi.c
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_UAPI_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_atomic_uapi.h], [
		AC_DEFINE(HAVE_DRM_ATOMIC_UAPI_HEADER, 1,
			[drm/drm_atomic_uapi.h is available])
	])
])

dnl #
dnl # commit d78aa650670d2257099469c344d4d147a43652d9
dnl # drm: Add drm/drm_util.h header file
dnl #
dnl # commit e9eafcb589213395232084a2378e2e90f67feb29
dnl # drm: move drm_can_sleep() to drm_util.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_UTIL_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_util.h],[
		AC_DEFINE(HAVE_DRM_UTIL_H, 1, [drm/drm_util.h is available])
	])
])

dnl #
dnl # commit v5.0-rc1-342-gfcd70cd36b9b
dnl # drm: Split out drm_probe_helper.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PROBE_HELPER_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_probe_helper.h], [
		AC_DEFINE(HAVE_DRM_PROBE_HELPER_H, 1,
			[drm/drm_probe_helper.h is available])
	])
])

dnl #
dnl # drm: Extract drm_drv.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DRV_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_drv.h],[
		AC_DEFINE(HAVE_DRM_DRV_H, 1, [drm/drm_drv.h is available])
	])
])

dnl #
dnl # commit e4672e55d6f3428ae9f27542e05c891f2af71051
dnl # drm: Extract drm_device.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEVICE_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_device.h],[
		AC_DEFINE(HAVE_DRM_DEVICE_H, 1, [drm/drm_device.h is available])
	])
])

dnl #
dnl # commit 4e98f871bcffa322850c73d22c66bbd7af2a0374
dnl # drm: delete drmP.h + drm_os_linux.h
dnl #
AC_DEFUN([AC_AMDGPU_DRMP_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drmP.h], [
		AC_DEFINE(HAVE_DRMP_H, 1, [include/drm/drmP.h is available])
	])
])

dnl #
dnl # commit v4.12-rc1-158-g3ed4351a83ca
dnl # drm: Extract drm_vblank.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_VBLANK_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_vblank.h], [
		AC_DEFINE(HAVE_DRM_VBLANK_H, 1, [drm/drm_vblank.h is available])
	])
])

dnl #
dnl # commit v4.11-rc3-927-g7cfdf711ffb0
dnl # drm: Extract drm_ioctl.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_IOCTL_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_ioctl.h], [
		AC_DEFINE(HAVE_DRM_IOCTL_H, 1, [drm/drm_ioctl.h is available])
	])
])

dnl #
dnl # commit v4.11-rc3-918-g4834442d70be
dnl # drm: Extract drm_debugfs.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DEBUGFS_H], [
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_debugfs.h], [
		AC_DEFINE(HAVE_DRM_DEBUGFS_H, 1,
			[drm/drm_debugfs.h is available])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_HEADERS], [
	AC_AMDGPU_DRM_IRQ_H
	AC_AMDGPU_DRM_CONNECTOR_H
	AC_AMDGPU_DRM_ENCODER_H
	AC_AMDGPU_DRM_PLANE_H
	AC_AMDGPU_TASK_BARRIER_H
	AC_AMDGPU_DRM_FILE_H
	AC_AMDGPU_DRM_AUTH_H
	AC_AMDGPU_DRM_PRINT_H
	AC_AMDGPU_DRM_ATOMIC_UAPI_H
	AC_AMDGPU_DRM_UTIL_H
	AC_AMDGPU_DRM_PROBE_HELPER_H
	AC_AMDGPU_DRM_DRV_H
	AC_AMDGPU_DRM_DEVICE_H
	AC_AMDGPU_DRMP_H
	AC_AMDGPU_DRM_VBLANK_H
	AC_AMDGPU_DRM_IOCTL_H
	AC_AMDGPU_DRM_DEBUGFS_H
])
