dnl #
dnl # commit v4.6-rc3-385-gb837ba0ad95b
dnl # drm/atomic: Rename drm_atomic_async_commit to nonblocking.
dnl #
AC_DEFUN([AC_AMDGPU_DRM_ATOMIC_NONBLOCKING_COMMIT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_atomic.h>
		], [
			int i;
			i = drm_atomic_nonblocking_commit(NULL);
		], [
			AC_DEFINE(HAVE_DRM_ATOMIC_NONBLOCKING_COMMIT, 1,
				[drm_atomic_nonblocking_commit() is available])
		])
	])
])
