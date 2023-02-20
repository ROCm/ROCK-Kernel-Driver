dnl #
dnl # commit v6.1-rc2-534-g7fd50bc39d12
dnl # drm/fb-helper: Rename drm_fb_helper_alloc_fbi() to use _info postfix
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_ALLOC_INFO], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_fb_helper_alloc_info], [drivers/gpu/drm/drm_fb_helper.c], [
                        AC_DEFINE(HAVE_DRM_FB_HELPER_ALLOC_INFO, 1, [drm_fb_helper_alloc_info() is available])
                ])
        ])
])

dnl #
dnl # commit v6.1-rc2-535-gafb0ff78c13c
dnl # drm/fb-helper: Rename drm_fb_helper_unregister_fbi() to use _info postfix
dnl #
AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER_UNREGISTER_INFO], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_fb_helper_unregister_info], [drivers/gpu/drm/drm_fb_helper.c], [
                        AC_DEFINE(HAVE_DRM_FB_HELPER_UNREGISTER_INFO, 1, [drm_fb_helper_unregister_info() is available])
                ])
        ])
])

AC_DEFUN([AC_AMDGPU_DRM_FB_HELPER], [
	AC_AMDGPU_DRM_FB_HELPER_ALLOC_INFO
	AC_AMDGPU_DRM_FB_HELPER_UNREGISTER_INFO
])
