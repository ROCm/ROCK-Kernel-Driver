dnl #
dnl # v6.2-4472-g55970ce50152
dnl # drm/dp_mst: Clear MSG_RDY flag before sending new message
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_HPD_IRQ_HANDLE_EVENT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
		#include <drm/display/drm_dp_mst_helper.h>
		], [
			drm_dp_mst_hpd_irq_handle_event(NULL, NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_HPD_IRQ_HANDLE_EVENT, 1,
				[drm_dp_mst_hpd_irq_handle_event() is available])
		])
	])
])
