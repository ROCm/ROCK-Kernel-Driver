#ifndef AMDKCL_FENCE_H
#define AMDKCL_FENCE_H

#include <linux/version.h>
#if !defined(HAVE_DMA_FENCE_DEFINED)
#include <linux/fence.h>
#include <kcl/kcl_fence_array.h>
#else
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#endif

#if !defined(HAVE_DMA_FENCE_DEFINED)
#define dma_fence_cb fence_cb
#define dma_fence_ops fence_ops
#define dma_fence_array fence_array
#define dma_fence fence
#define DMA_FENCE_TRACE FENCE_TRACE
#define DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT FENCE_FLAG_ENABLE_SIGNAL_BIT
#define DMA_FENCE_FLAG_SIGNALED_BIT FENCE_FLAG_SIGNALED_BIT
#define dma_fence_wait fence_wait
#define dma_fence_get fence_get
#define dma_fence_put fence_put
#define dma_fence_is_signaled fence_is_signaled
#define dma_fence_signal fence_signal
#define dma_fence_signal_locked fence_signal_locked
#define dma_fence_get_rcu fence_get_rcu
#define dma_fence_array_create fence_array_create
#define dma_fence_add_callback fence_add_callback
#define dma_fence_remove_callback fence_remove_callback
#define dma_fence_default_wait fence_default_wait
#define dma_fence_enable_sw_signaling fence_enable_sw_signaling
typedef struct fence kcl_fence_t;
typedef struct fence_ops kcl_fence_ops_t;
#endif

#if !defined(HAVE_DMA_FENCE_DEFINED)
extern struct fence * _kcl_fence_get_rcu_safe(struct fence * __rcu *fencep);
extern signed long _kcl_fence_wait_any_timeout(struct fence **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx);
extern u64 _kcl_fence_context_alloc(unsigned num);
extern void _kcl_fence_init(struct fence *fence, const struct fence_ops *ops,
	     spinlock_t *lock, u64 context, unsigned seqno);
extern signed long _kcl_fence_wait_timeout(struct fence *fence, bool intr,
				signed long timeout);
#endif

/* commit v4.5-rc3-715-gb47bcb93bbf2
 * fall back to HAVE_DMA_FENCE_DEFINED check directly
 * as it's hard to detect the implementation in kernel
 */
#if !defined(HAVE_DMA_FENCE_DEFINED)
static inline bool dma_fence_is_later(struct dma_fence *f1, struct dma_fence *f2)
{
	if (WARN_ON(f1->context != f2->context))
		return false;

	return (int)(f1->seqno - f2->seqno) > 0;
}
#endif

#if !defined(HAVE_DMA_FENCE_DEFINED)
static inline u64 dma_fence_context_alloc(unsigned num)
{
	return _kcl_fence_context_alloc(num);
}

static inline void
dma_fence_init(struct dma_fence *fence, const struct dma_fence_ops *ops,
	       spinlock_t *lock, u64 context, unsigned seqno)
{
	return _kcl_fence_init(fence, ops, lock, context, seqno);
}
#endif

static inline signed long kcl_fence_wait_any_timeout(kcl_fence_t **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx)
{
#if !defined(HAVE_DMA_FENCE_DEFINED)
	return _kcl_fence_wait_any_timeout(fences, count, intr, timeout, idx);
#else
	return dma_fence_wait_any_timeout(fences, count, intr, timeout, idx);
#endif
}

#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
signed long
_kcl_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout);
#endif
static inline signed long
kcl_fence_default_wait(struct dma_fence *fence, bool intr, signed long timeout)
{
#if DRM_VERSION_CODE < DRM_VERSION(4, 19, 0)
	return _kcl_fence_default_wait(fence, intr, timeout);
#else
	return dma_fence_default_wait(fence, intr, timeout);
#endif
}

#if !defined(HAVE_DMA_FENCE_DEFINED)
static inline signed long
dma_fence_wait_timeout(struct dma_fence *fence, bool intr, signed long timeout)
{
	return _kcl_fence_wait_timeout(fence, intr, timeout);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
extern struct fence * _kcl_fence_get_rcu_safe(struct fence * __rcu *fencep);
#endif

static inline struct fence *
kcl_fence_get_rcu_safe(struct fence * __rcu *fencep)
{
#if !defined(HAVE_DMA_FENCE_DEFINED)
	return _kcl_fence_get_rcu_safe(fencep);
#else
	return dma_fence_get_rcu_safe(fencep);
#endif
}

static inline void kcl_dma_fence_set_error(struct dma_fence *fence,
				       int error)
{
#if DRM_VERSION_CODE < DRM_VERSION(4, 11, 0)
	BUG_ON(test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->flags));
	BUG_ON(error >= 0 || error < -MAX_ERRNO);

	fence->status = error;
#else
	dma_fence_set_error(fence, error);
#endif
}

#endif /* AMDKCL_FENCE_H */
