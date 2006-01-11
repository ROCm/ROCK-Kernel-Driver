/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef _DMAPI_PRIVATE_H
#define _DMAPI_PRIVATE_H

#include <linux/slab.h>
#include "dmapi_port.h"
#include "sv.h"

#ifdef CONFIG_PROC_FS
#define DMAPI_PROCFS		"fs/dmapi_v2" /* DMAPI device in /proc. */
#define DMAPI_DBG_PROCFS	"fs/dmapi_d" /* DMAPI debugging dir */
#endif

extern struct kmem_cache	*dm_fsreg_cachep;
extern struct kmem_cache	*dm_tokdata_cachep;
extern struct kmem_cache	*dm_session_cachep;
extern struct kmem_cache	*dm_fsys_map_cachep;
extern struct kmem_cache	*dm_fsys_vptr_cachep;

typedef struct dm_tokdata {
	struct dm_tokdata *td_next;
	struct dm_tokevent *td_tevp;	/* pointer to owning tevp */
	int		td_app_ref;	/* # app threads currently active */
	dm_right_t	td_orig_right;	/* original right held when created */
	dm_right_t	td_right;	/* current right held for this handle */
	short		td_flags;
	short		td_type;	/* object type */
	int		td_vcount;	/* # of current application VN_HOLDs */
	struct inode	*td_ip;		/* inode pointer */
	dm_handle_t	td_handle;	/* handle for ip or sb */
} dm_tokdata_t;

/* values for td_type */

#define DM_TDT_NONE	0x00		/* td_handle is empty */
#define DM_TDT_VFS	0x01		/* td_handle points to a sb */
#define DM_TDT_REG	0x02		/* td_handle points to a file */
#define DM_TDT_DIR	0x04		/* td_handle points to a directory */
#define DM_TDT_LNK	0x08		/* td_handle points to a symlink */
#define DM_TDT_OTH	0x10		/* some other object eg. pipe, socket */

#define DM_TDT_VNO	(DM_TDT_REG|DM_TDT_DIR|DM_TDT_LNK|DM_TDT_OTH)
#define DM_TDT_ANY	(DM_TDT_VFS|DM_TDT_REG|DM_TDT_DIR|DM_TDT_LNK|DM_TDT_OTH)

/* values for td_flags */

#define DM_TDF_ORIG	0x0001		/* part of the original event */
#define DM_TDF_EVTREF	0x0002		/* event thread holds inode reference */
#define DM_TDF_STHREAD	0x0004		/* only one app can use this handle */
#define DM_TDF_RIGHT	0x0008		/* vcount bumped for dm_request_right */
#define DM_TDF_HOLD	0x0010		/* vcount bumped for dm_obj_ref_hold */


/* Because some events contain __u64 fields, we force te_msg and te_event
   to always be 8-byte aligned.	 In order to send more than one message in
   a single dm_get_events() call, we also ensure that each message is an
   8-byte multiple.
*/

typedef struct dm_tokevent {
	struct dm_tokevent  *te_next;
	struct dm_tokevent  *te_hashnext; /* hash chain */
	lock_t		te_lock;	/* lock for all fields but te_*next.
					 * te_next and te_hashnext are
					 * protected by the session lock.
					 */
	short		te_flags;
	short		te_allocsize;	/* alloc'ed size of this structure */
	sv_t		te_evt_queue;	/* queue waiting for dm_respond_event */
	sv_t		te_app_queue;	/* queue waiting for handle access */
	int		te_evt_ref;	/* number of event procs using token */
	int		te_app_ref;	/* number of app procs using token */
	int		te_app_slp;	/* number of app procs sleeping */
	int		te_reply;	/* return errno for sync messages */
	dm_tokdata_t	*te_tdp;	/* list of handle/right pairs */
	union {
		__u64		align;	/* force alignment of te_msg */
		dm_eventmsg_t	te_msg;		/* user visible part */
	} te_u;
	__u64		te_event;	/* start of dm_xxx_event_t message */
} dm_tokevent_t;

#define te_msg	te_u.te_msg

/* values for te_flags */

#define DM_TEF_LOCKED	0x0001		/* event "locked" by dm_get_events() */
#define DM_TEF_INTERMED 0x0002		/* a dm_pending reply was received */
#define DM_TEF_FINAL	0x0004		/* dm_respond_event has been received */
#define DM_TEF_HASHED	0x0010		/* event is on hash chain  */
#define DM_TEF_FLUSH	0x0020		/* flushing threads from queues */


#ifdef CONFIG_DMAPI_DEBUG
#define DM_SHASH_DEBUG
#endif

typedef struct dm_sesshash {
	dm_tokevent_t	*h_next;	/* ptr to chain of tokevents */
#ifdef DM_SHASH_DEBUG
	int		maxlength;
	int		curlength;
	int		num_adds;
	int		num_dels;
	int		dup_hits;
#endif
} dm_sesshash_t;


typedef struct dm_eventq {
	dm_tokevent_t	*eq_head;
	dm_tokevent_t	*eq_tail;
	int		eq_count;	/* size of queue */
} dm_eventq_t;


typedef struct dm_session {
	struct dm_session	*sn_next;	/* sessions linkage */
	dm_sessid_t	sn_sessid;	/* user-visible session number */
	u_int		sn_flags;
	lock_t		sn_qlock;	/* lock for newq/delq related fields */
	sv_t		sn_readerq;	/* waiting for message on sn_newq */
	sv_t		sn_writerq;	/* waiting for room on sn_newq */
	u_int		sn_readercnt;	/* count of waiting readers */
	u_int		sn_writercnt;	/* count of waiting readers */
	dm_eventq_t	sn_newq;	/* undelivered event queue */
	dm_eventq_t	sn_delq;	/* delivered event queue */
	dm_eventq_t	sn_evt_writerq; /* events of thrds in sn_writerq */
	dm_sesshash_t	*sn_sesshash;	/* buckets for tokevent hash chains */
#ifdef DM_SHASH_DEBUG
	int		sn_buckets_in_use;
	int		sn_max_buckets_in_use;
#endif
	char		sn_info[DM_SESSION_INFO_LEN];	/* user-supplied info */
} dm_session_t;

/* values for sn_flags */

#define DM_SN_WANTMOUNT 0x0001		/* session wants to get mount events */


typedef enum {
	DM_STATE_MOUNTING,
	DM_STATE_MOUNTED,
	DM_STATE_UNMOUNTING,
	DM_STATE_UNMOUNTED
} dm_fsstate_t;


typedef struct dm_fsreg {
	struct dm_fsreg *fr_next;
	struct super_block *fr_sb;	/* filesystem pointer */
	dm_tokevent_t	*fr_tevp;
	dm_fsid_t	fr_fsid;	/* filesystem ID */
	void		*fr_msg;	/* dm_mount_event_t for filesystem */
	int		fr_msgsize;	/* size of dm_mount_event_t */
	dm_fsstate_t	fr_state;
	sv_t		fr_dispq;
	int		fr_dispcnt;
	dm_eventq_t	fr_evt_dispq;	/* events of thrds in fr_dispq */
	sv_t		fr_queue;	/* queue for hdlcnt/sbcnt/unmount */
	lock_t		fr_lock;
	int		fr_hdlcnt;	/* threads blocked during unmount */
	int		fr_vfscnt;	/* threads in VFS_VGET or VFS_ROOT */
	int		fr_unmount;	/* if non-zero, umount is sleeping */
	dm_attrname_t	fr_rattr;	/* dm_set_return_on_destroy attribute */
	dm_session_t	*fr_sessp [DM_EVENT_MAX];
} dm_fsreg_t;




/* events valid in dm_set_disp() when called with a filesystem handle. */

#define DM_VALID_DISP_EVENTS		( \
	(1 << DM_EVENT_PREUNMOUNT)	| \
	(1 << DM_EVENT_UNMOUNT)		| \
	(1 << DM_EVENT_NOSPACE)		| \
	(1 << DM_EVENT_DEBUT)		| \
	(1 << DM_EVENT_CREATE)		| \
	(1 << DM_EVENT_POSTCREATE)	| \
	(1 << DM_EVENT_REMOVE)		| \
	(1 << DM_EVENT_POSTREMOVE)	| \
	(1 << DM_EVENT_RENAME)		| \
	(1 << DM_EVENT_POSTRENAME)	| \
	(1 << DM_EVENT_LINK)		| \
	(1 << DM_EVENT_POSTLINK)	| \
	(1 << DM_EVENT_SYMLINK)		| \
	(1 << DM_EVENT_POSTSYMLINK)	| \
	(1 << DM_EVENT_READ)		| \
	(1 << DM_EVENT_WRITE)		| \
	(1 << DM_EVENT_TRUNCATE)	| \
	(1 << DM_EVENT_ATTRIBUTE)	| \
	(1 << DM_EVENT_DESTROY)		)


/* isolate the read/write/trunc events of a dm_tokevent_t */

#define DM_EVENT_RDWRTRUNC(tevp)			(  \
	((tevp)->te_msg.ev_type == DM_EVENT_READ)	|| \
	((tevp)->te_msg.ev_type == DM_EVENT_WRITE)	|| \
	((tevp)->te_msg.ev_type == DM_EVENT_TRUNCATE)	)


/*
 *  Global handle hack isolation.
 */

#define DM_GLOBALHAN(hanp, hlen)	(((hanp) == DM_GLOBAL_HANP) && \
					 ((hlen) == DM_GLOBAL_HLEN))


#define DM_MAX_MSG_DATA		3960



/* Supported filesystem function vector functions. */


typedef struct {
	int				code_level;
	dm_fsys_clear_inherit_t		clear_inherit;
	dm_fsys_create_by_handle_t	create_by_handle;
	dm_fsys_downgrade_right_t	downgrade_right;
	dm_fsys_get_allocinfo_rvp_t	get_allocinfo_rvp;
	dm_fsys_get_bulkall_rvp_t	get_bulkall_rvp;
	dm_fsys_get_bulkattr_rvp_t	get_bulkattr_rvp;
	dm_fsys_get_config_t		get_config;
	dm_fsys_get_config_events_t	get_config_events;
	dm_fsys_get_destroy_dmattr_t	get_destroy_dmattr;
	dm_fsys_get_dioinfo_t		get_dioinfo;
	dm_fsys_get_dirattrs_rvp_t	get_dirattrs_rvp;
	dm_fsys_get_dmattr_t		get_dmattr;
	dm_fsys_get_eventlist_t		get_eventlist;
	dm_fsys_get_fileattr_t		get_fileattr;
	dm_fsys_get_region_t		get_region;
	dm_fsys_getall_dmattr_t		getall_dmattr;
	dm_fsys_getall_inherit_t	getall_inherit;
	dm_fsys_init_attrloc_t		init_attrloc;
	dm_fsys_mkdir_by_handle_t	mkdir_by_handle;
	dm_fsys_probe_hole_t		probe_hole;
	dm_fsys_punch_hole_t		punch_hole;
	dm_fsys_read_invis_rvp_t	read_invis_rvp;
	dm_fsys_release_right_t		release_right;
	dm_fsys_remove_dmattr_t		remove_dmattr;
	dm_fsys_request_right_t		request_right;
	dm_fsys_set_dmattr_t		set_dmattr;
	dm_fsys_set_eventlist_t		set_eventlist;
	dm_fsys_set_fileattr_t		set_fileattr;
	dm_fsys_set_inherit_t		set_inherit;
	dm_fsys_set_region_t		set_region;
	dm_fsys_symlink_by_handle_t	symlink_by_handle;
	dm_fsys_sync_by_handle_t	sync_by_handle;
	dm_fsys_upgrade_right_t		upgrade_right;
	dm_fsys_write_invis_rvp_t	write_invis_rvp;
	dm_fsys_obj_ref_hold_t		obj_ref_hold;
} dm_fsys_vector_t;


typedef struct	{
	struct list_head	ftype_list;	/* list of fstypes */
	struct list_head	sb_list;	/* list of sb's per fstype */
	struct file_system_type *f_type;
	struct filesystem_dmapi_operations *dmapiops;
	dm_fsys_vector_t	*vptr;
	struct super_block	*sb;
} dm_vector_map_t;


extern	dm_session_t	*dm_sessions;		/* head of session list */
extern	dm_fsreg_t	*dm_registers;
extern	lock_t		dm_reg_lock;		/* lock for registration list */

/*
 *  Kernel only prototypes.
 */

int		dm_find_session_and_lock(
			dm_sessid_t	sid,
			dm_session_t	**sessionpp,
			unsigned long	*lcp);

int		dm_find_msg_and_lock(
			dm_sessid_t	sid,
			dm_token_t	token,
			dm_tokevent_t	**tevpp,
			unsigned long	*lcp);

dm_tokevent_t * dm_evt_create_tevp(
			dm_eventtype_t	event,
			int		variable_size,
			void		**msgpp);

int		dm_app_get_tdp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			short		types,
			dm_right_t	right,
			dm_tokdata_t	**tdpp);

int		dm_get_config_tdp(
			void		__user *hanp,
			size_t		hlen,
			dm_tokdata_t	**tdpp);

void		dm_app_put_tdp(
			dm_tokdata_t	*tdp);

void		dm_put_tevp(
			dm_tokevent_t	*tevp,
			dm_tokdata_t	*tdp);

void		dm_evt_rele_tevp(
			dm_tokevent_t	*tevp,
			int		droprights);

int		dm_enqueue_normal_event(
			struct super_block *sbp,
			dm_tokevent_t	**tevpp,
			int		flags);

int		dm_enqueue_mount_event(
			struct super_block *sbp,
			dm_tokevent_t	*tevp);

int		dm_enqueue_sendmsg_event(
			dm_sessid_t	targetsid,
			dm_tokevent_t	*tevp,
			int		synch);

int		dm_enqueue_user_event(
			dm_sessid_t	sid,
			dm_tokevent_t	*tevp,
			dm_token_t	*tokenp);

int		dm_obj_ref_query_rvp(
			dm_sessid_t	sid,
			dm_token_t	token,
			void		__user *hanp,
			size_t		hlen,
			int		*rvp);

int		dm_read_invis_rvp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			dm_off_t	off,
			dm_size_t	len,
			void		__user *bufp,
			int		*rvp);

int		dm_write_invis_rvp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			int		flags,
			dm_off_t	off,
			dm_size_t	len,
			void		__user *bufp,
			int		*rvp);

int		dm_get_bulkattr_rvp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			u_int		mask,
			dm_attrloc_t	__user *locp,
			size_t		buflen,
			void		__user *bufp,
			size_t		__user *rlenp,
			int		*rvp);

int		dm_get_bulkall_rvp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			u_int		mask,
			dm_attrname_t	__user *attrnamep,
			dm_attrloc_t	__user *locp,
			size_t		buflen,
			void		__user *bufp,
			size_t		__user *rlenp,
			int		*rvp);

int		dm_get_dirattrs_rvp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			u_int		mask,
			dm_attrloc_t	__user *locp,
			size_t		buflen,
			void		__user *bufp,
			size_t		__user *rlenp,
			int		*rvp);

int		dm_get_allocinfo_rvp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			dm_off_t	__user	*offp,
			u_int		nelem,
			dm_extent_t	__user *extentp,
			u_int		__user *nelemp,
			int		*rvp);

int		dm_waitfor_destroy_attrname(
			struct super_block	*sb,
			dm_attrname_t	*attrnamep);

void		dm_clear_fsreg(
			dm_session_t	*s);

int		dm_add_fsys_entry(
			struct super_block	*sb,
			dm_tokevent_t	*tevp);

void		dm_change_fsys_entry(
			struct super_block	*sb,
			dm_fsstate_t	newstate);

void		dm_remove_fsys_entry(
			struct super_block	*sb);

dm_fsys_vector_t *dm_fsys_vector(
			struct inode	*ip);

struct filesystem_dmapi_operations *dm_fsys_ops(
			struct super_block	*sb);

void dm_fsys_ops_release(
			 struct super_block	*sb);

int		dm_waitfor_disp_session(
			struct super_block	*sb,
			dm_tokevent_t	*tevp,
			dm_session_t	**sessionpp,
			unsigned long	*lcp);

struct inode *	dm_handle_to_ip (
			dm_handle_t	*handlep,
			short		*typep);

int		dm_check_dmapi_ip(
			struct inode	*ip);

dm_tokevent_t * dm_find_mount_tevp_and_lock(
			dm_fsid_t	*fsidp,
			unsigned long	*lcp);

int		dm_path_to_hdl(
			char		__user *path,
			void		__user *hanp,
			size_t		__user *hlenp);

int		dm_path_to_fshdl(
			char		__user *path,
			void		__user *hanp,
			size_t		__user *hlenp);

int		dm_fd_to_hdl(
			int		fd,
			void		__user *hanp,
			size_t		__user *hlenp);

int		dm_upgrade_right(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token);

int		dm_downgrade_right(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token);

int		dm_request_right(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			u_int		flags,
			dm_right_t	right);

int		dm_release_right(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token);

int		dm_query_right(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			dm_right_t	__user *rightp);


int		dm_set_eventlist(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			dm_eventset_t	__user *eventsetp,
			u_int		maxevent);

int		dm_obj_ref_hold(
			dm_sessid_t	sid,
			dm_token_t	token,
			void		__user *hanp,
			size_t		hlen);

int		dm_obj_ref_rele(
			dm_sessid_t	sid,
			dm_token_t	token,
			void		__user *hanp,
			size_t		hlen);

int		dm_get_eventlist(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			u_int		nelem,
			dm_eventset_t	__user *eventsetp,
			u_int		__user *nelemp);


int		dm_set_disp(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			dm_eventset_t	__user *eventsetp,
			u_int		maxevent);


int		dm_set_return_on_destroy(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			dm_attrname_t	__user *attrnamep,
			dm_boolean_t	enable);


int		dm_get_mountinfo(
			dm_sessid_t	sid,
			void		__user *hanp,
			size_t		hlen,
			dm_token_t	token,
			size_t		buflen,
			void		__user *bufp,
			size_t		__user *rlenp);

void		dm_link_event(
			dm_tokevent_t	*tevp,
			dm_eventq_t	*queue);

void		dm_unlink_event(
			dm_tokevent_t	*tevp,
			dm_eventq_t	*queue);

int		dm_open_by_handle_rvp(
			unsigned int	fd,
			void		__user *hanp,
			size_t		hlen,
			int		mode,
			int		*rvp);

int		dm_copyin_handle(
			void		__user *hanp,
			size_t		hlen,
			dm_handle_t	*handlep);

int		dm_release_disp_threads(
			dm_fsid_t	*fsid,
			struct inode	*inode,
			int		errno);

#endif	/* _DMAPI_PRIVATE_H */
