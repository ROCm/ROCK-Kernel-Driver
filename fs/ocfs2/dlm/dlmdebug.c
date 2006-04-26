/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmdebug.c
 *
 * debug functionality for the dlm
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

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/utsname.h>
#include <linux/sysctl.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>

#include "cluster/heartbeat.h"
#include "cluster/nodemanager.h"
#include "cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"
#include "dlmdebug.h"

#include "dlmdomain.h"
#include "dlmdebug.h"

#define MLOG_MASK_PREFIX ML_DLM
#include "cluster/masklog.h"

static int dlm_dump_all_lock_resources(const char __user *data,
					unsigned int len);
static void dlm_dump_purge_list(struct dlm_ctxt *dlm);
static int dlm_dump_all_purge_lists(const char __user *data, unsigned int len);
static int dlm_trigger_migration(const char __user *data, unsigned int len);
static int dlm_dump_one_lock_resource(const char __user *data,
				       unsigned int len);

static int dlm_parse_domain_and_lockres(char *buf, unsigned int len,
					struct dlm_ctxt **dlm,
					struct dlm_lock_resource **res);

static int dlm_proc_stats(char *page, char **start, off_t off,
			  int count, int *eof, void *data);

typedef int (dlm_debug_func_t)(const char __user *data, unsigned int len);

struct dlm_debug_funcs
{
	char key;
	dlm_debug_func_t *func;
};

static struct dlm_debug_funcs dlm_debug_map[] = {
	{ 'r', dlm_dump_all_lock_resources },
	{ 'R', dlm_dump_one_lock_resource },
	{ 'm', dlm_dump_all_mles },
	{ 'p', dlm_dump_all_purge_lists  },
	{ 'M', dlm_trigger_migration },
};
static int dlm_debug_map_sz = (sizeof(dlm_debug_map) /
			       sizeof(struct dlm_debug_funcs));

static ssize_t write_dlm_debug(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	int i;
	char c;
	dlm_debug_func_t *fn;
	int ret;

	mlog(0, "(%p, %p, %u, %lld)\n",
		  file, buf, (unsigned int)count, (long long)*ppos);
	ret = 0;
	if (count<=0)
		goto done;

	ret = -EFAULT;
	if (get_user(c, buf))
		goto done;

	ret = count;
	for (i=0; i < dlm_debug_map_sz; i++) {
		struct dlm_debug_funcs *d = &dlm_debug_map[i];
		if (c == d->key) {
			fn = d->func;
			if (fn)
				ret = (fn)(buf, count);
			goto done;
		}
	}
done:
	return ret;
}

static struct file_operations dlm_debug_operations = {
	.write          = write_dlm_debug,
};

#define OCFS2_DLM_PROC_PATH "fs/ocfs2_dlm"
#define DLM_DEBUG_PROC_NAME "debug"
#define DLM_STAT_PROC_NAME  "stat"

static struct proc_dir_entry *ocfs2_dlm_proc;

void dlm_remove_proc(void)
{
	if (ocfs2_dlm_proc) {
		remove_proc_entry(DLM_DEBUG_PROC_NAME, ocfs2_dlm_proc);
		remove_proc_entry(OCFS2_DLM_PROC_PATH, NULL);
	}
}

void dlm_init_proc(void)
{
	struct proc_dir_entry *entry;

	ocfs2_dlm_proc = proc_mkdir(OCFS2_DLM_PROC_PATH, NULL);
	if (!ocfs2_dlm_proc) {
		mlog_errno(-ENOMEM);
		return;
	}

	entry = create_proc_entry(DLM_DEBUG_PROC_NAME, S_IWUSR,
				  ocfs2_dlm_proc);
	if (entry)
		entry->proc_fops = &dlm_debug_operations;
}

static int dlm_proc_stats(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	int len;
	struct dlm_ctxt *dlm = data;

	len = sprintf(page, "local=%d, remote=%d, unknown=%d\n",
		      atomic_read(&dlm->local_resources),
		      atomic_read(&dlm->remote_resources),
		      atomic_read(&dlm->unknown_resources));

	if (len <= off + count)
		*eof = 1;

	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;

	return len;
}

void dlm_proc_add_domain(struct dlm_ctxt *dlm)
{
	struct proc_dir_entry *entry;

	dlm->dlm_proc = proc_mkdir(dlm->name, ocfs2_dlm_proc);
	if (dlm->dlm_proc) {
		entry = create_proc_read_entry(DLM_STAT_PROC_NAME,
					       S_IFREG | S_IRUGO, dlm->dlm_proc,
					       dlm_proc_stats, (char *)dlm);
		if (entry)
			entry->owner = THIS_MODULE;
	}
}

void dlm_proc_del_domain(struct dlm_ctxt *dlm)
{
	if (dlm->dlm_proc) {
		remove_proc_entry(DLM_STAT_PROC_NAME, dlm->dlm_proc);
		remove_proc_entry(dlm->name, ocfs2_dlm_proc);
	}
}

/* lock resource printing is usually very important (printed
 * right before a BUG in some cases), but we'd like to be
 * able to shut it off if needed, hence the KERN_NOTICE level */
static int dlm_dump_all_lock_resources(const char __user *data,
				       unsigned int len)
{
	struct dlm_ctxt *dlm;
	struct list_head *iter;

	mlog(ML_NOTICE, "dumping ALL dlm state for node %s\n",
		  system_utsname.nodename);
	spin_lock(&dlm_domain_lock);
	list_for_each(iter, &dlm_domains) {
		dlm = list_entry (iter, struct dlm_ctxt, list);
		dlm_dump_lock_resources(dlm);
	}
	spin_unlock(&dlm_domain_lock);
	return len;
}

static int dlm_dump_one_lock_resource(const char __user *data,
				       unsigned int len)
{
	struct dlm_ctxt *dlm;
	struct dlm_lock_resource *res;
	char *buf = NULL;
	int ret = -EINVAL;
	int tmpret;

	if (len >= PAGE_SIZE-1) {
		mlog(ML_ERROR, "user passed too much data: %d bytes\n", len);
		goto leave;
	}
	if (len < 5) {
		mlog(ML_ERROR, "user passed too little data: %d bytes\n", len);
		goto leave;
	}
	buf = kmalloc(len+1, GFP_NOFS);
	if (!buf) {
		mlog(ML_ERROR, "could not alloc %d bytes\n", len+1);
		ret = -ENOMEM;
		goto leave;
	}
	if (strncpy_from_user(buf, data, len) < len) {
		mlog(ML_ERROR, "failed to get all user data.  done.\n");
		goto leave;
	}
	buf[len]='\0';
	mlog(0, "got this data from user: %s\n", buf);

	if (*buf != 'R') {
		mlog(0, "bad data\n");
		goto leave;
	}

	tmpret = dlm_parse_domain_and_lockres(buf, len, &dlm, &res);
	if (tmpret < 0) {
		mlog(0, "bad data\n");
		goto leave;
	}

	mlog(ML_NOTICE, "struct dlm_ctxt: %s, node=%u, key=%u\n",
		dlm->name, dlm->node_num, dlm->key);

	dlm_print_one_lock_resource(res);
	dlm_lockres_put(res);
	dlm_put(dlm);
	ret = len;

leave:
	if (buf)
		kfree(buf);
	return ret;
}


void dlm_print_one_lock_resource(struct dlm_lock_resource *res)
{
	mlog(ML_NOTICE, "lockres: %.*s, owner=%u, state=%u\n",
	       res->lockname.len, res->lockname.name,
	       res->owner, res->state);
	spin_lock(&res->spinlock);
	__dlm_print_one_lock_resource(res);
	spin_unlock(&res->spinlock);
}

void __dlm_print_one_lock_resource(struct dlm_lock_resource *res)
{
	struct list_head *iter2;
	struct dlm_lock *lock;

	assert_spin_locked(&res->spinlock);

	mlog(ML_NOTICE, "lockres: %.*s, owner=%u, state=%u\n",
	       res->lockname.len, res->lockname.name,
	       res->owner, res->state);
	mlog(ML_NOTICE, "  last used: %lu, on purge list: %s\n",
	     res->last_used, list_empty(&res->purge) ? "no" : "yes");
	mlog(ML_NOTICE, "  granted queue: \n");
	list_for_each(iter2, &res->granted) {
		lock = list_entry(iter2, struct dlm_lock, list);
		spin_lock(&lock->spinlock);
		mlog(ML_NOTICE, "    type=%d, conv=%d, node=%u, "
		       "cookie=%u:%llu, ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c)\n", 
		       lock->ml.type, lock->ml.convert_type, lock->ml.node, 
		       dlm_get_lock_cookie_node(lock->ml.cookie), 
		       dlm_get_lock_cookie_seq(lock->ml.cookie), 
		       list_empty(&lock->ast_list) ? 'y' : 'n',
		       lock->ast_pending ? 'y' : 'n',
		       list_empty(&lock->bast_list) ? 'y' : 'n',
		       lock->bast_pending ? 'y' : 'n');
		spin_unlock(&lock->spinlock);
	}
	mlog(ML_NOTICE, "  converting queue: \n");
	list_for_each(iter2, &res->converting) {
		lock = list_entry(iter2, struct dlm_lock, list);
		spin_lock(&lock->spinlock);
		mlog(ML_NOTICE, "    type=%d, conv=%d, node=%u, "
		       "cookie=%u:%llu, ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c)\n", 
		       lock->ml.type, lock->ml.convert_type, lock->ml.node, 
		       dlm_get_lock_cookie_node(lock->ml.cookie), 
		       dlm_get_lock_cookie_seq(lock->ml.cookie), 
		       list_empty(&lock->ast_list) ? 'y' : 'n',
		       lock->ast_pending ? 'y' : 'n',
		       list_empty(&lock->bast_list) ? 'y' : 'n',
		       lock->bast_pending ? 'y' : 'n');
		spin_unlock(&lock->spinlock);
	}
	mlog(ML_NOTICE, "  blocked queue: \n");
	list_for_each(iter2, &res->blocked) {
		lock = list_entry(iter2, struct dlm_lock, list);
		spin_lock(&lock->spinlock);
		mlog(ML_NOTICE, "    type=%d, conv=%d, node=%u, "
		       "cookie=%u:%llu, ast=(empty=%c,pend=%c), bast=(empty=%c,pend=%c)\n", 
		       lock->ml.type, lock->ml.convert_type, lock->ml.node, 
		       dlm_get_lock_cookie_node(lock->ml.cookie), 
		       dlm_get_lock_cookie_seq(lock->ml.cookie), 
		       list_empty(&lock->ast_list) ? 'y' : 'n',
		       lock->ast_pending ? 'y' : 'n',
		       list_empty(&lock->bast_list) ? 'y' : 'n',
		       lock->bast_pending ? 'y' : 'n');
		spin_unlock(&lock->spinlock);
	}
}


void dlm_print_one_lock(struct dlm_lock *lockid)
{
	dlm_print_one_lock_resource(lockid->lockres);
}
EXPORT_SYMBOL_GPL(dlm_print_one_lock);

void dlm_dump_lock_resources(struct dlm_ctxt *dlm)
{
	struct dlm_lock_resource *res;
	struct hlist_node *iter;
	struct hlist_head *bucket;
	int i;

	mlog(ML_NOTICE, "struct dlm_ctxt: %s, node=%u, key=%u\n",
		  dlm->name, dlm->node_num, dlm->key);
	if (!dlm || !dlm->name) {
		mlog(ML_ERROR, "dlm=%p\n", dlm);
		return;
	}

	spin_lock(&dlm->spinlock);
	for (i=0; i<DLM_HASH_BUCKETS; i++) {
		bucket = &(dlm->lockres_hash[i]);
		hlist_for_each_entry(res, iter, bucket, hash_node)
			dlm_print_one_lock_resource(res);
	}
	spin_unlock(&dlm->spinlock);
}

static void dlm_dump_purge_list(struct dlm_ctxt *dlm)
{
	struct list_head *iter;
	struct dlm_lock_resource *lockres;

	mlog(ML_NOTICE, "Purge list for DLM Domain \"%s\"\n", dlm->name);
	mlog(ML_NOTICE, "Last_used\tName\n");

	spin_lock(&dlm->spinlock);
	list_for_each(iter, &dlm->purge_list) {
		lockres = list_entry(iter, struct dlm_lock_resource, purge);

		spin_lock(&lockres->spinlock);
		mlog(ML_NOTICE, "%lu\t%.*s\n", lockres->last_used,
		       lockres->lockname.len, lockres->lockname.name);
		spin_unlock(&lockres->spinlock);
	}
	spin_unlock(&dlm->spinlock);
}

static int dlm_dump_all_purge_lists(const char __user *data, unsigned int len)
{
	struct dlm_ctxt *dlm;
	struct list_head *iter;

	spin_lock(&dlm_domain_lock);
	list_for_each(iter, &dlm_domains) {
		dlm = list_entry (iter, struct dlm_ctxt, list);
		dlm_dump_purge_list(dlm);
	}
	spin_unlock(&dlm_domain_lock);
	return len;
}

static int dlm_parse_domain_and_lockres(char *buf, unsigned int len,
					struct dlm_ctxt **dlm,
					struct dlm_lock_resource **res)
{
	char *resname;
	char *domainname;
	char *tmp;
	int ret = -EINVAL;

	*dlm = NULL;
	*res = NULL;

	tmp = buf;
	tmp++;
	if (*tmp != ' ') {
		mlog(0, "bad data\n");
		goto leave;
	}
	tmp++;
	domainname = tmp;

	while (*tmp) {
		if (*tmp == ' ')
			break;
		tmp++;
	}
	if (!*tmp || !*(tmp+1)) {
		mlog(0, "bad data\n");
		goto leave;
	}

	*tmp = '\0';  // null term the domainname
	tmp++;
	resname = tmp;
	while (*tmp) {
		if (*tmp == '\n' ||
		    *tmp == ' ' ||
		    *tmp == '\r') {
			*tmp = '\0';
			break;
		}
		tmp++;
	}

	mlog(0, "now looking up domain %s, lockres %s\n",
	       domainname, resname);
	spin_lock(&dlm_domain_lock);
	*dlm = __dlm_lookup_domain(domainname);
	spin_unlock(&dlm_domain_lock);

	if (!dlm_grab(*dlm)) {
		mlog(ML_ERROR, "bad dlm!\n");
		*dlm = NULL;
		goto leave;
	}

	*res = dlm_lookup_lockres(*dlm, resname, strlen(resname));
	if (!*res) {
		mlog(ML_ERROR, "bad lockres!\n");
		dlm_put(*dlm);
		*dlm = NULL;
		goto leave;
	}

	mlog(0, "found dlm=%p, lockres=%p\n", *dlm, *res);
	ret = 0;

leave:
	return ret;
}

static int dlm_trigger_migration(const char __user *data, unsigned int len)
{
	struct dlm_lock_resource *res;
	struct dlm_ctxt *dlm;
	char *buf = NULL;
	int ret = -EINVAL;
	int tmpret;

	if (len >= PAGE_SIZE-1) {
		mlog(ML_ERROR, "user passed too much data: %d bytes\n", len);
		goto leave;
	}
	if (len < 5) {
		mlog(ML_ERROR, "user passed too little data: %d bytes\n", len);
		goto leave;
	}
	buf = kmalloc(len+1, GFP_NOFS);
	if (!buf) {
		mlog(ML_ERROR, "could not alloc %d bytes\n", len+1);
		ret = -ENOMEM;
		goto leave;
	}
	if (strncpy_from_user(buf, data, len) < len) {
		mlog(ML_ERROR, "failed to get all user data.  done.\n");
		goto leave;
	}
	buf[len]='\0';
	mlog(0, "got this data from user: %s\n", buf);

	if (*buf != 'M') {
		mlog(0, "bad data\n");
		goto leave;
	}

	tmpret = dlm_parse_domain_and_lockres(buf, len, &dlm, &res);
	if (tmpret < 0) {
		mlog(0, "bad data\n");
		goto leave;
	}
	tmpret = dlm_migrate_lockres(dlm, res, O2NM_MAX_NODES);
	mlog(0, "dlm_migrate_lockres returned %d\n", tmpret);
	if (tmpret < 0)
		mlog(ML_ERROR, "failed to migrate %.*s: %d\n",
		     res->lockname.len, res->lockname.name, tmpret);
	dlm_lockres_put(res);
	dlm_put(dlm);
	ret = len;

leave:
	if (buf)
		kfree(buf);
	return ret;
}

static const char *dlm_errnames[] = {
	[DLM_NORMAL] =			"DLM_NORMAL",
	[DLM_GRANTED] =			"DLM_GRANTED",
	[DLM_DENIED] =			"DLM_DENIED",
	[DLM_DENIED_NOLOCKS] =		"DLM_DENIED_NOLOCKS",
	[DLM_WORKING] =			"DLM_WORKING",
	[DLM_BLOCKED] =			"DLM_BLOCKED",
	[DLM_BLOCKED_ORPHAN] =		"DLM_BLOCKED_ORPHAN",
	[DLM_DENIED_GRACE_PERIOD] =	"DLM_DENIED_GRACE_PERIOD",
	[DLM_SYSERR] =			"DLM_SYSERR",
	[DLM_NOSUPPORT] =		"DLM_NOSUPPORT",
	[DLM_CANCELGRANT] =		"DLM_CANCELGRANT",
	[DLM_IVLOCKID] =		"DLM_IVLOCKID",
	[DLM_SYNC] =			"DLM_SYNC",
	[DLM_BADTYPE] =			"DLM_BADTYPE",
	[DLM_BADRESOURCE] =		"DLM_BADRESOURCE",
	[DLM_MAXHANDLES] =		"DLM_MAXHANDLES",
	[DLM_NOCLINFO] =		"DLM_NOCLINFO",
	[DLM_NOLOCKMGR] =		"DLM_NOLOCKMGR",
	[DLM_NOPURGED] =		"DLM_NOPURGED",
	[DLM_BADARGS] =			"DLM_BADARGS",
	[DLM_VOID] =			"DLM_VOID",
	[DLM_NOTQUEUED] =		"DLM_NOTQUEUED",
	[DLM_IVBUFLEN] =		"DLM_IVBUFLEN",
	[DLM_CVTUNGRANT] =		"DLM_CVTUNGRANT",
	[DLM_BADPARAM] =		"DLM_BADPARAM",
	[DLM_VALNOTVALID] =		"DLM_VALNOTVALID",
	[DLM_REJECTED] =		"DLM_REJECTED",
	[DLM_ABORT] =			"DLM_ABORT",
	[DLM_CANCEL] =			"DLM_CANCEL",
	[DLM_IVRESHANDLE] =		"DLM_IVRESHANDLE",
	[DLM_DEADLOCK] =		"DLM_DEADLOCK",
	[DLM_DENIED_NOASTS] =		"DLM_DENIED_NOASTS",
	[DLM_FORWARD] =			"DLM_FORWARD",
	[DLM_TIMEOUT] =			"DLM_TIMEOUT",
	[DLM_IVGROUPID] =		"DLM_IVGROUPID",
	[DLM_VERS_CONFLICT] =		"DLM_VERS_CONFLICT",
	[DLM_BAD_DEVICE_PATH] =		"DLM_BAD_DEVICE_PATH",
	[DLM_NO_DEVICE_PERMISSION] =	"DLM_NO_DEVICE_PERMISSION",
	[DLM_NO_CONTROL_DEVICE ] =	"DLM_NO_CONTROL_DEVICE ",
	[DLM_RECOVERING] =		"DLM_RECOVERING",
	[DLM_MIGRATING] =		"DLM_MIGRATING",
	[DLM_MAXSTATS] =		"DLM_MAXSTATS",
};

static const char *dlm_errmsgs[] = {
	[DLM_NORMAL] = 			"request in progress",
	[DLM_GRANTED] = 		"request granted",
	[DLM_DENIED] = 			"request denied",
	[DLM_DENIED_NOLOCKS] = 		"request denied, out of system resources",
	[DLM_WORKING] = 		"async request in progress",
	[DLM_BLOCKED] = 		"lock request blocked",
	[DLM_BLOCKED_ORPHAN] = 		"lock request blocked by a orphan lock",
	[DLM_DENIED_GRACE_PERIOD] = 	"topological change in progress",
	[DLM_SYSERR] = 			"system error",
	[DLM_NOSUPPORT] = 		"unsupported",
	[DLM_CANCELGRANT] = 		"can't cancel convert: already granted",
	[DLM_IVLOCKID] = 		"bad lockid",
	[DLM_SYNC] = 			"synchronous request granted",
	[DLM_BADTYPE] = 		"bad resource type",
	[DLM_BADRESOURCE] = 		"bad resource handle",
	[DLM_MAXHANDLES] = 		"no more resource handles",
	[DLM_NOCLINFO] = 		"can't contact cluster manager",
	[DLM_NOLOCKMGR] = 		"can't contact lock manager",
	[DLM_NOPURGED] = 		"can't contact purge daemon",
	[DLM_BADARGS] = 		"bad api args",
	[DLM_VOID] = 			"no status",
	[DLM_NOTQUEUED] = 		"NOQUEUE was specified and request failed",
	[DLM_IVBUFLEN] = 		"invalid resource name length",
	[DLM_CVTUNGRANT] = 		"attempted to convert ungranted lock",
	[DLM_BADPARAM] = 		"invalid lock mode specified",
	[DLM_VALNOTVALID] = 		"value block has been invalidated",
	[DLM_REJECTED] = 		"request rejected, unrecognized client",
	[DLM_ABORT] = 			"blocked lock request cancelled",
	[DLM_CANCEL] = 			"conversion request cancelled",
	[DLM_IVRESHANDLE] = 		"invalid resource handle",
	[DLM_DEADLOCK] = 		"deadlock recovery refused this request",
	[DLM_DENIED_NOASTS] = 		"failed to allocate AST",
	[DLM_FORWARD] = 		"request must wait for primary's response",
	[DLM_TIMEOUT] = 		"timeout value for lock has expired",
	[DLM_IVGROUPID] = 		"invalid group specification",
	[DLM_VERS_CONFLICT] = 		"version conflicts prevent request handling",
	[DLM_BAD_DEVICE_PATH] = 	"Locks device does not exist or path wrong",
	[DLM_NO_DEVICE_PERMISSION] = 	"Client has insufficient perms for device",
	[DLM_NO_CONTROL_DEVICE] = 	"Cannot set options on opened device ",
	[DLM_RECOVERING] = 		"lock resource being recovered",
	[DLM_MIGRATING] = 		"lock resource being migrated",
	[DLM_MAXSTATS] = 		"invalid error number",
};


const char *dlm_errmsg(enum dlm_status err)
{
	if (err >= DLM_MAXSTATS || err < 0)
		return dlm_errmsgs[DLM_MAXSTATS];
	return dlm_errmsgs[err];
}
EXPORT_SYMBOL_GPL(dlm_errmsg);

const char *dlm_errname(enum dlm_status err)
{
	if (err >= DLM_MAXSTATS || err < 0)
		return dlm_errnames[DLM_MAXSTATS];
	return dlm_errnames[err];
}
EXPORT_SYMBOL_GPL(dlm_errname);

