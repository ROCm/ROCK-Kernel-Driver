dnl #
dnl # commit b8b5342b699b9b3d1b3455861a68b96424146959
dnl # drm: Consolidate plane arrays in drm_atomic_state
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_STATE_PLANE_STATES],
	[AC_MSG_CHECKING([whether drm_atomic_state->plane_states is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_crtc.h>
	],[
		struct drm_atomic_state *ptest = NULL;
		ptest->plane_states = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_ATOMIC_STATE_PLANE_STATES, 1, [drm_atomic_state->plane_states is available])
	],[
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic.h>
		],[
			struct drm_atomic_state *ptest = NULL;
			ptest->plane_states = NULL;
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_DRM_ATOMIC_STATE_PLANE_STATES, 1, [drm_atomic_state->plane_states is available])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
