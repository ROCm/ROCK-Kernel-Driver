#ifndef AMDKCL_RESERVATION_H
#define AMDKCL_RESERVATION_H

#include <linux/reservation.h>
#include <linux/ww_mutex.h>

#if !defined(HAVE_RESERVATION_OBJECT_RESERVE_SHARED)
extern int _kcl_reservation_object_reserve_shared(struct reservation_object *obj,
				      unsigned int num_fences);
#endif
static inline int
kcl_reservation_object_reserve_shared(struct reservation_object *obj,
				      unsigned int num_fences)
{
#if !defined(HAVE_RESERVATION_OBJECT_RESERVE_SHARED)
        return _kcl_reservation_object_reserve_shared(obj,num_fences);
#else
        return reservation_object_reserve_shared(obj,num_fences);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_WAIT_TIMEOUT_RCU)
extern long _kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout);
#endif

static inline long
kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout)
{
#if !defined(HAVE_RESERVATION_OBJECT_WAIT_TIMEOUT_RCU)
	return _kcl_reservation_object_wait_timeout_rcu(obj,
					wait_all, intr, timeout);
#else
	return reservation_object_wait_timeout_rcu(obj,
					wait_all, intr, timeout);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_LOCK)
static inline int
_kcl_reservation_object_lock(struct reservation_object *obj,
				struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock(&obj->lock, ctx);
}
#endif

static inline int
kcl_reservation_object_lock(struct reservation_object *obj,
				struct ww_acquire_ctx *ctx)
{
#if !defined(HAVE_RESERVATION_OBJECT_LOCK)
	return _kcl_reservation_object_lock(obj, ctx);
#else
	return reservation_object_lock(obj, ctx);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_LOCK)
static inline void
_kcl_reservation_object_unlock(struct reservation_object *obj)
{
	ww_mutex_unlock(&obj->lock);
}
#endif

static inline void
kcl_reservation_object_unlock(struct reservation_object *obj)
{
#if !defined(HAVE_RESERVATION_OBJECT_LOCK)
	return _kcl_reservation_object_unlock(obj);
#else
	return reservation_object_unlock(obj);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_COPY_FENCES)
extern int _kcl_reservation_object_copy_fences(struct reservation_object *dst,
					struct reservation_object *src);
#endif

static inline int
kcl_reservation_object_copy_fences(struct reservation_object *dst,
				struct reservation_object *src)
{
#if !defined(HAVE_RESERVATION_OBJECT_COPY_FENCES)
	return _kcl_reservation_object_copy_fences(dst, src);
#else
	return reservation_object_copy_fences(dst, src);
#endif
}

static inline int
kcl_reservation_object_lock_interruptible(struct reservation_object *obj,
					struct ww_acquire_ctx *ctx)
{
#if !defined(HAVE_RESERVATION_OBJECT_LOCK_INTERRUPTIBLE)
	return ww_mutex_lock_interruptible(&obj->lock, ctx);
#else
	return reservation_object_lock_interruptible(obj, ctx);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_TRYLOCK)
static inline bool __must_check
_kcl_reservation_object_trylock(struct reservation_object *obj)
{
	return ww_mutex_trylock(&obj->lock);
}
#endif

static inline bool __must_check
kcl_reservation_object_trylock(struct reservation_object *obj)
{
#if !defined(HAVE_RESERVATION_OBJECT_TRYLOCK)
	return _kcl_reservation_object_trylock(obj);
#else
	return reservation_object_trylock(obj);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_TEST_SIGNALED_RCU)
bool _kcl_reservation_object_test_signaled_rcu(struct reservation_object *obj,
					       bool test_all);
#endif

static inline bool
kcl_reservation_object_test_signaled_rcu(struct reservation_object *obj,
					 bool test_all)
{
#if !defined(HAVE_RESERVATION_OBJECT_TEST_SIGNALED_RCU)
	return _kcl_reservation_object_test_signaled_rcu(obj, test_all);
#else
	return reservation_object_test_signaled_rcu(obj, test_all);
#endif
}

#if !defined(HAVE_RESERVATION_OBJECT_ADD_SHARED_FENCE)
extern void _kcl_reservation_object_add_shared_fence(
						struct reservation_object *obj,
						struct dma_fence *fence);
#endif

static inline void
kcl_reservation_object_add_shared_fence(struct reservation_object *obj,
					struct dma_fence *fence)
{
#if !defined(HAVE_RESERVATION_OBJECT_ADD_SHARED_FENCE)
	return _kcl_reservation_object_add_shared_fence(obj, fence);
#else
	return reservation_object_add_shared_fence(obj, fence);
#endif
}

#if defined(BUILD_AS_DKMS)
extern int
_kcl_reservation_object_get_fences_rcu(struct reservation_object *obj,
				      struct dma_fence **pfence_excl,
				      unsigned *pshared_count,
				      struct dma_fence ***pshared);
#endif

static inline int
kcl_reservation_object_get_fences_rcu(struct reservation_object *obj,
				      struct dma_fence **pfence_excl,
				      unsigned *pshared_count,
				      struct dma_fence ***pshared)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_reservation_object_get_fences_rcu(obj, pfence_excl, pshared_count, pshared);
#else
	return reservation_object_get_fences_rcu(obj, pfence_excl, pshared_count, pshared);
#endif

}
#endif /* AMDKCL_RESERVATION_H */
