/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * proc.c
 *
 * proc interface
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/socket.h>

#define MLOG_MASK_PREFIX ML_SUPER
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "proc.h"
#include "alloc.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "ver.h"

#define OCFS2_PROC_BASENAME    "fs/ocfs2"

static struct proc_dir_entry *ocfs2_proc_root_dir = NULL; /* points to /proc/fs/ocfs2 */

static int ocfs2_proc_version(char *page,
			      char **start,
			      off_t off,
			      int count,
			      int *eof,
			      void *data);
static int ocfs2_proc_nodenum(char *page,
			      char **start,
			      off_t off,
			      int count,
			      int *eof,
			      void *data);
static int ocfs2_proc_slotnum(char *page,
			      char **start,
			      off_t off,
			      int count,
			      int *eof,
			      void *data);
static int ocfs2_proc_nodename(char *page,
			       char **start,
			       off_t off,
			       int count,
			       int *eof,
			       void *data);
static int ocfs2_proc_uuid(char *page,
			   char **start,
			   off_t off,
			   int count,
			   int *eof,
			   void *data);
static int ocfs2_proc_statistics(char *page,
				 char **start,
				 off_t off,
				 int count,
				 int *eof,
				 void *data);
static int ocfs2_proc_device(char *page,
			     char **start,
			     off_t off,
			     int count,
			     int *eof,
			     void *data);
static int ocfs2_proc_nodes(char *page,
			    char **start,
			    off_t off,
			    int count,
			    int *eof,
			    void *data);
static int ocfs2_proc_alloc_stat(char *page,
				 char **start,
				 off_t off,
				 int count,
				 int *eof,
				 void *data);
static int ocfs2_proc_label(char *page,
			    char **start,
			    off_t off,
			    int count,
			    int *eof,
			    void *data);

typedef struct _ocfs2_proc_list
{
	char *name;
	char *data;
	int (*read_proc) (char *, char **, off_t, int, int *, void *);
	write_proc_t *write_proc;
	mode_t mode;
} ocfs2_proc_list;

static ocfs2_proc_list top_dir[] = {
	{ "version", NULL, ocfs2_proc_version, NULL, S_IFREG | S_IRUGO, },
	{ "nodename", NULL, ocfs2_proc_nodename, NULL, S_IFREG | S_IRUGO, },
	{ NULL }
};

static ocfs2_proc_list sub_dir[] = {
	{ "nodenum", NULL, ocfs2_proc_nodenum, NULL, S_IFREG | S_IRUGO, },
	{ "uuid", NULL, ocfs2_proc_uuid, NULL, S_IFREG | S_IRUGO, },
	{ "slotnum", NULL, ocfs2_proc_slotnum, NULL, S_IFREG | S_IRUGO, },
	{ "statistics", NULL, ocfs2_proc_statistics, NULL, S_IFREG | S_IRUGO, },
	{ "device", NULL, ocfs2_proc_device, NULL, S_IFREG | S_IRUGO, },
	{ "nodes", NULL, ocfs2_proc_nodes, NULL, S_IFREG | S_IRUGO, },
	{ "allocstat", NULL, ocfs2_proc_alloc_stat, NULL, S_IFREG | S_IRUGO, },
	{ "label", NULL, ocfs2_proc_label, NULL, S_IFREG | S_IRUGO, },
	{ NULL }
};

int ocfs2_proc_init(void)
{
	struct proc_dir_entry *parent = NULL;
	ocfs2_proc_list *p;
	struct proc_dir_entry* entry;

	mlog_entry_void();

	parent = proc_mkdir(OCFS2_PROC_BASENAME, NULL);
	if (parent) {
		ocfs2_proc_root_dir = parent;
		for (p = top_dir; p->name; p++) {
			entry = create_proc_read_entry(p->name, p->mode,
						       parent, p->read_proc,
						       p->data);
			if (!entry)
				return -EINVAL;
			if (p->write_proc)
				entry->write_proc = p->write_proc;

			entry->owner = THIS_MODULE;
		}
	}

	mlog_exit_void();
	return 0;
}

void ocfs2_proc_deinit(void)
{
	struct proc_dir_entry *parent = ocfs2_proc_root_dir;
	ocfs2_proc_list *p;

	mlog_entry_void();

	if (parent) {
		for (p = top_dir; p->name; p++)
			remove_proc_entry(p->name, parent);
		remove_proc_entry(OCFS2_PROC_BASENAME, NULL);
	}

	mlog_exit_void();
}

static int ocfs2_proc_calc_metrics(char *page, char **start, off_t off,
				   int count, int *eof, int len)
{
	mlog_entry_void();

	if (len <= off + count)
		*eof = 1;

	*start = page + off;

	len -= off;

	if (len > count)
		len = count;

	if (len < 0)
		len = 0;

	mlog_exit_void();
	return len;
}

static int ocfs2_proc_alloc_stat(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len, ret;
	char *la_state;
	ocfs2_super *osb = data;

	mlog_entry_void();

#define ALLOC_STATS_HDR "%-25s %10u\n"

	len = sprintf(page, "%s\n", "*** Disk Allocation Stats ***");

	if (osb->local_alloc_state == OCFS2_LA_ENABLED)
		la_state = "enabled";
	else if (osb->local_alloc_state == OCFS2_LA_DISABLED)
		la_state = "disabled";
	else
		la_state = "unused";

	len += sprintf(page + len, "%-25s %10s\n", "Local Alloc", la_state);
	len += sprintf(page + len, ALLOC_STATS_HDR, "Window Moves",
		       atomic_read(&osb->alloc_stats.moves));
	len += sprintf(page + len, ALLOC_STATS_HDR, "Local Allocs",
		       atomic_read(&osb->alloc_stats.local_data));
	len += sprintf(page + len, ALLOC_STATS_HDR, "Bitmap Allocs",
		       atomic_read(&osb->alloc_stats.bitmap_data));
	len += sprintf(page + len, ALLOC_STATS_HDR, "Block Group Allocs",
		       atomic_read(&osb->alloc_stats.bg_allocs));
	len += sprintf(page + len, ALLOC_STATS_HDR, "Block Group Adds",
		       atomic_read(&osb->alloc_stats.bg_extends));

	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);

	return ret;
}

static int ocfs2_proc_version(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len;
	int ret;

	mlog_entry_void();

        len = ocfs2_str_version(page);
	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_nodenum(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len;
	int ret;
	ocfs2_super *osb = data;

	mlog_entry_void();

	len = sprintf(page, "%d\n", osb->node_num);
	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_slotnum(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	int len;
	int ret;
	ocfs2_super *osb = data;

	mlog_entry_void();

	len = sprintf(page, "%d\n", osb->slot_num);
	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_nodename(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	int len;
	int ret;
	struct o2nm_node *node;

	mlog_entry_void();

	node = o2nm_get_node_by_num(o2nm_this_node());

	if (node) {
		len = sprintf(page, "%s\n", node->nd_name);
		o2nm_node_put(node);
	} else
		len = sprintf(page, "(unknown)\n");

	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

void ocfs2_proc_add_volume(ocfs2_super * osb)
{
	char newdir[20];
	struct proc_dir_entry *parent = NULL;
	struct proc_dir_entry* entry;
	ocfs2_proc_list *p;

	mlog_entry_void();

	snprintf(newdir, sizeof(newdir), "%u_%u",
		 MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));
	parent = proc_mkdir(newdir, ocfs2_proc_root_dir);
	osb->proc_sub_dir = parent;

	if (!parent) {
		mlog_exit_void();
		return;
	}

	for (p = sub_dir; p->name; p++) {
		/* XXX: What do we do if
		 * create_proc_read_entry fails?! */
		entry = create_proc_read_entry(p->name, p->mode,
					       parent, p->read_proc,
					       (char *)osb);
		if (entry) {
			if (p->write_proc)
				entry->write_proc = p->write_proc;

			entry->owner = THIS_MODULE;
		}
	}

	mlog_exit_void();
}

void ocfs2_proc_remove_volume(ocfs2_super * osb)
{
	ocfs2_proc_list *p;
	char dir[20];

	mlog_entry_void();

	if (osb->proc_sub_dir) {
		for (p = sub_dir; p->name; p++)
			remove_proc_entry(p->name, osb->proc_sub_dir);

		snprintf(dir, sizeof(dir), "%u_%u",
			 MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));
		remove_proc_entry(dir, ocfs2_proc_root_dir);
	}

	mlog_exit_void();
}

static int ocfs2_proc_uuid(char *page, char **start, off_t off,
			   int count, int *eof, void *data)
{
	int len, ret;
	ocfs2_super *osb = data;

	mlog_entry_void();

	len = sprintf(page, "%s\n", osb->uuid_str);
	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_statistics(char *page, char **start, off_t off,
				 int count, int *eof, void *data)
{
	int len;
	int ret = 0;
	ocfs2_super *osb = data;

	mlog_entry_void();

#define PROC_STATS				\
  "Number of nodes          : %u\n"		\
  "Cluster size             : %d\n"		\
  "Volume size              : %"MLFu64"\n"	\
  "Open Transactions:       : %u\n"

	len = sprintf(page, PROC_STATS, osb->num_nodes, osb->s_clustersize,
		      ocfs2_clusters_to_bytes(osb->sb, osb->num_clusters),
		      atomic_read(&osb->journal->j_num_trans));

	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_device(char *page, char **start, off_t off,
			     int count, int *eof, void *data)
{
	int len;
	int ret;
	ocfs2_super *osb = data;

	mlog_entry_void();

	len = snprintf(page, sizeof(osb->dev_str), "%s\n", osb->dev_str);
	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_nodes(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len = 0;
	int i;
	int ret;
	ocfs2_super *osb = data;
	char mount;

	mlog_entry_void();

	if (osb) {
		for (i = 0; i < OCFS2_NODE_MAP_MAX_NODES; i++) {
			mount = ocfs2_node_map_test_bit(osb, &osb->mounted_map, i) ? 'M' : ' ';
			len += sprintf(page + len, "%2d %c\n", i, mount);
		}
	}

	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

static int ocfs2_proc_label(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	int len;
	int ret;
	ocfs2_super *osb = data;

	mlog_entry_void();

	len = sprintf(page, "%s\n", osb->vol_label);
	ret = ocfs2_proc_calc_metrics(page, start, off, count, eof, len);

	mlog_exit(ret);
	return ret;
}

