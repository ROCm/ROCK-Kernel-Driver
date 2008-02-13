/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/configfs.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include <linux/time.h>

#include "heartbeat.h"
#include "tcp.h"
#include "nodemanager.h"
#include "quorum.h"

#include "masklog.h"


/*
 * The first heartbeat pass had one global thread that would serialize all hb
 * callback calls.  This global serializing sem should only be removed once
 * we've made sure that all callees can deal with being called concurrently
 * from multiple hb region threads.
 */
static DECLARE_RWSEM(o2hb_callback_sem);

/*
 * multiple hb threads are watching multiple regions.  A node is live
 * whenever any of the threads sees activity from the node in its region.
 */
spinlock_t o2hb_live_lock = SPIN_LOCK_UNLOCKED;
EXPORT_SYMBOL_GPL(o2hb_live_lock);
static LIST_HEAD(o2hb_node_events);
static LIST_HEAD(o2hb_all_regions);
static DEFINE_SPINLOCK(o2hb_group_lock);
static LIST_HEAD(o2hb_group_list);
static struct o2hb_heartbeat_group *o2hb_active_group;

static struct o2hb_callback {
	struct list_head list;
} o2hb_callbacks[O2HB_NUM_CB];

static struct o2hb_callback *hbcall_from_type(enum o2hb_callback_type type);

static void o2hb_fire_callbacks(struct o2hb_callback *hbcall,
                                struct o2hb_node_event *event)
{
	struct list_head *iter;
	struct o2hb_callback_func *f;
	struct o2nm_node *node = event->hn_node;
	int idx = event->hn_node_num;

	list_for_each(iter, &hbcall->list) {
		f = list_entry(iter, struct o2hb_callback_func, hc_item);
		mlog(ML_HEARTBEAT, "calling funcs %p\n", f);
		if (f->hc_res == NULL || event->hn_res == NULL ||
		    f->hc_res == event->hn_res)
			(f->hc_func)(node, idx, f->hc_data);
	}
}

/* Will run the list in order until we process the passed event */
void o2hb_run_event_list(struct o2hb_node_event *queued_event)
{
	int empty;
	struct o2hb_callback *hbcall;
	struct o2hb_node_event *event;

	spin_lock(&o2hb_live_lock);
	empty = list_empty(&queued_event->hn_item);
	spin_unlock(&o2hb_live_lock);
	if (empty)
		return;

	/* Holding callback sem assures we don't alter the callback
	 * lists when doing this, and serializes ourselves with other
	 * processes wanting callbacks. */
	down_write(&o2hb_callback_sem);

	spin_lock(&o2hb_live_lock);
	while (!list_empty(&o2hb_node_events)
	       && !list_empty(&queued_event->hn_item)) {
		event = list_entry(o2hb_node_events.next,
				   struct o2hb_node_event,
				   hn_item);
		list_del_init(&event->hn_item);
		spin_unlock(&o2hb_live_lock);

		mlog(ML_HEARTBEAT, "Node %s event for %d\n",
		     event->hn_event_type == O2HB_NODE_UP_CB ? "UP" : "DOWN",
		     event->hn_node_num);

		hbcall = hbcall_from_type(event->hn_event_type);

		/* We should *never* have gotten on to the list with a
		 * bad type... This isn't something that we should try
		 * to recover from. */
		BUG_ON(IS_ERR(hbcall));

		o2hb_fire_callbacks(hbcall, event);

		spin_lock(&o2hb_live_lock);
	}
	spin_unlock(&o2hb_live_lock);

	up_write(&o2hb_callback_sem);
}
EXPORT_SYMBOL_GPL(o2hb_run_event_list);

void o2hb_queue_node_event(struct o2hb_node_event *event,
				  enum o2hb_callback_type type,
				  struct o2nm_node *node,
				  int node_num)
{
	assert_spin_locked(&o2hb_live_lock);

	event->hn_event_type = type;
	event->hn_node = node;
	event->hn_node_num = node_num;

	mlog(ML_HEARTBEAT, "Queue node %s event for node %d\n",
	     type == O2HB_NODE_UP_CB ? "UP" : "DOWN", node_num);

	list_add_tail(&event->hn_item, &o2hb_node_events);
}
EXPORT_SYMBOL_GPL(o2hb_queue_node_event);

void o2hb_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(o2hb_callbacks); i++)
		INIT_LIST_HEAD(&o2hb_callbacks[i].list);

	INIT_LIST_HEAD(&o2hb_node_events);
}

int o2hb_fill_node_map_from_callback(const char *resource, unsigned long *map,
                                     unsigned bytes)
{
	return o2hb_active_group->fill_node_map(resource, map, bytes);
}

/*
 * get a map of all nodes that are heartbeating in any regions
 */
int o2hb_fill_node_map(const char *resource, unsigned long *map, unsigned bytes)
{
	/* callers want to serialize this map and callbacks so that they
	 * can trust that they don't miss nodes coming to the party */
	int err;
	down_read(&o2hb_callback_sem);
	spin_lock(&o2hb_live_lock);
	err = o2hb_fill_node_map_from_callback(resource, map, bytes);
	spin_unlock(&o2hb_live_lock);
	up_read(&o2hb_callback_sem);
	return err;
}
EXPORT_SYMBOL_GPL(o2hb_fill_node_map);


const char *o2hb_heartbeat_mode(void)
{
	const char *ret = "";
	spin_lock(&o2hb_group_lock);
	if (o2hb_active_group)
		ret = o2hb_active_group->hs_name;
	spin_unlock(&o2hb_group_lock);
	return ret;
}

int o2hb_set_heartbeat_mode(const char *type, size_t count)
{
	int err = -EINVAL;
	spin_lock(&o2hb_group_lock);
	if (o2hb_active_group && atomic_read(&o2hb_active_group->hs_count)) {
		err = -EBUSY;
	} else {
		struct o2hb_heartbeat_group *hs;
		list_for_each_entry(hs, &o2hb_group_list, hs_list) {
			if (!strncmp(hs->hs_name, type, count - 1)) {
				o2hb_active_group = hs;
				err = count;
				break;
			}
		}
	}
	spin_unlock(&o2hb_group_lock);
	return err;
}

/* this is just here to avoid touching group in heartbeat.h which the
 * entire damn world #includes */
struct config_group *o2hb_alloc_hb_set(void)
{
	struct o2hb_heartbeat_group *hs = NULL;
	int err = 0;

	spin_lock(&o2hb_group_lock);
	hs = o2hb_active_group;
	if (hs == NULL) {
		printk("o2hb: No group types have been associated with "
		       "heartbeat. Please load a group type module.\n");
		spin_unlock(&o2hb_group_lock);
		return NULL;
	}
	if ((err = try_module_get(hs->hs_type.ct_owner)))
		atomic_inc(&hs->hs_count);
	spin_unlock(&o2hb_group_lock);

	if (err == 0)
		return NULL;

	config_group_init_type_name(&hs->hs_group, "heartbeat", &hs->hs_type);
	if (hs->init)
		err = hs->init(hs);
	return &hs->hs_group;
}

void o2hb_free_hb_set(struct config_group *group)
{
	if (group) {
		struct o2hb_heartbeat_group *hs = to_o2hb_heartbeat_group(group);
		if (hs->exit)
			hs->exit(hs);

		atomic_dec(&hs->hs_count);
		module_put(hs->hs_type.ct_owner);
	}
}

/* hb callback registration and issueing */

static struct o2hb_callback *hbcall_from_type(enum o2hb_callback_type type)
{
	if (type == O2HB_NUM_CB)
		return ERR_PTR(-EINVAL);

	return &o2hb_callbacks[type];
}

void o2hb_setup_callback(struct o2hb_callback_func *hc,
			 enum o2hb_callback_type type,
			 o2hb_cb_func *func,
			 void *data,
			 int priority)
{
	INIT_LIST_HEAD(&hc->hc_item);
	hc->hc_func = func;
	hc->hc_data = data;
	hc->hc_priority = priority;
	hc->hc_type = type;
	hc->hc_magic = O2HB_CB_MAGIC;
	hc->hc_res = NULL;
}
EXPORT_SYMBOL_GPL(o2hb_setup_callback);

static struct o2hb_heartbeat_resource *o2hb_find_resource(const char *region_uuid)
{
	struct o2hb_heartbeat_resource *p, *reg = NULL;

	assert_spin_locked(&o2hb_live_lock);

	list_for_each_entry(p, &o2hb_all_regions, hr_all_item) {
		if (!strcmp(region_uuid, config_item_name(&p->hr_item))) {
			reg = p;
			break;
		}
	}

	return reg;
}

struct o2hb_heartbeat_resource *
o2hb_resource_get(const char *region_uuid)
{
	int ret = 0;
	struct o2hb_heartbeat_resource *reg;

	spin_lock(&o2hb_live_lock);

	reg = o2hb_find_resource(region_uuid);
	if (!reg)
		ret = -ENOENT;
	spin_unlock(&o2hb_live_lock);

	if (ret)
		goto out;

	ret = o2nm_depend_this_node();
	if (ret)
		goto out;

	ret = o2nm_depend_item(&reg->hr_item);
	if (ret)
		o2nm_undepend_this_node();

out:
	if (ret)
		return ERR_PTR(ret);
	return reg;
}
EXPORT_SYMBOL_GPL(o2hb_resource_get);

void o2hb_resource_put(struct o2hb_heartbeat_resource *reg)
{
	if (reg) {
		o2nm_undepend_item(&reg->hr_item);
		o2nm_undepend_this_node();
	}
}
EXPORT_SYMBOL_GPL(o2hb_resource_put);

void o2hb_add_resource(struct o2hb_heartbeat_resource *res)
{
	spin_lock(&o2hb_live_lock);
	list_add_tail(&res->hr_all_item, &o2hb_all_regions);
	spin_unlock(&o2hb_live_lock);
}
EXPORT_SYMBOL_GPL(o2hb_add_resource);

void o2hb_remove_resource(struct o2hb_heartbeat_resource *res)
{
	spin_lock(&o2hb_live_lock);
	list_del(&res->hr_all_item);
	spin_unlock(&o2hb_live_lock);
}
EXPORT_SYMBOL_GPL(o2hb_remove_resource);

int __o2hb_active_resources(void)
{
	return !list_empty(&o2hb_all_regions);
}
EXPORT_SYMBOL_GPL(o2hb_active_resources);

void o2hb_for_each_resource(o2hb_action_fn func, void *data)
{
	struct o2hb_heartbeat_resource *res;
	spin_lock(&o2hb_live_lock);
	list_for_each_entry(res, &o2hb_all_regions, hr_all_item)
		func(res, data);
	spin_unlock(&o2hb_live_lock);
}
EXPORT_SYMBOL_GPL(o2hb_for_each_resource);

int o2hb_register_callback(const char *region_uuid,
			   struct o2hb_callback_func *hc)
{
	struct o2hb_heartbeat_resource *res = NULL;
	struct o2hb_callback_func *tmp;
	struct list_head *iter;
	struct o2hb_callback *hbcall;
	int ret;

	BUG_ON(hc->hc_magic != O2HB_CB_MAGIC);
	BUG_ON(!list_empty(&hc->hc_item));

	hbcall = hbcall_from_type(hc->hc_type);
	if (IS_ERR(hbcall)) {
		ret = PTR_ERR(hbcall);
		goto out;
	}

	if (region_uuid) {
		res = o2hb_resource_get(region_uuid);
		if (IS_ERR(res)) {
			ret = PTR_ERR(res);
			goto out;
		}
		hc->hc_res = res;
	}

	down_write(&o2hb_callback_sem);

	list_for_each(iter, &hbcall->list) {
		tmp = list_entry(iter, struct o2hb_callback_func, hc_item);
		if (hc->hc_priority < tmp->hc_priority) {
			list_add_tail(&hc->hc_item, iter);
			break;
		}
	}
	if (list_empty(&hc->hc_item))
		list_add_tail(&hc->hc_item, &hbcall->list);

	up_write(&o2hb_callback_sem);
	ret = 0;
out:
	mlog(ML_HEARTBEAT, "returning %d on behalf of %p for funcs %p\n",
	     ret, __builtin_return_address(0), hc);
	return ret;
}
EXPORT_SYMBOL_GPL(o2hb_register_callback);

void o2hb_unregister_callback(struct o2hb_callback_func *hc)
{
	BUG_ON(hc->hc_magic != O2HB_CB_MAGIC);

	mlog(ML_HEARTBEAT, "on behalf of %p for funcs %p\n",
	     __builtin_return_address(0), hc);

	/* XXX Can this happen _with_ a region reference? */
	if (list_empty(&hc->hc_item))
		return;

	if (hc->hc_res) {
		o2hb_resource_put(hc->hc_res);
		hc->hc_res = NULL;
	}

	down_write(&o2hb_callback_sem);

	list_del_init(&hc->hc_item);

	up_write(&o2hb_callback_sem);
}
EXPORT_SYMBOL_GPL(o2hb_unregister_callback);

int o2hb_register_heartbeat_group(struct o2hb_heartbeat_group *group)
{
	spin_lock(&o2hb_group_lock);
	if (list_empty(&o2hb_group_list))
		o2hb_active_group = group;
	list_add(&group->hs_list, &o2hb_group_list);
	atomic_set(&group->hs_count, 0);
	spin_unlock(&o2hb_group_lock);

	printk("o2cb heartbeat: registered %s mode\n", group->hs_name);
	return 0;
}
EXPORT_SYMBOL_GPL(o2hb_register_heartbeat_group);

int o2hb_unregister_heartbeat_group(struct o2hb_heartbeat_group *group)
{
	spin_lock(&o2hb_group_lock);
	if (o2hb_active_group == group)
		o2hb_active_group = NULL;
	list_del_init(&group->hs_list);
	spin_unlock(&o2hb_group_lock);
	printk("o2cb heartbeat: unregistered %s mode\n", group->hs_name);
	return 0;
}
EXPORT_SYMBOL_GPL(o2hb_unregister_heartbeat_group);

static int __o2hb_check_node_heartbeating(const char *resource, u8 node_num,
                                          int need_lock)
{
	int ret = 0;
	if (need_lock) {
		down_read(&o2hb_callback_sem);
		spin_lock(&o2hb_live_lock);
	}

	if (o2hb_active_group->check_node_status) {
		ret = o2hb_active_group->check_node_status(resource, node_num);
	} else {
		unsigned long testing_map[BITS_TO_LONGS(O2NM_MAX_NODES)];
		if( o2hb_fill_node_map_from_callback(resource, testing_map,
		                                     sizeof(testing_map)) == 0)
			ret = test_bit(node_num, testing_map);
	}

	if (need_lock) {
		spin_unlock(&o2hb_live_lock);
		up_read(&o2hb_callback_sem);
	}

	if (!ret)
		mlog(ML_HEARTBEAT,
		     "node (%u) does not have heartbeating enabled.\n",
		     node_num);

	return ret;
}

int o2hb_check_node_heartbeating(const char *resource, u8 node_num)
{
	return __o2hb_check_node_heartbeating(resource, node_num, 1);
}
EXPORT_SYMBOL_GPL(o2hb_check_node_heartbeating);

int o2hb_check_node_heartbeating_from_callback(const char *resource,
                                               u8 node_num)
{
	return __o2hb_check_node_heartbeating(resource, node_num, 0);
}
EXPORT_SYMBOL_GPL(o2hb_check_node_heartbeating_from_callback);

/* Makes sure our local node is configured with a node number, and is
 * heartbeating. */
int o2hb_check_local_node_heartbeating(const char *resource)
{
	return __o2hb_check_node_heartbeating(resource, o2nm_this_node(), 1);
}
EXPORT_SYMBOL_GPL(o2hb_check_local_node_heartbeating);
