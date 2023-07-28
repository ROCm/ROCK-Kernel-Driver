dnl #
dnl # commit v4.14-rc1-a4370c7774
dnl # drm/atomic: Make private objs proper objects
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_MGR_BASE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
			#include <drm/drm_atomic.h>
		], [
			struct drm_dp_mst_topology_mgr *mst_mgr = 0;
			int i = 0;
			if ((&mst_mgr->base) && (&mst_mgr->base.lock))
				i++;
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_MGR_BASE, 1,
				[struct drm_dp_mst_topology_mgr.base is available])
		])
	])
])
