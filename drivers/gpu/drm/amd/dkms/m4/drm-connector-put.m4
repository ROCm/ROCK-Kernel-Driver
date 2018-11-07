dnl #
dnl # commit ad09360750afa18a0a0ce0253d6ea6033abc22e7
dnl # drm: Introduce drm_connector_{get,put}()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CONNECTOR_PUT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_crtc.h>
		],[
			drm_connector_put(NULL);
		],[
			AC_DEFINE(HAVE_DRM_CONNECTOR_PUT, 1,
				[drm_connector_put() is available])
		],[
			dnl #
			dnl # commit d0f37cf62979e65558c1b7bd4d4c221c5281bae1
			dnl # drm/mode: move framebuffer reference into object
			dnl #
			AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_crtc.h>
			],[
				struct drm_mode_object *obj = NULL;
				obj->free_cb = NULL;
			],[
				AC_DEFINE(HAVE_FREE_CB_IN_STRUCT_DRM_MODE_OBJECT,1,
					[drm_mode_object->free_cb is available])
			])
		])
	])
])
