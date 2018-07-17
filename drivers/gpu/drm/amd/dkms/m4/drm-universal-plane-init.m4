dnl #
dnl # 4.14 API
dnl # commit e6fc3b68558e4c6d8d160b5daf2511b99afa8814
dnl # drm: Plumb modifiers through plane init
dnl #
AC_DEFUN([AC_AMDGPU_NUM_ARGS_DRM_UNIVERSAL_PLANE_INIT], [
	AC_MSG_CHECKING([whether drm_universal_plane_init() wants 9 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/types.h>
		#include <drm/drm_plane_helper.h>
	], [
		struct drm_device *dev = NULL;
		struct drm_plane *plane = NULL;
		uint32_t possible_crtcs = 0;
		const struct drm_plane_funcs *funcs = NULL;
		const uint32_t *formats = NULL;
		unsigned int format_count = 0;
		const uint64_t *format_modifiers = 0;
		enum drm_plane_type type = DRM_PLANE_TYPE_PRIMARY;
		const char *name = NULL;
		int error;

		error = drm_universal_plane_init(dev, plane, possible_crtcs,
			funcs, formats, format_count, format_modifiers,
			type, name);
	], [drm_universal_plane_init], [drivers/gpu/drm/drm_plane.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_9ARGS_DRM_UNIVERSAL_PLANE_INIT, 1, [drm_universal_plane_init() wants 9 args])
	], [
		dnl #
		dnl # 4.5 API
		dnl # commit 43968d7b806d7a7e021261294c583a216fddf0e5
		dnl # drm: Extract drm_plane.[hc]
		dnl #
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether drm_universal_plane_init() wants 8 args])
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/types.h>
			#include <drm/drm_plane_helper.h>
		], [
			struct drm_device *dev = NULL;
			struct drm_plane *plane = NULL;
			uint32_t possible_crtcs = 0;
			const struct drm_plane_funcs *funcs = NULL;
			const uint32_t *formats = NULL;
			unsigned int format_count = 0;
			enum drm_plane_type type = DRM_PLANE_TYPE_PRIMARY;
			const char *name = NULL;
			int error;

			error = drm_universal_plane_init(dev, plane, possible_crtcs,
				funcs, formats, format_count, type, name);
		], [drm_universal_plane_init], [drivers/gpu/drm/drm_plane.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_8ARGS_DRM_UNIVERSAL_PLANE_INIT, 1, [drm_universal_plane_init() wants 8 args])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
