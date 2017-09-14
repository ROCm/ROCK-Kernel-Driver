#include <kcl/kcl_fence.h>
#include <kcl/kcl_reservation.h>

#if defined(BUILD_AS_DKMS) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
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
#endif

#if defined(BUILD_AS_DKMS)
int _kcl_reservation_object_copy_fences(struct reservation_object *dst,
					struct reservation_object *src)
{
	struct reservation_object_list *src_list, *dst_list;
	struct dma_fence *old, *new;
	size_t size;
	unsigned i;

	src_list = reservation_object_get_list(src);

	/*
	 * resize dst->fence or allocate if it doesn't exist,
	 * noop if already correct size
	 */
	size = offsetof(typeof(*src_list), shared[src_list->shared_count]);
	dst_list = kmalloc(size, GFP_KERNEL);
	if (!dst_list)
		return -ENOMEM;

	kfree(dst->staged);
	dst->staged = NULL;

	dst_list->shared_count = src_list->shared_count;
	dst_list->shared_max = src_list->shared_count;
	for (i = 0; i < src_list->shared_count; ++i)
		dst_list->shared[i] = dma_fence_get(src_list->shared[i]);

	src_list = reservation_object_get_list(dst);

	old = reservation_object_get_excl(dst);
	new = reservation_object_get_excl(src);

	dma_fence_get(new);

	preempt_disable();
	write_seqcount_begin(&dst->seq);
	/* write_seqcount_begin provides the necessary memory barrier */
	RCU_INIT_POINTER(dst->fence_excl, new);
	RCU_INIT_POINTER(dst->fence, dst_list);
	write_seqcount_end(&dst->seq);
	preempt_enable();

	if (src_list)
		kfree_rcu(src_list, rcu);
	dma_fence_put(old);

	return 0;
}
EXPORT_SYMBOL(_kcl_reservation_object_copy_fences);
#endif
