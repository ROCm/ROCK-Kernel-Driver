// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2012-2014 Canonical Ltd (Maarten Lankhorst)
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
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
#include <linux/dma-resv.h>
#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/seq_file.h>

/* Copied from drivers/dma-buf/dma-resv.c */
#ifndef HAVE_DMA_RESV_FENCES

/**
 * DOC: Reservation Object Overview
 *
 * The reservation object provides a mechanism to manage a container of
 * dma_fence object associated with a resource. A reservation object
 * can have any number of fences attaches to it. Each fence carries an usage
 * parameter determining how the operation represented by the fence is using the
 * resource. The RCU mechanism is used to protect read access to fences from
 * locked write-side updates.
 *
 * See struct dma_resv for more details.
 */

extern struct ww_class reservation_ww_class;

/**
 * dma_resv_list_alloc - allocate fence list
 * @shared_max: number of fences we need space for
 *
 * Allocate a new dma_resv_list and make sure to correctly initialize
 * shared_max.
 */
static struct dma_resv_list *dma_resv_list_alloc(unsigned int shared_max)
{
        struct dma_resv_list *list;

        list = kmalloc(struct_size(list, shared, shared_max), GFP_KERNEL);
        if (!list)
                return NULL;

        list->shared_max = (ksize(list) - offsetof(typeof(*list), shared)) /
                sizeof(*list->shared);

        return list;
}

/**
 * dma_resv_list_free - free fence list
 * @list: list to free
 *
 * Free a dma_resv_list and make sure to drop all references.
 */
static void dma_resv_list_free(struct dma_resv_list *list)
{
        unsigned int i;

        if (!list)
                return;

        for (i = 0; i < list->shared_count; ++i)
                dma_fence_put(rcu_dereference_protected(list->shared[i], true));

        kfree_rcu(list, rcu);
}

/**
 * dma_resv_init - initialize a reservation object
 * @obj: the reservation object
 */
void dma_resv_init(struct dma_resv *obj)
{
        ww_mutex_init(&obj->lock, &reservation_ww_class);
        seqcount_ww_mutex_init(&obj->seq, &obj->lock);

        RCU_INIT_POINTER(obj->fence, NULL);
        RCU_INIT_POINTER(obj->fence_excl, NULL);
}
EXPORT_SYMBOL(dma_resv_init);

/**
 * dma_resv_fini - destroys a reservation object
 * @obj: the reservation object
 */
void dma_resv_fini(struct dma_resv *obj)
{
        struct dma_resv_list *fobj;
        struct dma_fence *excl;

        /*
         * This object should be dead and all references must have
         * been released to it, so no need to be protected with rcu.
         */
        excl = rcu_dereference_protected(obj->fence_excl, 1);
        if (excl)
                dma_fence_put(excl);

        fobj = rcu_dereference_protected(obj->fence, 1);
        dma_resv_list_free(fobj);
        ww_mutex_destroy(&obj->lock);
}
EXPORT_SYMBOL(dma_resv_fini);

/**
 * dma_resv_reserve_fences - Reserve space to add shared fences to
 * a dma_resv.
 * @obj: reservation object
 * @num_fences: number of fences we want to add
 *
 * Should be called before dma_resv_add_shared_fence().  Must
 * be called with @obj locked through dma_resv_lock().
 *
 * Note that the preallocated slots need to be re-reserved if @obj is unlocked
 * at any time before calling dma_resv_add_shared_fence(). This is validated
 * when CONFIG_DEBUG_MUTEXES is enabled.
 *
 * RETURNS
 * Zero for success, or -errno
 */
int dma_resv_reserve_fences(struct dma_resv *obj, unsigned int num_fences)
{
        struct dma_resv_list *old, *new;
        unsigned int i, j, k, max;

        dma_resv_assert_held(obj);

        old = dma_resv_shared_list(obj);
        if (old && old->shared_max) {
                if ((old->shared_count + num_fences) <= old->shared_max)
                        return 0;
                max = max(old->shared_count + num_fences, old->shared_max * 2);
        } else {
                max = max(4ul, roundup_pow_of_two(num_fences));
        }

        new = dma_resv_list_alloc(max);
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
                                                  dma_resv_held(obj));
                if (dma_fence_is_signaled(fence))
                        RCU_INIT_POINTER(new->shared[--k], fence);
                else
                        RCU_INIT_POINTER(new->shared[j++], fence);
        }
        new->shared_count = j;

        /*
         * We are not changing the effective set of fences here so can
         * merely update the pointer to the new array; both existing
         * readers and new readers will see exactly the same set of
         * active (unsignaled) shared fences. Individual fences and the
         * old array are protected by RCU and so will not vanish under
         * the gaze of the rcu_read_lock() readers.
         */
        rcu_assign_pointer(obj->fence, new);

        if (!old)
                return 0;

        /* Drop the references to the signaled fences */
        for (i = k; i < max; ++i) {
                struct dma_fence *fence;

                fence = rcu_dereference_protected(new->shared[i],
                                                  dma_resv_held(obj));
                dma_fence_put(fence);
        }
        kfree_rcu(old, rcu);

        return 0;
}
EXPORT_SYMBOL(dma_resv_reserve_fences);

#ifdef CONFIG_DEBUG_MUTEXES
/**
 * dma_resv_reset_max_fences - reset shared fences for debugging
 * @obj: the dma_resv object to reset
 *
 * Reset the number of pre-reserved shared slots to test that drivers do
 * correct slot allocation using dma_resv_reserve_fences(). See also
 * &dma_resv_list.shared_max.
 */
void dma_resv_reset_max_fences(struct dma_resv *obj)
{
        struct dma_resv_list *fences = dma_resv_shared_list(obj);

        dma_resv_assert_held(obj);

        /* Test shared fence slot reservation */
        if (fences)
                fences->shared_max = fences->shared_count;
}
EXPORT_SYMBOL(dma_resv_reset_max_fences);
#endif

/**
 * dma_resv_add_shared_fence - Add a fence to a shared slot
 * @obj: the reservation object
 * @fence: the shared fence to add
 *
 * Add a fence to a shared slot, @obj must be locked with dma_resv_lock(), and
 * dma_resv_reserve_fences() has been called.
 *
 * See also &dma_resv.fence for a discussion of the semantics.
 */
static void dma_resv_add_shared_fence(struct dma_resv *obj,
                                      struct dma_fence *fence)
{
        struct dma_resv_list *fobj;
        struct dma_fence *old;
        unsigned int i, count;

        dma_fence_get(fence);

        dma_resv_assert_held(obj);

        /* Drivers should not add containers here, instead add each fence
         * individually.
         */
        WARN_ON(dma_fence_is_container(fence));

        fobj = dma_resv_shared_list(obj);
        count = fobj->shared_count;

        write_seqcount_begin(&obj->seq);

        for (i = 0; i < count; ++i) {

                old = rcu_dereference_protected(fobj->shared[i],
                                                dma_resv_held(obj));
                if (old->context == fence->context ||
                    dma_fence_is_signaled(old))
                        goto replace;
        }

        BUG_ON(fobj->shared_count >= fobj->shared_max);
        old = NULL;
        count++;

replace:
        RCU_INIT_POINTER(fobj->shared[i], fence);
        /* pointer update must be visible before we extend the shared_count */
        smp_store_mb(fobj->shared_count, count);

        write_seqcount_end(&obj->seq);
        dma_fence_put(old);
}

/**
 * dma_resv_replace_fences - replace fences in the dma_resv obj
 * @obj: the reservation object
 * @context: the context of the fences to replace
 * @replacement: the new fence to use instead
 * @usage: how the new fence is used, see enum dma_resv_usage
 *
 * Replace fences with a specified context with a new fence. Only valid if the
 * operation represented by the original fence has no longer access to the
 * resources represented by the dma_resv object when the new fence completes.
 *
 * And example for using this is replacing a preemption fence with a page table
 * update fence which makes the resource inaccessible.
 */
void dma_resv_replace_fences(struct dma_resv *obj, uint64_t context,
                             struct dma_fence *replacement,
                             enum dma_resv_usage usage)
{
        struct dma_resv_list *list;
        struct dma_fence *old;
        unsigned int i;

        /* Only readers supported for now */
        WARN_ON(usage != DMA_RESV_USAGE_READ && usage != DMA_RESV_USAGE_BOOKKEEP);

        dma_resv_assert_held(obj);

        write_seqcount_begin(&obj->seq);

        old = dma_resv_excl_fence(obj);
        if (old && old->context == context) {
                RCU_INIT_POINTER(obj->fence_excl, dma_fence_get(replacement));
                dma_fence_put(old);
        }

        list = dma_resv_shared_list(obj);
        for (i = 0; list && i < list->shared_count; ++i) {
                old = rcu_dereference_protected(list->shared[i],
                                                dma_resv_held(obj));
                if (old->context != context)
                        continue;

                rcu_assign_pointer(list->shared[i], dma_fence_get(replacement));
                dma_fence_put(old);
        }

        write_seqcount_end(&obj->seq);
}
EXPORT_SYMBOL(dma_resv_replace_fences);

/**
 * dma_resv_add_fence - Add a fence to the dma_resv obj
 * @obj: the reservation object
 * @fence: the fence to add
 * @usage: how the fence is used, see enum dma_resv_usage
 *
 * Add a fence to a slot, @obj must be locked with dma_resv_lock(), and
 * dma_resv_reserve_fences() has been called.
 *
 * See also &dma_resv.fence for a discussion of the semantics.
 */
void dma_resv_add_fence(struct dma_resv *obj, struct dma_fence *fence,
                        enum dma_resv_usage usage)
{
        struct dma_fence_chain *chain;

        if (usage >= DMA_RESV_USAGE_READ) {
                dma_resv_add_shared_fence(obj, fence);
                return;
        }

        chain = dma_fence_chain_alloc();
        if (unlikely(!chain)) {
                /* We are out of memory, block as last resort */
                dma_fence_wait(fence, false);
                return;
        }
        dma_fence_chain_init(chain, dma_resv_excl_fence(obj), dma_fence_get(fence), 1);

        /* Store the usage in the user bit to retrieve it later on */
        chain->base.flags |= usage << DMA_FENCE_FLAG_USER_BITS;

        /* Install the exclusive fence manually */
        write_seqcount_begin(&obj->seq);
        RCU_INIT_POINTER(obj->fence_excl, &chain->base);
        write_seqcount_end(&obj->seq);
}
EXPORT_SYMBOL(dma_resv_add_fence);

/* Restart the iterator by initializing all the necessary fields, but not the
 * relation to the dma_resv object. */
static void dma_resv_iter_restart_unlocked(struct dma_resv_iter *cursor)
{
        cursor->seq = read_seqcount_begin(&cursor->obj->seq);
        cursor->index = -1;
        cursor->shared_count = 0;
	cursor->excl_fence = NULL;
	cursor->kernel_iter = NULL;
        if (cursor->usage >= DMA_RESV_USAGE_READ) {
                cursor->fences = dma_resv_shared_list(cursor->obj);
                if (cursor->fences)
                        cursor->shared_count = cursor->fences->shared_count;
        } else {
                cursor->fences = NULL;
        }
        cursor->is_restarted = true;
}

/* Walk to the next not signaled fence and grab a reference to it */
static void dma_resv_iter_walk_unlocked(struct dma_resv_iter *cursor)
{
        struct dma_resv *obj = cursor->obj;
	struct dma_fence_chain *chain;
	struct dma_fence *f;
	enum dma_resv_usage usage;

        do {
                /* Drop the reference from the previous round */
                dma_fence_put(cursor->fence);

                if (cursor->index == -1) {
			if (cursor->usage >= DMA_RESV_USAGE_WRITE) {
				cursor->fence = dma_resv_excl_fence(obj);
				cursor->index++;
				if (!cursor->fence)
					continue;
			} else {
				cursor->fence = NULL;
				/* Only return KERNEL fences */
				if (!cursor->excl_fence) {
					cursor->excl_fence = dma_resv_excl_fence(obj);
					if (!cursor->excl_fence)
						break;

					cursor->excl_fence = dma_fence_get(cursor->excl_fence);
					cursor->kernel_iter = dma_fence_get(cursor->excl_fence);
				}

				while ((f = cursor->kernel_iter) != NULL) {
					chain = to_dma_fence_chain(f);
					if (!chain) {
						dma_fence_put(f);
						break;
					}

					usage = chain->base.flags >> DMA_FENCE_FLAG_USER_BITS;
					if (usage == DMA_RESV_USAGE_KERNEL && !dma_fence_is_signaled(chain->fence))
						cursor->fence = chain->fence;

					cursor->kernel_iter = dma_fence_chain_walk(f);

					if (cursor->fence)
						break;
				}

				if (!cursor->fence) {
					dma_fence_put(cursor->excl_fence);
					cursor->excl_fence = NULL;
					break;
				}
			}
                } else if (!cursor->fences ||
                           cursor->index >= cursor->shared_count) {
                        cursor->fence = NULL;
                        break;

                } else {
                        struct dma_resv_list *fences = cursor->fences;
                        unsigned int idx = cursor->index++;

                        cursor->fence = rcu_dereference(fences->shared[idx]);
                }
                cursor->fence = dma_fence_get_rcu(cursor->fence);
                if (!cursor->fence || !dma_fence_is_signaled(cursor->fence))
                        break;
        } while (true);
}

/**
 * dma_resv_iter_first_unlocked - first fence in an unlocked dma_resv obj.
 * @cursor: the cursor with the current position
 *
 * Subsequent fences are iterated with dma_resv_iter_next_unlocked().
 *
 * Beware that the iterator can be restarted.  Code which accumulates statistics
 * or similar needs to check for this with dma_resv_iter_is_restarted(). For
 * this reason prefer the locked dma_resv_iter_first() whenver possible.
 *
 * Returns the first fence from an unlocked dma_resv obj.
 */
struct dma_fence *dma_resv_iter_first_unlocked(struct dma_resv_iter *cursor)
{
	bool restart = false;

        rcu_read_lock();
        do {
		if (restart) {
			/* drop reference when iter restart */
			dma_fence_put(cursor->excl_fence);
			dma_fence_put(cursor->kernel_iter);
		}
                dma_resv_iter_restart_unlocked(cursor);
                dma_resv_iter_walk_unlocked(cursor);
		restart = true;
        } while (read_seqcount_retry(&cursor->obj->seq, cursor->seq));
        rcu_read_unlock();

        return cursor->fence;
}
EXPORT_SYMBOL(dma_resv_iter_first_unlocked);

/**
 * dma_resv_iter_next_unlocked - next fence in an unlocked dma_resv obj.
 * @cursor: the cursor with the current position
 *
 * Beware that the iterator can be restarted.  Code which accumulates statistics
 * or similar needs to check for this with dma_resv_iter_is_restarted(). For
 * this reason prefer the locked dma_resv_iter_next() whenver possible.
 *
 * Returns the next fence from an unlocked dma_resv obj.
 */
struct dma_fence *dma_resv_iter_next_unlocked(struct dma_resv_iter *cursor)
{
        bool restart;

        rcu_read_lock();
        cursor->is_restarted = false;
        restart = read_seqcount_retry(&cursor->obj->seq, cursor->seq);
        do {
                if (restart) {
			/* drop reference when iter restart */
			dma_fence_put(cursor->excl_fence);
			dma_fence_put(cursor->kernel_iter);

                        dma_resv_iter_restart_unlocked(cursor);
		}
                dma_resv_iter_walk_unlocked(cursor);
                restart = true;
        } while (read_seqcount_retry(&cursor->obj->seq, cursor->seq));
        rcu_read_unlock();

        return cursor->fence;
}
EXPORT_SYMBOL(dma_resv_iter_next_unlocked);

/**
 * dma_resv_iter_first - first fence from a locked dma_resv object
 * @cursor: cursor to record the current position
 *
 * Subsequent fences are iterated with dma_resv_iter_next_unlocked().
 *
 * Return the first fence in the dma_resv object while holding the
 * &dma_resv.lock.
 */
struct dma_fence *dma_resv_iter_first(struct dma_resv_iter *cursor)
{
        struct dma_fence *fence, *f;
	struct dma_fence_chain *chain;
	enum dma_resv_usage usage;

        dma_resv_assert_held(cursor->obj);

        cursor->index = 0;
        if (cursor->usage >= DMA_RESV_USAGE_READ)
                cursor->fences = dma_resv_shared_list(cursor->obj);
        else
                cursor->fences = NULL;

	cursor->kernel_iter = NULL;
        fence = dma_resv_excl_fence(cursor->obj);
        if (!fence)
                fence = dma_resv_iter_next(cursor);
	else if (cursor->usage == DMA_RESV_USAGE_KERNEL) {
		cursor->kernel_iter = dma_fence_get(fence);
		fence = NULL;

		while ((f = cursor->kernel_iter) != NULL) {
			chain = to_dma_fence_chain(f);
			if (!chain) {
				dma_fence_put(f);
				break;
			}

			cursor->kernel_iter = dma_fence_chain_walk(f);

			usage = chain->base.flags >> DMA_FENCE_FLAG_USER_BITS;
			if (usage == DMA_RESV_USAGE_KERNEL)
				fence = chain->fence;

			if (fence)
				break;
		}
	}

        cursor->is_restarted = true;

        return fence;
}
EXPORT_SYMBOL_GPL(dma_resv_iter_first);

/**
 * dma_resv_iter_next - next fence from a locked dma_resv object
 * @cursor: cursor to record the current position
 *
 * Return the next fences from the dma_resv object while holding the
 * &dma_resv.lock.
 */
struct dma_fence *dma_resv_iter_next(struct dma_resv_iter *cursor)
{
        unsigned int idx;
	struct dma_fence *f;
	struct dma_fence_chain *chain;
	enum dma_resv_usage usage;

        dma_resv_assert_held(cursor->obj);

        cursor->is_restarted = false;

	if (cursor->usage == DMA_RESV_USAGE_KERNEL && cursor->kernel_iter != NULL) {
		while ((f = cursor->kernel_iter) != NULL) {
			chain = to_dma_fence_chain(f);
			if (!chain) {
				dma_fence_put(f);
				break;
			}

			cursor->kernel_iter = dma_fence_chain_walk(f);

			usage = chain->base.flags >> DMA_FENCE_FLAG_USER_BITS;
			if (usage == DMA_RESV_USAGE_KERNEL && chain->fence)
				return chain->fence;
		}
	}

        if (!cursor->fences || cursor->index >= cursor->fences->shared_count)
                return NULL;

        idx = cursor->index++;
        return rcu_dereference_protected(cursor->fences->shared[idx],
                                         dma_resv_held(cursor->obj));
}
EXPORT_SYMBOL_GPL(dma_resv_iter_next);

/**
 * dma_resv_copy_fences - Copy all fences from src to dst.
 * @dst: the destination reservation object
 * @src: the source reservation object
 *
 * Copy all fences from src to dst. dst-lock must be held.
 */
int dma_resv_copy_fences(struct dma_resv *dst, struct dma_resv *src)
{
        struct dma_resv_iter cursor;
        struct dma_resv_list *list;
        struct dma_fence *f, *excl;

        dma_resv_assert_held(dst);

        list = NULL;
        excl = NULL;

        dma_resv_iter_begin(&cursor, src, DMA_RESV_USAGE_READ);
        dma_resv_for_each_fence_unlocked(&cursor, f) {

                if (dma_resv_iter_is_restarted(&cursor)) {
                        dma_resv_list_free(list);
                        dma_fence_put(excl);

                        if (cursor.shared_count) {
                                list = dma_resv_list_alloc(cursor.shared_count);
                                if (!list) {
                                        dma_resv_iter_end(&cursor);
                                        return -ENOMEM;
                                }

                                list->shared_count = 0;

                        } else {
                                list = NULL;
                        }
                        excl = NULL;
                }

                dma_fence_get(f);
                if (dma_resv_iter_is_exclusive(&cursor))
                        excl = f;
                else
                        RCU_INIT_POINTER(list->shared[list->shared_count++], f);
        }
        dma_resv_iter_end(&cursor);

        write_seqcount_begin(&dst->seq);
        excl = rcu_replace_pointer(dst->fence_excl, excl, dma_resv_held(dst));
        list = rcu_replace_pointer(dst->fence, list, dma_resv_held(dst));
        write_seqcount_end(&dst->seq);

        dma_resv_list_free(list);
        dma_fence_put(excl);

        return 0;
}
EXPORT_SYMBOL(dma_resv_copy_fences);

/**
 * dma_resv_get_fences - Get an object's shared and exclusive
 * fences without update side lock held
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 * @num_fences: the number of fences returned
 * @fences: the array of fence ptrs returned (array is krealloc'd to the
 * required size, and must be freed by caller)
 *
 * Retrieve all fences from the reservation object.
 * Returns either zero or -ENOMEM.
 */
int dma_resv_get_fences(struct dma_resv *obj, enum dma_resv_usage usage,
                        unsigned int *num_fences, struct dma_fence ***fences)
{
        struct dma_resv_iter cursor;
        struct dma_fence *fence;

        *num_fences = 0;
        *fences = NULL;

        dma_resv_iter_begin(&cursor, obj, usage);
        dma_resv_for_each_fence_unlocked(&cursor, fence) {

                if (dma_resv_iter_is_restarted(&cursor)) {
                        unsigned int count;

                        while (*num_fences)
                                dma_fence_put((*fences)[--(*num_fences)]);

                        count = cursor.shared_count + 1;

                        /* Eventually re-allocate the array */
                        *fences = krealloc_array(*fences, count,
                                                 sizeof(void *),
                                                 GFP_KERNEL);
                        if (count && !*fences) {
                                dma_resv_iter_end(&cursor);
                                return -ENOMEM;
                        }
                }

                (*fences)[(*num_fences)++] = dma_fence_get(fence);
        }
        dma_resv_iter_end(&cursor);

        return 0;
}
EXPORT_SYMBOL_GPL(dma_resv_get_fences);

/**
 * dma_resv_get_singleton - Get a single fence for all the fences
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 * @fence: the resulting fence
 *
 * Get a single fence representing all the fences inside the resv object.
 * Returns either 0 for success or -ENOMEM.
 *
 * Warning: This can't be used like this when adding the fence back to the resv
 * object since that can lead to stack corruption when finalizing the
 * dma_fence_array.
 *
 * Returns 0 on success and negative error values on failure.
 */
int dma_resv_get_singleton(struct dma_resv *obj, enum dma_resv_usage usage,
                           struct dma_fence **fence)
{
        struct dma_fence_array *array;
        struct dma_fence **fences;
        unsigned count;
        int r;

        r = dma_resv_get_fences(obj, usage, &count, &fences);
        if (r)
                return r;

        if (count == 0) {
                *fence = NULL;
                return 0;
        }

        if (count == 1) {
                *fence = fences[0];
                kfree(fences);
                return 0;
        }

        array = dma_fence_array_create(count, fences,
                                       dma_fence_context_alloc(1),
                                       1, false);
        if (!array) {
                while (count--)
                        dma_fence_put(fences[count]);
                kfree(fences);
                return -ENOMEM;
        }

        *fence = &array->base;
        return 0;
}
EXPORT_SYMBOL_GPL(dma_resv_get_singleton);

/**
 * dma_resv_wait_timeout - Wait on reservation's objects
 * shared and/or exclusive fences.
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 * @intr: if true, do interruptible wait
 * @timeout: timeout value in jiffies or zero to return immediately
 *
 * Callers are not required to hold specific locks, but maybe hold
 * dma_resv_lock() already
 * RETURNS
 * Returns -ERESTARTSYS if interrupted, 0 if the wait timed out, or
 * greater than zer on success.
 */
long dma_resv_wait_timeout(struct dma_resv *obj, enum dma_resv_usage usage,
                           bool intr, unsigned long timeout)
{
        long ret = timeout ? timeout : 1;
        struct dma_resv_iter cursor;
        struct dma_fence *fence;

        dma_resv_iter_begin(&cursor, obj, usage);
        dma_resv_for_each_fence_unlocked(&cursor, fence) {

                ret = dma_fence_wait_timeout(fence, intr, ret);
                if (ret <= 0) {
                        dma_resv_iter_end(&cursor);
                        return ret;
                }
        }
        dma_resv_iter_end(&cursor);

        return ret;
}
EXPORT_SYMBOL_GPL(dma_resv_wait_timeout);

/**
 * dma_resv_test_signaled - Test if a reservation object's fences have been
 * signaled.
 * @obj: the reservation object
 * @usage: controls which fences to include, see enum dma_resv_usage.
 *
 * Callers are not required to hold specific locks, but maybe hold
 * dma_resv_lock() already.
 *
 * RETURNS
 *
 * True if all fences signaled, else false.
 */
bool dma_resv_test_signaled(struct dma_resv *obj, enum dma_resv_usage usage)
{
        struct dma_resv_iter cursor;
        struct dma_fence *fence;

        dma_resv_iter_begin(&cursor, obj, usage);
        dma_resv_for_each_fence_unlocked(&cursor, fence) {
                dma_resv_iter_end(&cursor);
                return false;
        }
        dma_resv_iter_end(&cursor);
        return true;
}
EXPORT_SYMBOL_GPL(dma_resv_test_signaled);

/**
 * dma_resv_describe - Dump description of the resv object into seq_file
 * @obj: the reservation object
 * @seq: the seq_file to dump the description into
 *
 * Dump a textual description of the fences inside an dma_resv object into the
 * seq_file.
 */
void dma_resv_describe(struct dma_resv *obj, struct seq_file *seq)
{
        static const char *usage[] = { "write", "read" };
        struct dma_resv_iter cursor;
        struct dma_fence *fence;

        dma_resv_for_each_fence(&cursor, obj, DMA_RESV_USAGE_READ, fence) {
                seq_printf(seq, "\t%s fence:",
                           usage[dma_resv_iter_usage(&cursor)]);
                dma_fence_describe(fence, seq);
        }
}
EXPORT_SYMBOL_GPL(dma_resv_describe);

#if IS_ENABLED(CONFIG_LOCKDEP)
static int __init dma_resv_lockdep(void)
{
        struct mm_struct *mm = mm_alloc();
        struct ww_acquire_ctx ctx;
        struct dma_resv obj;
        struct address_space mapping;
        int ret;

        if (!mm)
                return -ENOMEM;

        dma_resv_init(&obj);
        address_space_init_once(&mapping);

        mmap_read_lock(mm);
        ww_acquire_init(&ctx, &reservation_ww_class);
        ret = dma_resv_lock(&obj, &ctx);
        if (ret == -EDEADLK)
                dma_resv_lock_slow(&obj, &ctx);
        fs_reclaim_acquire(GFP_KERNEL);
        /* for unmap_mapping_range on trylocked buffer objects in shrinkers */
        i_mmap_lock_write(&mapping);
        i_mmap_unlock_write(&mapping);
#ifdef CONFIG_MMU_NOTIFIER
        lock_map_acquire(&__mmu_notifier_invalidate_range_start_map);
        __dma_fence_might_wait();
        lock_map_release(&__mmu_notifier_invalidate_range_start_map);
#else
        __dma_fence_might_wait();
#endif
        fs_reclaim_release(GFP_KERNEL);
        ww_mutex_unlock(&obj.lock);
        ww_acquire_fini(&ctx);
        mmap_read_unlock(mm);

        mmput(mm);

        return 0;
}
subsys_initcall(dma_resv_lockdep);
#endif




#endif
