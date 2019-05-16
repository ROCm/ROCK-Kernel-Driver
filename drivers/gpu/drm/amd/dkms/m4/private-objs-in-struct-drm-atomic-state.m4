dnl #
dnl # commit b430c27a7de3ccfb51b7e07b2dceba981df279ef
dnl # Author: Pandiyan, Dhinakaran <dhinakaran.pandiyan@intel.com>
dnl # Date:   Thu Apr 20 22:51:30 2017 -0700
dnl # drm: Add driver-private objects to atomic state
dnl #
AC_DEFUN([AC_AMDGPU_PRIVATE_OBJS_IN_STRUCTURE_DRM_ATOMIC_STATE],
	[AC_MSG_CHECKING([for private_objs field within drm_atomic_state structure])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_atomic.h>
	], [
		struct drm_atomic_state state;
		state.private_objs = NULL;
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PRIVATE_OBJS_IN_STRUCTURE_DRM_ATOMIC_STATE, 1, [drm_atomic_state structure contains private_objs field])
	], [
		AC_MSG_RESULT(no)
	])
])
