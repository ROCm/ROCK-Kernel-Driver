dnl #
dnl # commit v6.9-rc2-247-gd12e36494dc2
dnl # drm/vblank: Introduce drm_crtc_vblank_crtc()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_CRTC_VBLANK_CRTC], [
    AC_KERNEL_DO_BACKGROUND([
        AC_KERNEL_TRY_COMPILE([
            #include <drm/drm_vblank.h>
        ],[
            drm_crtc_vblank_crtc(NULL);
        ],[
            AC_DEFINE(HAVE_CRTC_DRM_VBLANK_CRTC, 1,
                [drm_edid_raw() is available])
        ])
    ])
])
