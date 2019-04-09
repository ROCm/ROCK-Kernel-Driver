#ifndef AMDKCL_FENCE_H
#define AMDKCL_FENCE_H

#include <linux/version.h>
#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
#include <linux/fence.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
#include <linux/fence-array.h>
#endif
#include <kcl/kcl_fence_array.h>
#else
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
#define dma_fence_cb fence_cb
#define dma_fence_ops fence_ops
#define dma_fence_array fence_array
#define dma_fence fence
#define DMA_FENCE_TRACE FENCE_TRACE
#define DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT FENCE_FLAG_ENABLE_SIGNAL_BIT

#define dma_fence_init fence_init
#define dma_fence_wait fence_wait
#define dma_fence_get fence_get
#define dma_fence_put fence_put
#define dma_fence_is_signaled fence_is_signaled
#define dma_fence_signal fence_signal
#define dma_fence_signal_locked fence_signal_locked
#define dma_fence_get_rcu fence_get_rcu
#define dma_fence_is_later fence_is_later
#define dma_fence_wait_timeout fence_wait_timeout
#define dma_fence_array_create fence_array_create
#define dma_fence_add_callback fence_add_callback
#define dma_fence_remove_callback fence_remove_callback
#define dma_fence_default_wait fence_default_wait
#define dma_fence_enable_sw_signaling fence_enable_sw_signaling
typedef struct fence kcl_fence_t;
typedef struct fence_ops kcl_fence_ops_t;
#else
#define fence_cb dma_fence_cb
#define fence_ops dma_fence_ops
#define fence_array dma_fence_array
#define fence dma_fence
#define FENCE_TRACE DMA_FENCE_TRACE
#define FENCE_FLAG_SIGNALED_BIT DMA_FENCE_FLAG_SIGNALED_BIT
#define FENCE_FLAG_ENABLE_SIGNAL_BIT DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT
#define FENCE_FLAG_USER_BITS DMA_FENCE_FLAG_USER_BITS

#define fence_init dma_fence_init
#define fence_wait dma_fence_wait
#define fence_get dma_fence_get
#define fence_put dma_fence_put
#define fence_is_signaled dma_fence_is_signaled
#define fence_signal dma_fence_signal
#define fence_signal_locked dma_fence_signal_locked
#define fence_get_rcu dma_fence_get_rcu
#define fence_is_later dma_fence_is_later
#define fence_wait_timeout dma_fence_wait_timeout
#define fence_array_create dma_fence_array_create
#define fence_add_callback dma_fence_add_callback
#define fence_remove_callback dma_fence_remove_callback
#define fence_default_wait dma_fence_default_wait
#define fence_enable_sw_signaling dma_fence_enable_sw_signaling
typedef struct dma_fence kcl_fence_t;
typedef struct dma_fence_ops kcl_fence_ops_t;
#endif

#if defined(BUILD_AS_DKMS) && !defined(OS_NAME_RHEL_7_X)
extern signed long kcl_fence_default_wait(kcl_fence_t *fence,
					  bool intr,
					  signed long timeout);
extern signed long _kcl_fence_wait_any_timeout(struct fence **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx);
extern u64 _kcl_fence_context_alloc(unsigned num);
extern void _kcl_fence_init(struct fence *fence, const struct fence_ops *ops,
	     spinlock_t *lock, u64 context, unsigned seqno);
extern signed long _kcl_fence_wait_timeout(struct fence *fence, bool intr,
				signed long timeout);
#endif

#if DRM_VERSION_CODE < DRM_VERSION(4, 5, 0)
static inline bool fence_is_later(struct fence *f1, struct fence *f2)
{
	if (WARN_ON(f1->context != f2->context))
		return false;

	return (int)(f1->seqno - f2->seqno) > 0;
}
#endif

static inline signed long kcl_fence_wait_any_timeout(kcl_fence_t **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx)
{
#if defined(BUILD_AS_DKMS) && DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
	return _kcl_fence_wait_any_timeout(fences, count, intr, timeout, idx);
#else
	return dma_fence_wait_any_timeout(fences, count, intr, timeout, idx);
#endif
}

static inline u64 kcl_fence_context_alloc(unsigned num)
{
#if defined(BUILD_AS_DKMS) && DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
	return _kcl_fence_context_alloc(num);
#else
	return dma_fence_context_alloc(num);
#endif
}

static inline void kcl_fence_init(kcl_fence_t *fence, const kcl_fence_ops_t *ops,
	     spinlock_t *lock, u64 context, unsigned seqno)
{
#if defined(BUILD_AS_DKMS) && DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
	return _kcl_fence_init(fence, ops, lock, context, seqno);
#else
	return dma_fence_init(fence, ops, lock, context, seqno);
#endif
}

static inline signed long kcl_fence_wait_timeout(kcl_fence_t *fences, bool intr,
					signed long timeout)
{
#if defined(BUILD_AS_DKMS) && DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
	return _kcl_fence_wait_timeout(fences, intr, timeout);
#else
	return dma_fence_wait_timeout(fences, intr, timeout);
#endif
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
extern struct fence * _kcl_fence_get_rcu_safe(struct fence * __rcu *fencep);
#endif

static inline struct fence *
kcl_fence_get_rcu_safe(struct fence * __rcu *fencep)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
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
