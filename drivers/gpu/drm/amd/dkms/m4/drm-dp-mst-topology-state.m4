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
                        #include <drm/display/drm_dp_mst_helper.h>
                ], [
                        struct drm_dp_mst_topology_state * mst_state = NULL;
                        mst_state->pbn_div = 0;
                ], [
                        AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_STATE_PBN_DIV_INT, 1,
                                [struct drm_dp_mst_topology_state has member pbn_div])
		])
        ])
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE([
                        #include <drm/display/drm_dp_mst_helper.h>
                ], [
                        struct drm_dp_mst_topology_state * mst_state = NULL;
			fixed20_12 pbn_div;
                        pbn_div = mst_state->pbn_div;
                ], [
                        AC_DEFINE(HAVE_DRM_DP_MST_TOPOLOGY_STATE_PBN_DIV_UNION, 1,
                                [struct drm_dp_mst_topology_state has union member pbn_div])
                ])
        ])
])

dnl #
dnl # commit v6.9-rc6-1554-g8a0a7b98d4b6
dnl # drm/mst: Fix NULL pointer dereference at drm_dp_add_payload_part2
dnl #
dnl # commit v5.19-rc6-1771-g4d07b0bc4034
dnl # drm/display/dp_mst: Move all payload info into the atomic state
dnl #
AC_DEFUN([AC_AMDGPU_DRM_DP_ADD_PAYLOAD_PART2_THREE_ARGUMENTS], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <drm/display/drm_dp_mst_helper.h>
		], [
		    int a = 0;
			a = drm_dp_add_payload_part2(NULL, NULL, NULL);
		], [
			AC_DEFINE(HAVE_DRM_DP_ADD_PAYLOAD_PART2_THREE_ARGUMENTS, 1,
				[drm_dp_add_payload_part2 has three arguments])
		])
	])
])
