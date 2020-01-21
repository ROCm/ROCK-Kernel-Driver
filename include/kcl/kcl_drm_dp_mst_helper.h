#ifndef AMDKCL_DRM_DP_MST_HELPER_H
#define AMDKCL_DRM_DP_MST_HELPER_H

#include <drm/drm_dp_mst_helper.h>

#if !defined(HAVE_DRM_DP_CALC_PBN_MODE_3ARGS)
static inline
int _kcl_drm_dp_calc_pbn_mode(int clock, int bpp, bool dsc)
{
#if defined(HAVE_MUL_U32_U32)
	if (dsc)
		return DIV_ROUND_UP_ULL(mul_u32_u32(clock * (bpp / 16), 64 * 1006),
				8 * 54 * 1000 * 1000);
#endif

	return drm_dp_calc_pbn_mode(clock, bpp);
}
#define drm_dp_calc_pbn_mode _kcl_drm_dp_calc_pbn_mode
#endif

#if defined(HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS)
#if !defined(HAVE_DRM_DP_ATOMIC_FIND_VCPI_SLOTS_5ARGS)
static inline
int _kcl_drm_dp_atomic_find_vcpi_slots(struct drm_atomic_state *state,
				  struct drm_dp_mst_topology_mgr *mgr,
				  struct drm_dp_mst_port *port, int pbn,
				  int pbn_div)
{
	int pbn_backup;
	int req_slots;

	if (pbn_div > 0) {
		pbn_backup = mgr->pbn_div;
		mgr->pbn_div = pbn_div;
	}

	req_slots = drm_dp_atomic_find_vcpi_slots(state, mgr, port, pbn);

	if (pbn_div > 0)
		mgr->pbn_div = pbn_backup;

	return req_slots;
}
#define drm_dp_atomic_find_vcpi_slots _kcl_drm_dp_atomic_find_vcpi_slots
#endif
#endif

#endif
