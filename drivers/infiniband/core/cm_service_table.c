/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: cm_service_table.c 32 2004-04-09 03:57:42Z roland $
*/

#include "cm_priv.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/random.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#else
#include <os_dep/win/linux/string.h>
#include <os_dep/win/linux/spinlock.h>
#endif

#define TS_SERVICE_POISON ((void *) 0x00100100)
#if defined(TS_SERVICE_POISON)
#  define TS_USE_SERVICE_POISON 1
#else
#  define TS_USE_SERVICE_POISON 0
#  define TS_SERVICE_POISON     0
#endif

struct ib_cm_tree_node {
	tTS_IB_SERVICE_ID         id;
	tTS_IB_SERVICE_ID         mask;
	uint64_t                  bit;

	struct ib_cm_tree_node   *parent;
	int                       child_num;

	union {
		struct ib_cm_tree_node *child[2];
		struct ib_cm_service   *service;
	} ptr;
};

/* XXX this #if must be removed */
#ifndef W2K_OS
static kmem_cache_t *service_cache;
static kmem_cache_t *node_cache;
#else
static PNPAGED_LOOKASIDE_LIST service_cache;
static PNPAGED_LOOKASIDE_LIST  node_cache;
#endif

static tTS_IB_SERVICE_ID local_service_id;

static struct ib_cm_tree_node *service_tree;
static spinlock_t          tree_lock = SPIN_LOCK_UNLOCKED;

static struct ib_cm_tree_node *ib_cm_tree_search(tTS_IB_SERVICE_ID service_id,
                                                 tTS_IB_SERVICE_ID service_mask)
{
	struct ib_cm_tree_node *node = service_tree;

	while (node) {
		if (service_mask <= node->mask)
			return node;

		if (node->id != (service_id & node->mask))
			return node;

		if (!node->bit)
			return node;

		node = node->ptr.child[!!(node->bit & service_id)];
	}

	return node;
}

tTS_IB_SERVICE_ID ib_cm_service_assign(void)
{
	tTS_IB_SERVICE_ID ret;
	struct ib_cm_tree_node *node;
	TS_WINDOWS_SPINLOCK_FLAGS

		spin_lock(&tree_lock);

	while (1) {
		ret = local_service_id;
		local_service_id = ((local_service_id + 1) & 0x0fffffffull) | 0x20000000ull;
		node = ib_cm_tree_search(ret, TS_IB_CM_SERVICE_EXACT_MASK);
		if (!node || node->bit || node->id != (ret & node->mask))
			break;
	}

	spin_unlock(&tree_lock);

	return ret;
}

struct ib_cm_service *ib_cm_service_find(tTS_IB_SERVICE_ID service_id)
{
	struct ib_cm_tree_node *node;
	struct ib_cm_service   *service;
	TS_WINDOWS_SPINLOCK_FLAGS

		spin_lock(&tree_lock);

	node = ib_cm_tree_search(service_id, TS_IB_CM_SERVICE_EXACT_MASK);

	if (!node || node->bit) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Failed to find service 0x%016" TS_U64_FMT "x",
			       service_id);
		goto out;
	}
	if (node->id != (service_id & node->mask)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "service 0x%016" TS_U64_FMT "x doesn't match "
			       "0x%016" TS_U64_FMT "x/0x%016" TS_U64_FMT "x",
			       service_id, node->id, node->mask);
		goto out;
	}

	service = node->ptr.service;
	atomic_inc(&service->waiters);

	spin_unlock(&tree_lock);

	down(&service->mutex);

	if (!service->freeing) {
		atomic_dec(&service->waiters);
		return service;
	} else {
		up(&service->mutex);
		atomic_dec(&service->waiters);
		return NULL;
	}

 out:
	spin_unlock(&tree_lock);
	return NULL;
}

int ib_cm_service_create(tTS_IB_SERVICE_ID  service_id,
			 tTS_IB_SERVICE_ID  service_mask,
			 struct ib_cm_service **service)
{
	struct ib_cm_tree_node *node;
	struct ib_cm_tree_node *new_node = NULL;
	struct ib_cm_tree_node *new_parent = NULL;
	uint64_t bit, mask;
	int child_num;
	int ret;
	TS_WINDOWS_SPINLOCK_FLAGS

		*service = NULL;

	new_node   = kmem_cache_alloc(node_cache,    GFP_KERNEL);
	new_parent = kmem_cache_alloc(node_cache,    GFP_KERNEL);
	*service   = kmem_cache_alloc(service_cache, GFP_KERNEL);

	if (!new_node || !new_parent || !*service) {
		ret = -ENOMEM;
		goto out_free;
	}

	new_node->id          = service_id;
	new_node->mask        = service_mask;
	new_node->bit         = 0;
	new_node->ptr.service = *service;

	spin_lock(&tree_lock);

	node = ib_cm_tree_search(service_id, service_mask);

	if (node) {
		if ((service_mask & node->id) == (node->mask & service_id)) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "Conflict between %016" TS_U64_FMT "x/%016" TS_U64_FMT "x "
				       "and %016" TS_U64_FMT "x/%016" TS_U64_FMT "x",
				       node->id, node->mask,
				       service_id, service_mask);
			ret = -EADDRINUSE;
			goto out;
		}

		/* find the first bit where we're different -- if there isn't one,
		   then we conflict with the previous service */
		for (mask = 0x0ULL, bit = 0x8000000000000000ULL;
		     bit;
		     mask |= bit, bit >>= 1) {
			if ((bit & service_id) != (bit & node->id))
				break;
		}

		if (!bit || mask >= node->mask) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "Couldn't find a difference: %016" TS_U64_FMT "x/%016" TS_U64_FMT "x "
				       "and %016" TS_U64_FMT "x/%016" TS_U64_FMT "x",
				       node->id, node->mask,
				       service_id, service_mask);
			ret = -EINVAL;
			goto out;
		}

		new_parent->id        = service_id & mask;
		new_parent->mask      = mask;
		new_parent->bit       = bit;
		new_parent->parent    = node->parent;
		new_parent->child_num = node->child_num;
		if (node->parent) {
			node->parent->ptr.child[node->child_num] = new_parent;
		} else {
			service_tree = new_parent;
		}

		child_num = !!(bit & service_id);
		new_parent->ptr.child[ child_num] = new_node;
		new_node->parent                  = new_parent;
		new_node->child_num               = child_num;
		new_parent->ptr.child[!child_num] = node;
		node->parent                      = new_parent;
		node->child_num                   = !child_num;
	} else {
		if (service_tree) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "No parent found but tree not empty!");
			ret = -EINVAL;
			goto out;
		}

		/* Don't need the new parent node for the first service we add */
		kmem_cache_free(node_cache, new_parent);

		service_tree        = new_node;
		new_node->parent    = NULL;
	}

	(*service)->node    = new_node;
	(*service)->freeing = 0;
	init_MUTEX_LOCKED(&(*service)->mutex);

	atomic_set(&(*service)->waiters, 0);

	spin_unlock(&tree_lock);

	return 0;

 out:
	spin_unlock(&tree_lock);

 out_free:
	if (new_node)
		kmem_cache_free(node_cache, new_node);

	if (new_parent)
		kmem_cache_free(node_cache, new_parent);

	if (*service)
		kmem_cache_free(service_cache, *service);

	return ret;
}

void ib_cm_service_free(struct ib_cm_service *service)
{
	struct ib_cm_tree_node *node;
	struct ib_cm_tree_node *sibling;
	TS_WINDOWS_SPINLOCK_FLAGS

		if (TS_USE_SERVICE_POISON && service->node == TS_SERVICE_POISON) {
			TS_REPORT_WARN(MOD_IB_CM,
				       "Double free of service at %p",
				       service);
		}

	down(&service->mutex);
	/* Unlink service from the tree */
	spin_lock(&tree_lock);

	node = service->node;
	if (node->parent) {
		sibling         = node->parent->ptr.child[!node->child_num];
		sibling->parent = node->parent->parent;
		if (sibling->parent) {
			sibling->parent->ptr.child[node->parent->child_num] = sibling;
			sibling->child_num = node->parent->child_num;
		} else {
			service_tree = sibling;
		}
		kmem_cache_free(node_cache, node->parent);
	} else {
		service_tree = NULL;
	}

	spin_unlock(&tree_lock);

	service->freeing = 1;
	if (TS_USE_SERVICE_POISON)
		service->node = TS_SERVICE_POISON;
	up(&service->mutex);

	/* XXX this #if must be removed */
#ifndef W2K_OS
	while (atomic_read(&service->waiters)) {
		set_current_state(TASK_RUNNING);
		schedule();
	}
#endif

	kmem_cache_free(node_cache,    node);
	kmem_cache_free(service_cache, service);
}

static void ib_cm_service_constructor(
	void *service_ptr,
	/* XXX this #if must be removed */
#ifndef W2K_OS
	kmem_cache_t *cache,
#else
	PNPAGED_LOOKASIDE_LIST cache,
#endif
	unsigned long flags
	) {
	struct ib_cm_service *service = service_ptr;

	if (TS_USE_SERVICE_POISON) {
		service->node = TS_SERVICE_POISON;
	}
}

void ib_cm_service_table_init(void)
{
	get_random_bytes(&local_service_id, sizeof local_service_id);

	/* set top byte to 0x02 to indicate locally administered service ID */
	local_service_id = (local_service_id & 0x0fffffffull) | 0x20000000ull;

#define WORD_ALIGN(x) (((x) + sizeof (void *) - 1) & ~(sizeof (void *) - 1))
	/* XXX this #if must be removed */
#ifndef W2K_OS
	service_cache = kmem_cache_create("ib_cm_service",
					  WORD_ALIGN(sizeof (struct ib_cm_service)),
					  0,
					  SLAB_HWCACHE_ALIGN,
					  ib_cm_service_constructor,
					  NULL);

	node_cache    = kmem_cache_create("ib_cm_node",
					  WORD_ALIGN(sizeof (struct ib_cm_tree_node)),
					  0,
					  SLAB_HWCACHE_ALIGN,
					  NULL,
					  NULL);
#else
	service_cache = kmalloc(sizeof(NPAGED_LOOKASIDE_LIST),NULL);
	ExInitializeNPagedLookasideList(service_cache, NULL, NULL, 0,
					sizeof(struct ib_cm_service),'1mc',0);

	node_cache = kmalloc(sizeof(NPAGED_LOOKASIDE_LIST),NULL);
	ExInitializeNPagedLookasideList(node_cache, NULL, NULL, 0,
					sizeof(struct ib_cm_tree_node),'2mc',0);

#endif
}

void ib_cm_service_table_cleanup(void)
{
	/* XXX this #if must be removed */
#ifndef W2K_OS
	if (kmem_cache_destroy(service_cache)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Failed to destroy CM service slab cache (memory leak?)");
	}
	if (kmem_cache_destroy(node_cache)) {
		TS_REPORT_WARN(MOD_IB_CM,
			       "Failed to destroy CM tree node slab cache (memory leak?)");
	}
#else
	ExDeleteNPagedLookasideList(service_cache);
	ExDeleteNPagedLookasideList(node_cache);
#endif
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
