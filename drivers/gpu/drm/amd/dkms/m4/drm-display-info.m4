AC_DEFUN([AC_AMDGPU_DRM_DISPLAY_INFO_HDMI_SCDC_SCRAMBLING], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_connector.h>
		], [
			struct drm_display_info *display_info = NULL;
			display_info->hdmi.scdc.scrambling.low_rates = 0;
		], [
			AC_DEFINE(HAVE_DRM_DISPLAY_INFO_HDMI_SCDC_SCRAMBLING, 1,
				[display_info->hdmi.scdc.scrambling are available])
		])
	])
])
