dnl #
dnl # v6.5-2548-g2d4457c2d03e
dnl # drm/amd/display: Add 3x4 CTM support for plane CTM
dnl #
AC_DEFUN([AC_AMDGPU_DRM_COLOR_CTM_3X4], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                       #include <uapi/drm/drm_mode.h>
                ],[
			struct drm_color_ctm_3x4 *ctm = NULL;
                        ctm->matrix[0] = 0;
                ],[
                        AC_DEFINE(HAVE_DRM_COLOR_CTM_3X4, 1,
                                [struct drm_color_ctm_3x4 is available])
                ])
        ])
])



