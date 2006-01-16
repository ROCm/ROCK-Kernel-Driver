/*
 * Device Mapper Netlink Support
 *
 * Copyright (C) 2005 IBM Corporation
 * 	Author: Mike Anderson <andmike@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#ifndef DM_NETLINK_H
#define DM_NETLINK_H

struct dm_evt {
	struct list_head zlist;
	struct list_head elist;
	struct sk_buff *skb;
};

#ifdef CONFIG_DM_NL_EVT
void dm_send_evt(struct dm_evt *);
struct dm_evt *dm_path_fail_evt(char*, int);
struct dm_evt *dm_path_reinstate_evt(char*);
int dm_nl_init(void);
void dm_nl_exit(void);
#else
static inline void dm_send_evt(struct dm_evt *evt)
{
}
static inline struct dm_evt *dm_path_fail_evt(char* dm_name, int blk_err)
{
	return NULL;
}
static inline struct dm_evt *dm_path_reinstate_evt(char* dm_name)
{
	return NULL;
}
static inline int __init dm_nl_init(void)
{
	return 0;
}
static inline void dm_nl_exit(void)
{
}
#endif

#endif /* DM_NETLINK_H */
