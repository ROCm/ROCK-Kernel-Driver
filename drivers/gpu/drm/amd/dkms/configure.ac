AC_INIT(amdgpu-dkms, 19.40)

m4_include([m4/kernel.m4])
m4_include([m4/kobj-to-dev.m4])
m4_include([m4/drm-modeset-lock-all-ctx.m4])
m4_include([m4/drm-atomic-helper-disable-all.m4])
m4_include([m4/drm-atomic-helper-duplicate-state.m4])
m4_include([m4/config.m4])

AC_LANG(C)
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_HEADERS([config/config.h])
AC_PROG_INSTALL
AC_PROG_CC

AC_AMDGPU_CONFIG

AC_OUTPUT
