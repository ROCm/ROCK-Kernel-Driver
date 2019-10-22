AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_HDMI_SCDC_SCRAMBLING], [
	AC_MSG_CHECKING([whether display_info->hdmi.scdc.scrambling are available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_connector.h>
	], [
		struct drm_display_info *display_info = NULL;
		display_info->hdmi.scdc.scrambling.low_rates = 0;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DISPLAY_INFO_HDMI_SCDC_SCRAMBLING, 1, [display_info->hdmi.scdc.scrambling are available])
	], [
		AC_MSG_RESULT(no)
	])
])
