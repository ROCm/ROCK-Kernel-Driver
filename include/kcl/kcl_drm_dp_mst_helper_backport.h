#ifndef _KCL_DRM_DP_MST_HELPER_BACKPORT_H_
#define _KCL_DRM_DP_MST_HELPER_BACKPORT_H_

#include <drm/drm_dp_mst_helper.h>

#ifndef HAVE_DRM_DP_MST_TOPOLOGY_MGR_RESUME_2ARGS
static inline int
_kcl_drm_dp_mst_topology_mgr_resume(struct drm_dp_mst_topology_mgr *mgr,
			       bool sync)
{
	return drm_dp_mst_topology_mgr_resume(mgr);
}
#define drm_dp_mst_topology_mgr_resume _kcl_drm_dp_mst_topology_mgr_resume
#endif
#endif
