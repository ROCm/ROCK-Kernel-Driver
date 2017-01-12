#include <kcl/kcl_fence.h>
#include <kcl/kcl_reservation.h>

long _kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout)
{
	struct fence *fence;
	unsigned seq, shared_count, i = 0;
	long ret = timeout ? timeout : 1;

retry:
	fence = NULL;
	shared_count = 0;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();

	if (wait_all) {
		struct reservation_object_list *fobj =
						rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		for (i = 0; i < shared_count; ++i) {
			struct fence *lfence = rcu_dereference(fobj->shared[i]);

			if (test_bit(FENCE_FLAG_SIGNALED_BIT, &lfence->flags))
				continue;

			if (!fence_get_rcu(lfence))
				goto unlock_retry;

			if (fence_is_signaled(lfence)) {
				fence_put(lfence);
				continue;
			}

			fence = lfence;
			break;
		}
	}

	if (!shared_count) {
		struct fence *fence_excl = rcu_dereference(obj->fence_excl);

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		if (fence_excl &&
		    !test_bit(FENCE_FLAG_SIGNALED_BIT, &fence_excl->flags)) {
			if (!fence_get_rcu(fence_excl))
				goto unlock_retry;

			if (fence_is_signaled(fence_excl))
				fence_put(fence_excl);
			else
				fence = fence_excl;
		}
	}

	rcu_read_unlock();
	if (fence) {
		ret = kcl_fence_wait_timeout(fence, intr, ret);
		fence_put(fence);
		if (ret > 0 && wait_all && (i + 1 < shared_count))
			goto retry;
	}
	return ret;

unlock_retry:
	rcu_read_unlock();
	goto retry;
}
EXPORT_SYMBOL(_kcl_reservation_object_wait_timeout_rcu);

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
