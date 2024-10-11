dnl #
dnl # commit v6.10-3140-ge9b641807e5e
dnl # drm: new helper: drm_gem_prime_handle_to_dmabuf()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GEM_PRIME_HANDLE_TO_DMABUF], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE_SYMBOL([
			#include <drm/drm_prime.h>
		],[
			drm_gem_prime_handle_to_dmabuf(NULL, NULL, 0, 0);
		],[drm_gem_prime_handle_to_dmabuf],[drivers/gpu/drm/drm_prime.c],[
			AC_DEFINE(HAVE_DRM_GEM_PRIME_HANDLE_TO_DMABUF, 1,
				[drm_gem_prime_handle_to_dmabuf() is available])
		])
	])
])
