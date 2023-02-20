dnl #
dnl # v4.16-rc1-388-g80f690e9e3a6 drm: Add optional COLOR_ENCODING and COLOR_RANGE properties to drm_plane
dnl #
AC_DEFUN([AC_AMDGPU_DRM_PLANE_PROPERTY_COLOR_ENCODING_RANGE], [
	AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_plane_create_color_properties],
		[drivers/gpu/drm/drm_color_mgmt.c],[
		AC_DEFINE(HAVE_DRM_PLANE_PROPERTY_COLOR_ENCODING_RANGE, 1,
			[drm_plane_create_color_properties is available])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_PLANE_PROPERTY], [
	AC_KERNEL_DO_BACKGROUND([
		AC_AMDGPU_DRM_PLANE_PROPERTY_COLOR_ENCODING_RANGE
	])
])
