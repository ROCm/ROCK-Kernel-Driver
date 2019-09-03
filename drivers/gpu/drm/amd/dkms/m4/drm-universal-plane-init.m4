dnl #
dnl # 4.14 API
dnl # commit e6fc3b68558e4c6d8d160b5daf2511b99afa8814
dnl # drm: Plumb modifiers through plane init
dnl #
AC_DEFUN([AC_AMDGPU_NUM_ARGS_DRM_UNIVERSAL_PLANE_INIT], [
	AC_MSG_CHECKING([whether drm_universal_plane_init() wants 9 args])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/types.h>
		#include <drm/drm_modeset_lock.h>
		#include <drm/drm_mode.h>
		#include <drm/drm_rect.h>
		#include <drm/drm_property.h>
		#include <drm/drm_plane.h>
	], [
		drm_universal_plane_init(NULL, NULL, 0, NULL, NULL, 0, NULL, 0, NULL);
	], [drm_universal_plane_init], [drivers/gpu/drm/drm_plane.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_9ARGS_DRM_UNIVERSAL_PLANE_INIT, 1, [drm_universal_plane_init() wants 9 args])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether drm_universal_plane_init() wants 8 args and declared in drm_plane.h])
dnl #
dnl # 4.5 API
dnl # commit 43968d7b806d7a7e021261294c583a216fddf0e5
dnl # drm: Extract drm_plane.[hc]
dnl #
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <linux/types.h>
			#include <drm/drm_modeset_lock.h>
			#include <drm/drm_mode.h>
			#include <drm/drm_rect.h>
			#include <drm/drm_plane.h>
		], [
			drm_universal_plane_init(NULL, NULL, 0, NULL, NULL, 0, 0, NULL);
		], [drm_universal_plane_init], [drivers/gpu/drm/drm_plane.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_8ARGS_DRM_UNIVERSAL_PLANE_INIT, 1, [drm_universal_plane_init() wants 8 args])
		], [
			AC_MSG_RESULT(no)
			AC_MSG_CHECKING([whether drm_universal_plane_init() wants 8 args and declared in drm_crtc.h])
dnl #
dnl # commit b0b3b79511143
dnl # drm: Pass 'name' to drm_universal_plane_init()
dnl #
			AC_KERNEL_TRY_COMPILE_SYMBOL([
				#include <drm/drm_crtc.h>
			], [
				drm_universal_plane_init(NULL, NULL, 0, NULL, NULL, 0, 0, NULL);
			], [drm_universal_plane_init], [drivers/gpu/drm/drm_crtc.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_8ARGS_DRM_UNIVERSAL_PLANE_INIT, 1, [drm_universal_plane_init() wants 8 args])
			], [
				AC_MSG_RESULT(no)
			])
		])
	])
])
