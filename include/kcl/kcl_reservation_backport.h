#ifndef AMDKCL_RESERVATION_BACKPORT_H
#define AMDKCL_RESERVATION_BACKPORT_H

#include <linux/reservation.h>
#include <kcl/kcl_reservation.h>

#if !defined(HAVE_RESERVATION_OBJECT_RESERVE_SHARED_P_I)
/**
 * _kcl_reservation_object_reserve_shared - Reserve space to add shared fences to
 * a reservation_object.
 * @obj: reservation object
 * @num_fences: number of fences we want to add
 *
 * Should be called before reservation_object_add_shared_fence().  Must
 * be called with obj->lock held.
 *
 * RETURNS
 * Zero for success, or -errno
 */
static inline
int _kcl_reservation_object_reserve_shared(struct reservation_object *obj,
				      unsigned int num_fences)
{
	int i, r;

	for (i = 0; i < num_fences; i++) {
		r = reservation_object_reserve_shared(obj);
		if (r)
			return r;
	}

	return 0;
}
#define reservation_object_reserve_shared _kcl_reservation_object_reserve_shared
#endif

#if defined(BUILD_AS_DKMS) && DRM_VERSION_CODE < DRM_VERSION(4, 10, 0)
#define reservation_object_wait_timeout_rcu _kcl_reservation_object_wait_timeout_rcu
#endif

#endif
