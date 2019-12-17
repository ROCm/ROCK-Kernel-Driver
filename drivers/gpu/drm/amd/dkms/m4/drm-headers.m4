dnl #
dnl # commit v4.7-rc5-1465-g34a67dd7f33f
dnl # drm: Extract&Document drm_irq.h
dnl #
AC_DEFUN([AC_AMDGPU_DRM_IRQ_H],
	[AC_MSG_CHECKING([whether drm/drm_irq.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_irq.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_IRQ_H, 1, [drm/drm_irq.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # commit v4.8-rc2-342-g522171951761
dnl # drm: Extract drm_connector.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_H],
	[AC_MSG_CHECKING([whether drm/drm_connector.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_connector.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_CONNECTOR_H, 1, [drm/drm_connector.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # commit v4.8-rc2-384-g321a95ae35f2
dnl # drm: Extract drm_encoder.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ENCODER_H],
	[AC_MSG_CHECKING([whether drm/drm_encoder.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_encoder.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ENCODER_H, 1, [drm/drm_encoder.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # v4.8-rc2-798-g43968d7b806d
dnl # drm: Extract drm_plane.[hc]
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PLANE_H],
	[AC_MSG_CHECKING([whether drm/drm_plane.h is available])
	AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_plane.h], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_PLANE_H, 1, [drm/drm_plane.h is available])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([AC_AMDGPU_DRM_HEADERS], [
	AC_AMDGPU_DRM_IRQ_H
	AC_AMDGPU_DRM_CONNECTOR_H
	AC_AMDGPU_DRM_ENCODER_H
	AC_AMDGPU_DRM_PLANE_H
])
