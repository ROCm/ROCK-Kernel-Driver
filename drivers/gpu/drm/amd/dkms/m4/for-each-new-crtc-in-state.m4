dnl # commit 581e49fe6b411f407102a7f2377648849e0fa37f
dnl # Author: Maarten Lankhorst <maarten.lankhorst@linux.intel.com
dnl # Date:   Mon Jan 16 10:37:38 2017 +0100
dnl # drm/atomic: Add new iterators over all state, v3.
dnl # Add for_each_(old)(new)_(plane,connector,crtc)_in_state iterators to
dnl # replace the old for_each_xxx_in_state ones. This is useful for >1 flip
dnl # depth and getting rid of all xxx->state dereferences.
AC_DEFUN([AC_AMDGPU_FOR_EACH_NEW_CRTC_IN_STATE],
	[AC_MSG_CHECKING([whether for_each_new_crtc_in_state() is defined])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_atomic.h>
	],[
		#if !defined(for_each_new_crtc_in_state)
		#error for_each_new_crtc_in_state not #defined
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FOR_EACH_NEW_CRTC_IN_STATE, 1, [whether for_each_new_crtc_in_state() is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
