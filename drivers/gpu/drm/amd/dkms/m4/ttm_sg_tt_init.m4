dnl #
dnl # v4.16-rc1-1232-g75a57669cbc8
dnl # drm/ttm: add ttm_sg_tt_init
dnl #
AC_DEFUN([AC_AMDGPU_TTM_SG_TT_INIT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_CHECK_SYMBOL_EXPORT([ttm_sg_tt_init], [drivers/gpu/drm/ttm/ttm_tt.c], [
			AC_DEFINE(HAVE_TTM_SG_TT_INIT, 1, [ttm_sg_tt_init() is available])
		])
	])
])
