#ifndef AMDKCL_FENCE_H
#define AMDKCL_FENCE_H

#include <linux/version.h>
#include <linux/fence.h>

signed long
kcl_fence_default_wait(struct fence *fence, bool intr, signed long timeout);

#if defined(BUILD_AS_DKMS)
extern signed long _kcl_fence_wait_any_timeout(struct fence **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx);
extern u64 _kcl_fence_context_alloc(unsigned num);
extern void _kcl_fence_init(struct fence *fence, const struct fence_ops *ops,
	     spinlock_t *lock, u64 context, unsigned seqno);
extern signed long _kcl_fence_wait_timeout(struct fence *fence, bool intr,
				signed long timeout);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) && !defined(OS_NAME_RHEL_7_3)
static inline bool fence_is_later(struct fence *f1, struct fence *f2)
{
	if (WARN_ON(f1->context != f2->context))
		return false;

	return (int)(f1->seqno - f2->seqno) > 0;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0) */

static inline signed long kcl_fence_wait_any_timeout(struct fence **fences,
				   uint32_t count, bool intr,
				   signed long timeout, uint32_t *idx)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_fence_wait_any_timeout(fences, count, intr, timeout, idx);
#else
	return fence_wait_any_timeout(fences, count, intr, timeout, idx);
#endif
}

static inline u64 kcl_fence_context_alloc(unsigned num)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_fence_context_alloc(num);
#else
	return fence_context_alloc(num);
#endif
}

static inline void kcl_fence_init(struct fence *fence, const struct fence_ops *ops,
	     spinlock_t *lock, u64 context, unsigned seqno)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_fence_init(fence, ops, lock, context, seqno);
#else
	return fence_init(fence, ops, lock, context, seqno);
#endif
}

static inline signed long kcl_fence_wait_timeout(struct fence *fences, bool intr,
					signed long timeout)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_fence_wait_timeout(fences, intr, timeout);
#else
	return fence_wait_timeout(fences, intr, timeout);
#endif
}
#endif /* AMDKCL_FENCE_H */
