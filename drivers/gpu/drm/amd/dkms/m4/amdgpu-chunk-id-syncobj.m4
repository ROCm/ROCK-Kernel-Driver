dnl #
dnl # commit 660e855813f78b7fe63ff1ebc4f2ca07d94add0b
dnl # drm/amdgpu: use drm sync objects for shared semaphores (v6)
dnl # commit 5ee0b7e006b373b3df201ac8799d966c74b50d67
dnl # drm/amdgpu: add timeline support in amdgpu CS v3
dnl #
AC_DEFUN([AC_AMDGPU_CHUNK_ID_SYNCOBJ],
	[AC_MSG_CHECKING([whether AMDGPU_CHUNK_ID_SYNCOBJ_* are defined])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/amdgpu_drm.h>
	],[
		#if !defined(AMDGPU_CHUNK_ID_SYNCOBJ_IN) || \
			!defined(AMDGPU_CHUNK_ID_SYNCOBJ_OUT) || \
			!defined(AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_WAIT) || \
			!defined(AMDGPU_CHUNK_ID_SYNCOBJ_TIMELINE_SIGNAL)
		#error AMDGPU_CHUNK_ID_SYNCOBJ_* not #defined
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_AMDGPU_CHUNK_ID_SYNCOBJ, 1, [whether AMDGPU_CHUNK_ID_SYNCOBJ_* are defined])
	],[
		AC_MSG_RESULT(no)
	])
])
