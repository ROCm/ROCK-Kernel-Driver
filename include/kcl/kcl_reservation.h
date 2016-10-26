#ifndef AMDKCL_RESERVATION_H
#define AMDKCL_RESERVATION_H

#include <linux/reservation.h>

#if defined(BUILD_AS_DKMS)
extern long _kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout);
#endif

static inline long 
kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout)
{
#if defined(BUILD_AS_DKMS)
	return _kcl_reservation_object_wait_timeout_rcu(obj, 
					wait_all, intr, timeout);
#else
	return reservation_object_wait_timeout_rcu(obj, 
					wait_all, intr, timeout);
#endif
}

#ifdef OS_NAME_RHEL_6
bool _kcl_reservation_object_test_signaled_rcu(struct reservation_object *obj,
					       bool test_all);
#endif

static inline bool
kcl_reservation_object_test_signaled_rcu(struct reservation_object *obj,
					 bool test_all)
{
#ifdef OS_NAME_RHEL_6
	return _kcl_reservation_object_test_signaled_rcu(obj, test_all);
#else
	return reservation_object_test_signaled_rcu(obj, test_all);
#endif
}

#endif /* AMDKCL_RESERVATION_H */
