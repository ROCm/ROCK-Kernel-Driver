dnl 45bd77e987d6f84a7c4433758687153e0734677a
dnl # drm/amd/display: Add MST atomic routines
dnl #
AC_DEFUN([AC_AMDGPU_STRUCT_NAME_CB_NAME_2ARGS],
	[AC_MSG_CHECKING([whether atomic_check() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_modeset_helper_vtables.h>
		#include <drm/drm_atomic.h>
	], [
		struct drm_connector_helper_funcs p;
		p->atomic_check(NULL, (struct drm_atomic_state*)NULL)
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_STRUCT_NAME_CB_NAME_2ARGS, 1, [atomic_check() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
