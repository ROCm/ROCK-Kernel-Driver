dnl #
dnl # commit edb1ed1ab7d314e114de84003f763da34c0f34c0
dnl # drm/dp: Add DP MST helpers to atomically find and release vcpi slots
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_FIND_VCPI_SLOTS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#if defined(HAVE_DRM_DISPLAY_DRM_DP_MST_HELPER_H)
			#include <drm/display/drm_dp_mst_helper.h>
			#elif defined(HAVE_DRM_DP_DRM_DP_MST_HELPER_H)
			#include <drm/dp/drm_dp_mst_helper.h>
			#else
			#include <drm/drm_dp_mst_helper.h>
			#endif
		], [
			int retval;
			retval = drm_dp_atomic_find_vcpi_slots(NULL, NULL, NULL, 0, 0);
		], [
			AC_DEFINE(HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS_5ARGS, 1,
				[drm_dp_atomic_find_vcpi_slots() wants 5args])
		])
	])
])

dnl #
dnl # commit v6.1-rc1~27-df78f7f660cd
dnl # drm/display/dp_mst: Call them time slots, not VCPI slots
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_FIND_TIME_SLOTS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#if defined(HAVE_DRM_DISPLAY_DRM_DP_MST_HELPER_H)
			#include <drm/display/drm_dp_mst_helper.h>
			#elif defined(HAVE_DRM_DP_DRM_DP_MST_HELPER_H)
			#include <drm/dp/drm_dp_mst_helper.h>
			#else
			#include <drm/drm_dp_mst_helper.h>
			#endif
		],[
			int ret;
			ret = drm_dp_atomic_find_time_slots(NULL, NULL, NULL, 0);
		],[
			AC_DEFINE(HAVE_DRM_DP_ATOMIC_FIND_TIME_SLOTS, 1,
				[drm_dp_atomic_find_time_slots() is available])
		])
	])
])

dnl #
dnl # commit v6.1-rc1~27-a5c2c0d164e9
dnl # drm/display/dp_mst: Add nonblocking helpers for DP MST
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_SETUP_COMMIT], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		],[
			int ret;
			ret = drm_dp_mst_atomic_setup_commit(NULL);
		],[
			AC_DEFINE(HAVE_DRM_DP_ATOMIC_SETUP_COMMIT, 1,
				[drm_dp_mst_atomic_setup_commit() is available])
		])
	])
])

drm_dp_mst_atomic_wait_for_dependencies

dnl #
dnl # commit v6.1-rc1~27-a5c2c0d164e9
dnl # drm/display/dp_mst: Add nonblocking helpers for DP MST
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_WAIT_FOR_DEPENDENCIES], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		],[
			int ret;
			ret = drm_dp_mst_atomic_wait_for_dependencies(NULL);
		],[
			AC_DEFINE(HAVE_DRM_DP_ATOMIC_WAIT_FOR_DEPENDENCIES, 1,
				[drm_dp_mst_atomic_wait_for_dependencies() is available])
		])
	])
])

dnl #
dnl # commit v6.1-rc1~27-a5c2c0d164e9
dnl # drm/display/dp_mst: Add nonblocking helpers for DP MST
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_ROOT_CONN_ATOMIC_CHECK], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		],[
			int ret;
			ret = drm_dp_mst_root_conn_atomic_check(NULL, NULL);
		],[
			AC_DEFINE(HAVE_DRM_DP_MST_ROOT_CONN_ATOMIC_CHECK, 1,
				[drm_dp_mst_root_conn_atomic_check() is available])
		])
	])
])

dnl #
dnl # commit v6.1-rc1~27-df78f7f660cd
dnl # drm/display/dp_mst: Call them time slots, not VCPI slots
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_RELEASE_TIME_SLOTS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		],[
			int ret;
			ret = drm_dp_atomic_release_time_slots(NULL, NULL, NULL);
		],[
			AC_DEFINE(HAVE_DRM_DP_ATOMIC_RELEASE_TIME_SLOTS, 1,
				[drm_dp_atomic_release_time_slots() is available])
		])
	])
])

AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_FUNCS], [
	AC_AMDGPU_DRM_DP_ATOMIC_FIND_VCPI_SLOTS
	AC_AMDGPU_DRM_DP_ATOMIC_FIND_TIME_SLOTS
	AC_AMDGPU_DRM_DP_ATOMIC_SETUP_COMMIT
	AC_AMDGPU_DRM_DP_ATOMIC_WAIT_FOR_DEPENDENCIES
	AC_AMDGPU_DRM_DP_MST_ROOT_CONN_ATOMIC_CHECK
	AC_AMDGPU_DRM_DP_ATOMIC_RELEASE_TIME_SLOTS
])
