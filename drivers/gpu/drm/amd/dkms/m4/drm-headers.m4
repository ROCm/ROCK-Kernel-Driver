AC_DEFUN([AC_AMDGPU_DRM_HEADERS], [

	dnl #
	dnl # commit v4.7-rc5-1465-g34a67dd7f33f
	dnl # drm: Extract&Document drm_irq.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_irq.h])

	dnl #
	dnl # commit v4.8-rc2-342-g522171951761
	dnl # drm: Extract drm_connector.[hc]
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_connector.h])

	dnl #
	dnl # commit v4.8-rc2-384-g321a95ae35f2
	dnl # drm: Extract drm_encoder.[hc]
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_encoder.h])

	dnl #
	dnl # v4.8-rc2-798-g43968d7b806d
	dnl # drm: Extract drm_plane.[hc]
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_plane.h])

	dnl #
	dnl # commit 1e53724100df15bb83e614879fedbc4914e9f3a1
	dnl # Subject: drm/amdgpu: Redo XGMI reset synchronization.
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/task_barrier.h])

	dnl #
	dnl # commit a8f8b1d9b8701465f1309d551fba2ebda6760f49
	dnl # drm: Extract drm_file.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_file.h])

	dnl #
	dnl # commit f3804203306e098dae9ca51540fcd5eb700d7f40
	dnl # array_index_nospec: Sanitize speculative array de-references
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_auth.h])

	dnl #
	dnl # commit d8187177b0b195368699ba12b5fa8cd5fdc39b79
	dnl # drm: add helper for printing to log or seq_file
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_print.h])

	dnl #
	dnl # commit 72fdb40c1a4b48f5fa6f6083ea7419b94639ed57
	dnl # drm: extract drm_atomic_uapi.c
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_atomic_uapi.h])

	dnl #
	dnl # commit d78aa650670d2257099469c344d4d147a43652d9
	dnl # drm: Add drm/drm_util.h header file
	dnl #
	dnl # commit e9eafcb589213395232084a2378e2e90f67feb29
	dnl # drm: move drm_can_sleep() to drm_util.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_util.h])

	dnl #
	dnl # commit v5.0-rc1-342-gfcd70cd36b9b
	dnl # drm: Split out drm_probe_helper.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_probe_helper.h])

	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_drv.h])

	dnl #
	dnl # commit e4672e55d6f3428ae9f27542e05c891f2af71051
	dnl # drm: Extract drm_device.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_device.h])

	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drmP.h])

	dnl #
	dnl # commit v4.12-rc1-158-g3ed4351a83ca
	dnl # drm: Extract drm_vblank.[hc]
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_vblank.h])

	dnl #
	dnl # commit v4.11-rc3-927-g7cfdf711ffb0
	dnl # drm: Extract drm_ioctl.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_ioctl.h])

	dnl #
	dnl # Optional devices ID for amdgpu driver
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/amdgpu_pciid.h])

	dnl #
	dnl # commit v4.11-rc3-918-g4834442d70be
	dnl # drm: Extract drm_debugfs.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_debugfs.h])

	dnl #
	dnl # RHEL 7.x wrapper
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_backport.h])
])
