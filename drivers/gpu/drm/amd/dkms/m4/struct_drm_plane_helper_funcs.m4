dnl # commit v5.11-rc2-701-g7c11b99a8e58
dnl # drm/atomic: Pass the full state to planes atomic_check
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS_ATOMIC_CHECK_DRM_ATOMIC_STATE_PARAMS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_modeset_helper_vtables.h>
		], [
			struct drm_plane_helper_funcs *funcs = NULL;
			funcs->atomic_check(NULL, (struct drm_atomic_state *)NULL);
		], [
			AC_DEFINE(HAVE_STRUCT_DRM_PLANE_HELPER_FUNCS_ATOMIC_CHECK_DRM_ATOMIC_STATE_PARAMS, 1,
				[drm_plane_helper_funcs->atomic_check() second param wants drm_atomic_state arg])
		])
	])
])

dnl #
dnl # v6.9-rc2-213-gbf9fb17c6672
dnl # drm/panic: Add a drm panic handler
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS_GET_SCANOUT_BUFFER], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                       #include <drm/drm_modeset_helper_vtables.h>
                ],[
                        struct drm_plane_helper_funcs *ptr = NULL;
                        ptr->get_scanout_buffer(NULL, NULL);
                ],[
                        AC_DEFINE(HAVE_STRUCT_DRM_PLANE_HELPER_FUNCS_GET_SCANOUT_BUFFER, 1,
                                [struct drm_plane_helper_funcs->get_scanout_buffer is available])
                ])
        ])
])


AC_DEFUN([AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS], [
	AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS_ATOMIC_CHECK_DRM_ATOMIC_STATE_PARAMS
	AC_AMDGPU_STRUCT_DRM_PLANE_HELPER_FUNCS_GET_SCANOUT_BUFFER
])
