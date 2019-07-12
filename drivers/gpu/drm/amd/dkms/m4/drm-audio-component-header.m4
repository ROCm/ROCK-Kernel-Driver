AC_DEFUN([AC_AMDGPU_DRM_AUDIO_COMPONENT_HEADER], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TEST_HEADER_FILE_EXIST([drm/drm_audio_component.h], [
			AC_DEFINE(HAVE_DRM_AUDIO_COMPONENT_HEADER, 1,
				[whether drm/drm_audio_component.h is defined])
		])
	])
])

