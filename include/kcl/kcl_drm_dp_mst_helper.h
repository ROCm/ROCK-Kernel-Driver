#ifndef AMDKCL_DRM_DP_MST_HELPER_H
#define AMDKCL_DRM_DP_MST_HELPER_H

#include <drm/drm_dp_mst_helper.h>

#if !defined(HAVE_DRM_DP_CALC_PBN_MODE_3ARGS)
static inline
int _kcl_drm_dp_calc_pbn_mode(int clock, int bpp, bool dsc)
{
	if (dsc)
		return DIV_ROUND_UP_ULL(mul_u32_u32(clock * (bpp / 16), 64 * 1006),
				8 * 54 * 1000 * 1000);

	return drm_dp_calc_pbn_mode(clock, bpp);
}
#define drm_dp_calc_pbn_mode _kcl_drm_dp_calc_pbn_mode
#endif
#endif
