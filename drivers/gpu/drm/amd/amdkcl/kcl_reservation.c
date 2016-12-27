#include <kcl/kcl_reservation.h>

#ifdef OS_NAME_RHEL_6
static inline int
reservation_object_test_signaled_single(struct fence *passed_fence)
{
	struct fence *fence, *lfence = passed_fence;
	int ret = 1;

	if (!test_bit(FENCE_FLAG_SIGNALED_BIT, &lfence->flags)) {
		fence = fence_get_rcu(lfence);
		if (!fence)
			return -1;

		ret = !!fence_is_signaled(fence);
		fence_put(fence);
	}
	return ret;
}

bool _kcl_reservation_object_test_signaled_rcu(struct reservation_object *obj,
					       bool test_all)
{
	unsigned seq, shared_count;
	int ret = true;

retry:
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();

	if (test_all) {
		unsigned i;

		struct reservation_object_list *fobj = rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		for (i = 0; i < shared_count; ++i) {
			struct fence *fence = rcu_dereference(fobj->shared[i]);

			ret = reservation_object_test_signaled_single(fence);
			if (ret < 0)
				goto unlock_retry;
			else if (!ret)
				break;
		}

		/*
		 * There could be a read_seqcount_retry here, but nothing cares
		 * about whether it's the old or newer fence pointers that are
		 * signaled. That race could still have happened after checking
		 * read_seqcount_retry. If you care, use ww_mutex_lock.
		 */
	}

	if (!shared_count) {
		struct fence *fence_excl = rcu_dereference(obj->fence_excl);

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		if (fence_excl) {
			ret = reservation_object_test_signaled_single(fence_excl);
			if (ret < 0)
				goto unlock_retry;
		}
	}

	rcu_read_unlock();
	return ret;

unlock_retry:
	rcu_read_unlock();
	goto retry;
}
EXPORT_SYMBOL_GPL(_kcl_reservation_object_test_signaled_rcu);
#endif
