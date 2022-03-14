dnl #
dnl # v5.7-rc1-518-gab15d56e27be drm: remove transient drm_gem_object_put_unlocked()
dnl # v5.7-rc1-491-geecd7fd8bf58 drm/gem: add _locked suffix to drm_gem_object_put
dnl # v5.7-rc1-490-gb5d250744ccc drm/gem: fold drm_gem_object_put_unlocked and __drm_gem_object_put()
dnl #
AC_DEFUN([AC_AMDGPU_DRM_GEM_OBJECT_PUT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_gem.h>
		], [
			drm_gem_object_put(NULL);
		], [
			AC_DEFINE(HAVE_DRM_GEM_OBJECT_PUT, 1,
				[drm_gem_object_put() is available])

			AC_KERNEL_CHECK_SYMBOL_EXPORT([drm_gem_object_put],
			[drivers/gpu/drm/drm_gem.c], [
				AC_DEFINE(HAVE_DRM_GEM_OBJECT_PUT_SYMBOL, 1,
					[drm_gem_object_put() is exported])
			])
		])
	])
])
