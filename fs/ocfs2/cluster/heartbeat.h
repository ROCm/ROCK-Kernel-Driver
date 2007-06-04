/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * heartbeat.h
 *
 * Function prototypes
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 *
 */

#ifndef O2CLUSTER_HEARTBEAT_H
#define O2CLUSTER_HEARTBEAT_H

#include <linux/configfs.h>
#include <asm/semaphore.h>
#include "nodemanager.h"
#include "ocfs2_heartbeat.h"

#define O2HB_CB_MAGIC		0x51d1e4ec

/* callback stuff */
enum o2hb_callback_type {
	O2HB_NODE_DOWN_CB = 0,
	O2HB_NODE_UP_CB,
	O2HB_NUM_CB
};

struct o2hb_heartbeat_group {
	struct config_group hs_group;
	struct config_item_type hs_type;
	const char *hs_name;
	int (*init)(struct o2hb_heartbeat_group *hs);
	void (*exit)(struct o2hb_heartbeat_group *hs);
	int (*fill_node_map)(const char *resource, unsigned long *map,
	                     size_t bytes);
	int (*check_node_status)(const char *resource, u8 node_num);
	atomic_t hs_count;
	struct list_head hs_list;
};

struct o2hb_heartbeat_resource {
	struct config_item hr_item;
};

struct o2nm_node;
typedef void (o2hb_cb_func)(struct o2nm_node *, int, void *);

extern spinlock_t o2hb_live_lock;

struct o2hb_callback_func {
	u32			hc_magic;
	struct list_head	hc_item;
	o2hb_cb_func		*hc_func;
	void			*hc_data;
	int			hc_priority;
	enum o2hb_callback_type hc_type;
	struct o2hb_heartbeat_resource *hc_res;
};

struct o2hb_node_event {
	struct list_head        hn_item;
	enum o2hb_callback_type hn_event_type;
	struct o2nm_node        *hn_node;
	int                     hn_node_num;
	struct o2hb_heartbeat_resource *hn_res;
};
void o2hb_queue_node_event(struct o2hb_node_event *event,
                           enum o2hb_callback_type type,
                           struct o2nm_node *node, int node_num);
void o2hb_run_event_list(struct o2hb_node_event *queued_event);

int o2hb_register_heartbeat_group(struct o2hb_heartbeat_group *group);
int o2hb_unregister_heartbeat_group(struct o2hb_heartbeat_group *group);

struct config_group *o2hb_alloc_hb_set(void);
void o2hb_free_hb_set(struct config_group *group);

void o2hb_setup_callback(struct o2hb_callback_func *hc,
			 enum o2hb_callback_type type,
			 o2hb_cb_func *func,
			 void *data,
			 int priority, struct o2hb_heartbeat_resource *res);
int o2hb_register_callback(struct o2hb_callback_func *hc);
void o2hb_unregister_callback(struct o2hb_callback_func *hc);
int o2hb_fill_node_map(const char *resource, unsigned long *map,
			unsigned bytes);
void o2hb_init(void);
int o2hb_check_node_heartbeating(const char *resource, u8 node_num);
int o2hb_check_node_heartbeating_from_callback(const char *resource,
                                               u8 node_num);
int o2hb_check_local_node_heartbeating(const char *resource);

const char *o2hb_heartbeat_mode(void);
int o2hb_set_heartbeat_mode(const char *type, size_t count);

struct o2hb_heartbeat_resource *o2hb_heartbeat_resource_get_by_name(const char * name);

static inline struct o2hb_heartbeat_group *to_o2hb_heartbeat_group(struct config_group *group)
{
	return container_of(group, struct o2hb_heartbeat_group, hs_group);
}

static inline struct o2hb_heartbeat_resource *to_o2hb_heartbeat_resource(struct config_item *item)
{
	return container_of(item, struct o2hb_heartbeat_resource, hr_item);
}

static inline void o2hb_heartbeat_resource_get(struct o2hb_heartbeat_resource *hbres)
{
	config_item_get(&hbres->hr_item);
}

static inline void o2hb_heartbeat_resource_put(struct o2hb_heartbeat_resource *hbres)
{
	config_item_put(&hbres->hr_item);
}
#endif /* O2CLUSTER_HEARTBEAT_H */
