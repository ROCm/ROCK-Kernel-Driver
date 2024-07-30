dnl #
dnl # commit v5.18-rc5-1218-g6537f79a2aae
dnl # drm/edid: add new interfaces around struct drm_edid
dnl #
AC_DEFUN([AC_AMDGPU_DRM_EDID_MALLOC], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_edid.h>
		],[
			drm_edid_alloc(NULL, 0);
		],[
			AC_DEFINE(HAVE_DRM_EDID_MALLOC, 1,
				[drm_edid_alloc() is available])
		])
	])
])

dnl #
dnl # commit v5.19-rc2-380-g3d1ab66e043f
dnl # drm/edid: add drm_edid_raw() to access the raw EDID data
dnl #
AC_DEFUN([AC_AMDGPU_DRM_EDID_RAW], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_edid.h>
		],[
			drm_edid_raw(NULL);
		],[
			AC_DEFINE(HAVE_DRM_EDID_RAW, 1,
				[drm_edid_raw() is available])
		])
	])
])

dnl #
dnl # commit v6.1-rc1-145-g6c9b3db70aad
dnl # drm/edid: add function for checking drm_edid validity
dnl #
AC_DEFUN([AC_AMDGPU_DRM_EDID_VALID], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_edid.h>
		],[
			drm_edid_valid(NULL);
		],[
			AC_DEFINE(HAVE_DRM_EDID_VALID, 1,
				[drm_edid_valid() is available])
		])
	])
])


AC_DEFUN([AC_AMDGPU_STRUCT_DRM_EDID], [
	AC_AMDGPU_DRM_EDID_MALLOC
	AC_AMDGPU_DRM_EDID_RAW
	AC_AMDGPU_DRM_EDID_VALID
])