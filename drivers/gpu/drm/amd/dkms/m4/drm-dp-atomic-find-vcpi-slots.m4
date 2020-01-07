dnl #
dnl # commit dad1c2499a8f6d7ee01db8148f05ebba73cc41bd
dnl # drm/dp_mst: Manually overwrite PBN divider for calculating timeslots
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ATOMIC_FIND_VCPI_SLOTS],
	[AC_MSG_CHECKING([whether drm_dp_atomic_find_vcpi_slots() wants five arguments])
	AC_KERNEL_TRY_COMPILE([
		#include <drm/drm_dp_mst_helper.h>
	], [
		drm_dp_atomic_find_vcpi_slots(NULL, NULL, NULL, 0, 0);
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS_5ARGS, 1, [drm_dp_atomic_find_vcpi_slots() wants 5args])
	], [
		AC_MSG_RESULT(no)
	])
])
