#ifndef AMDKCL_FENCE_BACKPORT_H
#define AMDKCL_FENCE_BACKPORT_H
#include <kcl/kcl_fence.h>

/*
 * commit v4.18-rc2-533-g418cc6ca0607
 * dma-fence: Allow wait_any_timeout for all fences)
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
#define dma_fence_wait_any_timeout _kcl_fence_wait_any_timeout
#endif
#endif
