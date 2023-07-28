dnl #
dnl # commit v4.11-rc7-1869-g3f3353b7e121
dnl # drm/dp: Introduce MST topology state to track available link bandwidth
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_STATE_TOTAL_AVAIL_SLOTS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		], [
			struct drm_dp_mst_topology_state * mst_state = NULL;
			mst_state->total_avail_slots = 0;
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_STATE_TOTAL_AVAIL_SLOTS, 1,
				[struct drm_dp_mst_topology_state has member total_avail_slots])
		])
	])
])


dnl #
dnl # commit 8366f01fb15a54281c193658d1a916f6f2d5eb1e
dnl # drm/display/dp_mst: Move all payload info into the atomic state
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_STATE_PAYLOADS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/drm_dp_mst_helper.h>
		], [
			struct drm_dp_mst_topology_state * mst_state = NULL;
			struct list_head payloads;
			payloads = mst_state->payloads;
		], [
			AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_STATE_PAYLOADS, 1,
				[struct drm_dp_mst_topology_state has member payloads])
		])
	])
])


dnl #
dnl # commit v5.19-rc6-1771-g4d07b0bc4034
dnl # drm/display/dp_mst: Move all payload info into the atomic state
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_MST_TOPOLOGY_STATE_PBN_DIV], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/drm_dp_mst_helper.h>
                ], [
                        struct drm_dp_mst_topology_state * mst_state = NULL;
                        int pbn_div;
                        pbn_div = mst_state->pbn_div;
                ], [
                        AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_STATE_PBN_DIV, 1,
                                [struct drm_dp_mst_topology_state has member pbn_div])
                ])
        ])
])

