dnl #
dnl # v5.11-20-g2d24dd5798d0
dnl # rbtree: Add generic add and find helpers
dnl #
AC_DEFUN([AC_AMDGPU_DRM_VBLANK_CRTC_CONFIG], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_vblank.h>
                ],[
                        struct drm_vblank_crtc_config config;
                ],[
                        AC_DEFINE(HAVE_DRM_VBLANK_CRTC_CONFIG, 1,
                                [drm_vblank_crtc_config is available])
                ])
        ])
])



