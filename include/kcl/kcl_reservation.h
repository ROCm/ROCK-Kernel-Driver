#ifndef AMDKCL_RESERVATION_H
#define AMDKCL_RESERVATION_H

#include <linux/reservation.h>
#include <linux/ww_mutex.h>

#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
extern long _kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout);
#endif

static inline long
kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout)
{
#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	return _kcl_reservation_object_wait_timeout_rcu(obj,
					wait_all, intr, timeout);
#else
	return reservation_object_wait_timeout_rcu(obj,
					wait_all, intr, timeout);
#endif
}

#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
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
#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	return _kcl_reservation_object_lock(obj, ctx);
#else
	return reservation_object_lock(obj, ctx);
#endif
}

#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
static inline void
_kcl_reservation_object_unlock(struct reservation_object *obj)
{
	ww_mutex_unlock(&obj->lock);
}
#endif

static inline void
kcl_reservation_object_unlock(struct reservation_object *obj)
{
#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	return _kcl_reservation_object_unlock(obj);
#else
	return reservation_object_unlock(obj);
#endif
}

#endif /* AMDKCL_RESERVATION_H */
