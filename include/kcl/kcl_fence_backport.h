#ifndef AMDKCL_FENCE_BACKPORT_H
#define AMDKCL_FENCE_BACKPORT_H
#include <kcl/kcl_fence.h>

/*
 * commit v4.18-rc2-533-g418cc6ca0607
 * dma-fence: Allow wait_any_timeout for all fences)
 */
#ifdef AMDKCL_FENCE_WAIT_ANY_TIMEOUT
#define dma_fence_wait_any_timeout _kcl_fence_wait_any_timeout
#endif

/*
 * commit  v4.9-rc2-472-gbcc004b629d2
 * dma-buf/fence: make timeout handling in fence_default_wait consistent (v2))
 *
 * commit v4.9-rc2-473-g698c0f7ff216
 * dma-buf/fence: revert "don't wait when specified timeout is zero" (v2)
 */
#ifdef AMDKCL_FENCE_DEFAULT_WAIT_TIMEOUT
#define dma_fence_default_wait _kcl_fence_default_wait
#define dma_fence_wait_timeout _kcl_fence_wait_timeout
#endif

/*
 * commit v4.14-rc3-601-g5f72db59160c
 * dma-buf/fence: Sparse wants __rcu on the object itself
 */
#ifdef AMDKCL_FENCE_GET_RCU_SAFE
#define dma_fence_get_rcu_safe _kcl_fence_get_rcu_safe
#endif

/*
 * commit v4.18-rc2-533-g418cc6ca0607
 * dma-fence: Make ->wait callback optional
 */
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
#define AMDKCL_DMA_FENCE_OPS_WAIT_OPTIONAL \
	.wait = dma_fence_default_wait,
#else
#define AMDKCL_DMA_FENCE_OPS_WAIT_OPTIONAL
#endif

/*
 * commit v4.18-rc2-519-gc701317a3eb8
 * dma-fence: Make ->enable_signaling optional
 */
#ifdef AMDKCL_DMA_FENCE_OPS_ENABLE_SIGNALING
#define AMDKCL_DMA_FENCE_OPS_ENABLE_SIGNALING_OPTIONAL \
	.enable_signaling = _kcl_fence_enable_signaling,
#else
#define AMDKCL_DMA_FENCE_OPS_ENABLE_SIGNALING_OPTIONAL
#endif
#endif
