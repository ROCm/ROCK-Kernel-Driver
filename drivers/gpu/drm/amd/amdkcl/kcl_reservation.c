/*
 * Copyright (C) 2012-2013 Canonical Ltd
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <kcl/kcl_fence.h>
#include <kcl/kcl_reservation.h>

/*
 * Modifications [2016-10-26] (c) [2016]
 * Modifications [2017-07-06] (c) [2017]
 * Advanced Micro Devices, Inc.
 */

#if !defined(HAVE_RESERVATION_OBJECT_RESERVE_SHARED)
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
int _kcl_reservation_object_reserve_shared(struct reservation_object *obj,
				      unsigned int num_fences)
{
	struct reservation_object_list *old, *new;
	unsigned int i, j, k, max;

	old = reservation_object_get_list(obj);

	if (old && old->shared_max) {
		if ((old->shared_count + num_fences) <= old->shared_max)
			return 0;
		else
			max = max(old->shared_count + num_fences,
				  old->shared_max * 2);
	} else {
		max = 4;
	}

	new = kmalloc(offsetof(typeof(*new), shared[max]), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	/*
	 * no need to bump fence refcounts, rcu_read access
	 * requires the use of kref_get_unless_zero, and the
	 * references from the old struct are carried over to
	 * the new.
	 */
	for (i = 0, j = 0, k = max; i < (old ? old->shared_count : 0); ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(old->shared[i],
						  reservation_object_held(obj));
		if (dma_fence_is_signaled(fence))
			RCU_INIT_POINTER(new->shared[--k], fence);
		else
			RCU_INIT_POINTER(new->shared[j++], fence);
	}
	new->shared_count = j;
	new->shared_max = max;

	preempt_disable();
	write_seqcount_begin(&obj->seq);
	/*
	 * RCU_INIT_POINTER can be used here,
	 * seqcount provides the necessary barriers
	 */
	RCU_INIT_POINTER(obj->fence, new);
	write_seqcount_end(&obj->seq);
	preempt_enable();

	if (!old)
		return 0;

	/* Drop the references to the signaled fences */
	for (i = k; i < new->shared_max; ++i) {
		struct dma_fence *fence;

		fence = rcu_dereference_protected(new->shared[i],
						  reservation_object_held(obj));
		dma_fence_put(fence);
	}
	kfree_rcu(old, rcu);

	return 0;
}
EXPORT_SYMBOL(_kcl_reservation_object_reserve_shared);
#endif

#if !defined(HAVE_RESERVATION_OBJECT_WAIT_TIMEOUT_RCU)
long _kcl_reservation_object_wait_timeout_rcu(struct reservation_object *obj,
					 bool wait_all, bool intr,
					 unsigned long timeout)
{
	struct fence *fence;
	unsigned seq, shared_count;
	long ret = timeout ? timeout : 1;
    int i;
retry:
	shared_count = 0;
    i = -1;
	seq = read_seqcount_begin(&obj->seq);
	rcu_read_lock();

	/*
	 * Modifications [2017-08-16] (c) [2017]
	 * Advanced Micro Devices, Inc.
	 */
	fence = rcu_dereference(obj->fence_excl);
	if (fence && !test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->flags)) {
		if (!dma_fence_get_rcu(fence))
			goto unlock_retry;

		if (dma_fence_is_signaled(fence)) {
			dma_fence_put(fence);
			fence = NULL;
		}

	} else {
		fence = NULL;
	}

	if (wait_all) {
		struct reservation_object_list *fobj =
						rcu_dereference(obj->fence);

		if (fobj)
			shared_count = fobj->shared_count;

		if (read_seqcount_retry(&obj->seq, seq))
			goto unlock_retry;

		for (i = 0; !fence && i < shared_count; ++i) {
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

	rcu_read_unlock();
	if (fence) {
		ret = dma_fence_wait_timeout(fence, intr, ret);
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

/*
 * Modifications [2017-09-14] (c) [2017]
 * Advanced Micro Devices, Inc.
 */
#if !defined(HAVE_RESERVATION_OBJECT_COPY_FENCES)
int _kcl_reservation_object_copy_fences(struct reservation_object *dst,
					struct reservation_object *src)
{
	struct reservation_object_list *src_list, *dst_list;
	struct dma_fence *old, *new;
	size_t size;
	unsigned i;

	rcu_read_lock();
	src_list = rcu_dereference(src->fence);

retry:
	if (src_list) {
		unsigned shared_count = src_list->shared_count;

		size = offsetof(typeof(*src_list), shared[shared_count]);
		rcu_read_unlock();

		dst_list = kmalloc(size, GFP_KERNEL);
		if (!dst_list)
			return -ENOMEM;

		rcu_read_lock();
		src_list = rcu_dereference(src->fence);
		if (!src_list || src_list->shared_count > shared_count) {
			kfree(dst_list);
			goto retry;
		}

		dst_list->shared_count = 0;
		dst_list->shared_max = shared_count;
		for (i = 0; i < src_list->shared_count; ++i) {
			struct dma_fence *fence;

			fence = rcu_dereference(src_list->shared[i]);
			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &fence->flags))
				continue;

			if (!dma_fence_get_rcu(fence)) {
				kfree(dst_list);
				src_list = rcu_dereference(src->fence);
				goto retry;
			}

			if (dma_fence_is_signaled(fence)) {
				dma_fence_put(fence);
				continue;
			}

			dst_list->shared[dst_list->shared_count++] = fence;
		}
	} else {
		dst_list = NULL;
	}

	new = kcl_fence_get_rcu_safe(&src->fence_excl);
	rcu_read_unlock();

	kfree(dst->staged);
	dst->staged = NULL;

	src_list = reservation_object_get_list(dst);
	old = reservation_object_get_excl(dst);

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

/*
 * Modifications [2016-12-27] (c) [2016]
 * Advanced Micro Devices, Inc.
 */
#if !defined(HAVE_RESERVATION_OBJECT_TEST_SIGNALED_RCU)
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

#if !defined(HAVE_RESERVATION_OBJECT_ADD_SHARED_FENCE)
static void
reservation_object_add_shared_inplace(struct reservation_object *obj,
				      struct reservation_object_list *fobj,
				      struct dma_fence *fence)
{
	struct dma_fence *signaled = NULL;
	u32 i, signaled_idx;

	dma_fence_get(fence);

	preempt_disable();
	write_seqcount_begin(&obj->seq);

	for (i = 0; i < fobj->shared_count; ++i) {
		struct dma_fence *old_fence;

		old_fence = rcu_dereference_protected(fobj->shared[i],
						reservation_object_held(obj));

		if (old_fence->context == fence->context) {
			/* memory barrier is added by write_seqcount_begin */
			RCU_INIT_POINTER(fobj->shared[i], fence);
			write_seqcount_end(&obj->seq);
			preempt_enable();

			dma_fence_put(old_fence);
			return;
		}

		if (!signaled && dma_fence_is_signaled(old_fence)) {
			signaled = old_fence;
			signaled_idx = i;
		}
	}

	/*
	 * memory barrier is added by write_seqcount_begin,
	 * fobj->shared_count is protected by this lock too
	 */
	if (signaled) {
		RCU_INIT_POINTER(fobj->shared[signaled_idx], fence);
	} else {
		RCU_INIT_POINTER(fobj->shared[fobj->shared_count], fence);
		fobj->shared_count++;
	}

	write_seqcount_end(&obj->seq);
	preempt_enable();

	dma_fence_put(signaled);
}

static void
reservation_object_add_shared_replace(struct reservation_object *obj,
				      struct reservation_object_list *old,
				      struct reservation_object_list *fobj,
				      struct dma_fence *fence)
{
	unsigned i, j, k;

	dma_fence_get(fence);

	if (!old) {
		RCU_INIT_POINTER(fobj->shared[0], fence);
		fobj->shared_count = 1;
		goto done;
	}

	/*
	 * no need to bump fence refcounts, rcu_read access
	 * requires the use of kref_get_unless_zero, and the
	 * references from the old struct are carried over to
	 * the new.
	 */
	for (i = 0, j = 0, k = fobj->shared_max; i < old->shared_count; ++i) {
		struct dma_fence *check;

		check = rcu_dereference_protected(old->shared[i],
						reservation_object_held(obj));

		if (check->context == fence->context ||
		    dma_fence_is_signaled(check))
			RCU_INIT_POINTER(fobj->shared[--k], check);
		else
			RCU_INIT_POINTER(fobj->shared[j++], check);
	}
	RCU_INIT_POINTER(fobj->shared[j++], fence);
	fobj->shared_count = j;

done:
	preempt_disable();
	write_seqcount_begin(&obj->seq);
	/*
	 * RCU_INIT_POINTER can be used here,
	 * seqcount provides the necessary barriers
	 */
	RCU_INIT_POINTER(obj->fence, fobj);
	write_seqcount_end(&obj->seq);
	preempt_enable();

	if (!old)
		return;

	/* Drop the references to the signaled fences */
	for (i = k; i < fobj->shared_max; ++i) {
		struct dma_fence *f;

		f = rcu_dereference_protected(fobj->shared[i],
					      reservation_object_held(obj));
		dma_fence_put(f);
	}
	kfree_rcu(old, rcu);
}

void _kcl_reservation_object_add_shared_fence(struct reservation_object *obj,
					 struct dma_fence *fence)
{
	struct reservation_object_list *old, *fobj = obj->staged;

	old = reservation_object_get_list(obj);
	obj->staged = NULL;

	if (!fobj) {
		BUG_ON(old->shared_count >= old->shared_max);
		reservation_object_add_shared_inplace(obj, old, fence);
	} else
		reservation_object_add_shared_replace(obj, old, fobj, fence);
}
EXPORT_SYMBOL(_kcl_reservation_object_add_shared_fence);
#endif

int _kcl_reservation_object_get_fences_rcu(struct reservation_object *obj,
				      struct dma_fence **pfence_excl,
				      unsigned *pshared_count,
				      struct dma_fence ***pshared)
{
	struct dma_fence **shared = NULL;
	struct dma_fence *fence_excl;
	unsigned int shared_count;
	int ret = 1;

	do {
		struct reservation_object_list *fobj;
		unsigned int i, seq;
		size_t sz = 0;

		shared_count = i = 0;

		rcu_read_lock();
		seq = read_seqcount_begin(&obj->seq);

		fence_excl = rcu_dereference(obj->fence_excl);
		if (fence_excl && !dma_fence_get_rcu(fence_excl))
			goto unlock;

		fobj = rcu_dereference(obj->fence);
		if (fobj)
			sz += sizeof(*shared) * fobj->shared_max;

		if (!pfence_excl && fence_excl)
			sz += sizeof(*shared);

		if (sz) {
			struct dma_fence **nshared;

			nshared = krealloc(shared, sz,
					   GFP_NOWAIT | __GFP_NOWARN);
			if (!nshared) {
				rcu_read_unlock();
				nshared = krealloc(shared, sz, GFP_KERNEL);
				if (nshared) {
					shared = nshared;
					continue;
				}

				ret = -ENOMEM;
				break;
			}
			shared = nshared;
			shared_count = fobj ? fobj->shared_count : 0;
			for (i = 0; i < shared_count; ++i) {
				shared[i] = rcu_dereference(fobj->shared[i]);
				if (!dma_fence_get_rcu(shared[i]))
					break;
			}

			if (!pfence_excl && fence_excl) {
				shared[i] = fence_excl;
				fence_excl = NULL;
				++i;
				++shared_count;
			}
		}

		if (i != shared_count || read_seqcount_retry(&obj->seq, seq)) {
			while (i--)
				dma_fence_put(shared[i]);
			dma_fence_put(fence_excl);
			goto unlock;
		}

		ret = 0;
unlock:
		rcu_read_unlock();
	} while (ret);

	if (!shared_count) {
		kfree(shared);
		shared = NULL;
	}

	*pshared_count = shared_count;
	*pshared = shared;
	if (pfence_excl)
		*pfence_excl = fence_excl;

	return ret;
}
EXPORT_SYMBOL(_kcl_reservation_object_get_fences_rcu);
