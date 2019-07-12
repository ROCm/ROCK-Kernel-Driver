AC_DEFUN([AC_AMDGPU_DRM_AUDIO_COMPONENT_HEADER],
		[AC_MSG_CHECKING([whether drm_audio_component_header is defined])
		AC_KERNEL_TRY_COMPILE([
				#include <drm/drm_audio_component.h>
		],[
				#if !defined(drm_audio_component) || \
					!defined(drm_audio_component_ops) || \
					!defined(drm_audio_component_audio_ops)
				#error drm_audio_component_header not #defined
				#endif
		],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_DRM_AUDIO_COMPONENT_HEADER, 1, [whether drm_audio_component_header is defined])
		],[
				AC_MSG_RESULT(no)
		])
])

