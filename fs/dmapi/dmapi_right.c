/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
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
#include <asm/uaccess.h>
#include "dmapi.h"
#include "dmapi_kern.h"
#include "dmapi_private.h"


#define DM_FG_STHREAD		0x001	/* keep other threads from using tdp */
#define DM_FG_MUSTEXIST		0x002	/* handle must exist in the event */
#define DM_FG_DONTADD		0x004	/* don't add handle if not in event */

/* Get a handle of the form (void *, size_t) from user space and convert it to
   a handle_t.	Do as much validation of the result as possible; any error
   other than a bad address should return EBADF per the DMAPI spec.
*/

int
dm_copyin_handle(
	void		__user *hanp,	/* input,  handle data */
	size_t		hlen,		/* input,  size of handle data */
	dm_handle_t	*handlep)	/* output, copy of data */
{
	u_short		len;
	dm_fid_t	*fidp;

	fidp = (dm_fid_t*)&handlep->ha_fid;

	if (hlen < sizeof(handlep->ha_fsid) || hlen > sizeof(*handlep))
		return -EBADF;

	if (copy_from_user(handlep, hanp, hlen))
		return -EFAULT;

	if (hlen < sizeof(*handlep))
		memset((char *)handlep + hlen, 0, sizeof(*handlep) - hlen);

	if (hlen == sizeof(handlep->ha_fsid))
		return 0;	/* FS handle, nothing more to check */

	len = hlen - sizeof(handlep->ha_fsid) - sizeof(fidp->dm_fid_len);

	if ((fidp->dm_fid_len != len) || fidp->dm_fid_pad)
		return -EBADF;
	return 0;
}

/* Allocate and initialize a tevp structure.  Called from both application and
   event threads.
*/

static dm_tokevent_t *
dm_init_tevp(
	int		ev_size,	/* size of event structure */
	int		var_size)	/* size of variable-length data */
{
	dm_tokevent_t	*tevp;
	int		msgsize;

	/* Calculate the size of the event in bytes and allocate memory for it.
	   Zero all but the variable portion of the message, which will be
	   eventually overlaid by the caller with data.
	*/

	msgsize = offsetof(dm_tokevent_t, te_event) + ev_size + var_size;
	tevp = kmalloc(msgsize, GFP_KERNEL);
	if (tevp == NULL) {
		printk("%s/%d: kmalloc returned NULL\n", __FUNCTION__, __LINE__);
		return NULL;
	}
	memset(tevp, 0, msgsize - var_size);

	/* Now initialize all the non-zero fields. */

	spinlock_init(&tevp->te_lock, "te_lock");
	sv_init(&tevp->te_evt_queue, SV_DEFAULT, "te_evt_queue");
	sv_init(&tevp->te_app_queue, SV_DEFAULT, "te_app_queue");
	tevp->te_allocsize = msgsize;
	tevp->te_msg.ev_type = DM_EVENT_INVALID;
	tevp->te_flags = 0;

	return(tevp);
}


/* Given the event type and the number of bytes of variable length data that
   will follow the event, dm_evt_create_tevp() creates a dm_tokevent_t
   structure to hold the event and initializes all the common event fields.

   No locking is required for this routine because the caller is an event
   thread, and is therefore the only thread that can see the event.
*/

dm_tokevent_t *
dm_evt_create_tevp(
	dm_eventtype_t	event,
	int		variable_size,
	void		**msgpp)
{
	dm_tokevent_t	*tevp;
	int		evsize;

	switch (event) {
	case DM_EVENT_READ:
	case DM_EVENT_WRITE:
	case DM_EVENT_TRUNCATE:
		evsize = sizeof(dm_data_event_t);
		break;

	case DM_EVENT_DESTROY:
		evsize = sizeof(dm_destroy_event_t);
		break;

	case DM_EVENT_MOUNT:
		evsize = sizeof(dm_mount_event_t);
		break;

	case DM_EVENT_PREUNMOUNT:
	case DM_EVENT_UNMOUNT:
	case DM_EVENT_NOSPACE:
	case DM_EVENT_CREATE:
	case DM_EVENT_REMOVE:
	case DM_EVENT_RENAME:
	case DM_EVENT_SYMLINK:
	case DM_EVENT_LINK:
	case DM_EVENT_POSTCREATE:
	case DM_EVENT_POSTREMOVE:
	case DM_EVENT_POSTRENAME:
	case DM_EVENT_POSTSYMLINK:
	case DM_EVENT_POSTLINK:
	case DM_EVENT_ATTRIBUTE:
	case DM_EVENT_DEBUT:		/* currently not supported */
	case DM_EVENT_CLOSE:		/* currently not supported */
		evsize = sizeof(dm_namesp_event_t);
		break;

	case DM_EVENT_CANCEL:		/* currently not supported */
		evsize = sizeof(dm_cancel_event_t);
		break;

	case DM_EVENT_USER:
		evsize = 0;
		break;

	default:
		panic("dm_create_tevp: called with unknown event type %d\n",
			event);
	}

	/* Allocate and initialize an event structure of the correct size. */

	tevp = dm_init_tevp(evsize, variable_size);
	if (tevp == NULL)
		return NULL;
	tevp->te_evt_ref = 1;

	/* Fields ev_token, ev_sequence, and _link are all filled in when the
	   event is queued onto a session.  Initialize all other fields here.
	*/

	tevp->te_msg.ev_type = event;
	tevp->te_msg.ev_data.vd_offset = offsetof(dm_tokevent_t, te_event) -
		offsetof(dm_tokevent_t, te_msg);
	tevp->te_msg.ev_data.vd_length = evsize + variable_size;

	/* Give the caller a pointer to the event-specific structure. */

	*msgpp = ((char *)&tevp->te_msg + tevp->te_msg.ev_data.vd_offset);
	return(tevp);
}


/* Given a pointer to an event (tevp) and a pointer to a handle_t, look for a
   tdp structure within the event which contains the handle_t.	Either verify
   that the event contains the tdp, or optionally add the tdp to the
   event.  Called only from application threads.

   On entry, tevp->te_lock is held; it is dropped prior to return.
*/

static int
dm_app_lookup_tdp(
	dm_handle_t	*handlep,	/* the handle we are looking for */
	dm_tokevent_t	*tevp,		/* the event to search for the handle */
	unsigned long	*lcp,		/* address of active lock cookie */
	short		types,		/* acceptable object types */
	dm_right_t	right,		/* minimum right the object must have */
	u_int		flags,
	dm_tokdata_t	**tdpp)		/* if ! NULL, pointer to matching tdp */
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	struct inode	*ip;
	int		error;

	/* Bump the tevp application reference counter so that the event
	   can't disappear in case we have to drop the lock for a while.
	*/

	tevp->te_app_ref++;
	*tdpp = NULL;		/* assume failure */

	for (;;) {
		/* Look for a matching tdp in the tevp. */

		for (tdp = tevp->te_tdp; tdp; tdp = tdp->td_next) {
			if (DM_HANDLE_CMP(&tdp->td_handle, handlep) == 0)
				break;
		}

		/* If the tdp exists, but either we need single-thread access
		   to the handle and can't get it, or some other thread already
		   has single-thread access, then sleep until we can try again.
		*/

		if (tdp != NULL && tdp->td_app_ref &&
		    ((flags & DM_FG_STHREAD) ||
		     (tdp->td_flags & DM_TDF_STHREAD))) {
			tevp->te_app_slp++;
			sv_wait(&tevp->te_app_queue, 1,
				&tevp->te_lock, *lcp);
			*lcp = mutex_spinlock(&tevp->te_lock);
			tevp->te_app_slp--;
			continue;
		}

		if (tdp != NULL &&
		    (tdp->td_vcount > 0 || tdp->td_flags & DM_TDF_EVTREF)) {
			/* We have an existing tdp with a non-zero inode
			   reference count.  If it's the wrong type, return
			   an appropriate errno.
			*/

			if (!(tdp->td_type & types)) {
				mutex_spinunlock(&tevp->te_lock, *lcp);
				dm_put_tevp(tevp, NULL); /* no destroy events */
				return(-EOPNOTSUPP);
			}

			/* If the current access right isn't high enough,
			   complain.
			*/

			if (tdp->td_right < right) {
				mutex_spinunlock(&tevp->te_lock, *lcp);
				dm_put_tevp(tevp, NULL); /* no destroy events */
				return(-EACCES);
			}

			/* The handle is acceptable.  Increment the tdp
			   application and inode references and mark the tdp
			   as single-threaded if necessary.
			*/

			tdp->td_app_ref++;
			if (flags & DM_FG_STHREAD)
				tdp->td_flags |= DM_TDF_STHREAD;
			tdp->td_vcount++;

			fsys_vector = dm_fsys_vector(tdp->td_ip);
			(void)fsys_vector->obj_ref_hold(tdp->td_ip);

			mutex_spinunlock(&tevp->te_lock, *lcp);
			*tdpp = tdp;
			return(0);
		}

		/* If the tdp is not in the tevp or does not have an inode
		   reference, check to make sure it is okay to add/update it.
		*/

		if (flags & DM_FG_MUSTEXIST) {
			mutex_spinunlock(&tevp->te_lock, *lcp);
			dm_put_tevp(tevp, NULL);	/* no destroy events */
			return(-EACCES);		/* i.e. an insufficient right */
		}
		if (flags & DM_FG_DONTADD) {
			tevp->te_app_ref--;
			mutex_spinunlock(&tevp->te_lock, *lcp);
			return(0);
		}

		/* If a tdp structure doesn't yet exist, create one and link
		   it into the tevp.  Drop the lock while we are doing this as
		   zallocs can go to sleep.  Once we have the memory, make
		   sure that another thread didn't simultaneously add the same
		   handle to the same event.  If so, toss ours and start over.
		*/

		if (tdp == NULL) {
			dm_tokdata_t	*tmp;

			mutex_spinunlock(&tevp->te_lock, *lcp);

			tdp = kmem_cache_alloc(dm_tokdata_cachep, GFP_KERNEL);
			if (tdp == NULL){
				printk("%s/%d: kmem_cache_alloc(dm_tokdata_cachep) returned NULL\n", __FUNCTION__, __LINE__);
				return(-ENOMEM);
			}
			memset(tdp, 0, sizeof(*tdp));

			*lcp = mutex_spinlock(&tevp->te_lock);

			for (tmp = tevp->te_tdp; tmp; tmp = tmp->td_next) {
				if (DM_HANDLE_CMP(&tmp->td_handle, handlep) == 0)
					break;
			}
			if (tmp) {
				kmem_cache_free(dm_tokdata_cachep, tdp);
				continue;
			}

			tdp->td_next = tevp->te_tdp;
			tevp->te_tdp = tdp;
			tdp->td_tevp = tevp;
			tdp->td_handle = *handlep;
		}

		/* Temporarily single-thread access to the tdp so that other
		   threads don't touch it while we are filling the rest of the
		   fields in.
		*/

		tdp->td_app_ref = 1;
		tdp->td_flags |= DM_TDF_STHREAD;

		/* Drop the spinlock while we access, validate, and obtain the
		   proper rights to the object.	 This can take a very long time
		   if the inode is not in memory, if the filesystem is
		   unmounting,	or if the request_right() call should block
		   because some other tdp or kernel thread is holding a right.
		*/

		mutex_spinunlock(&tevp->te_lock, *lcp);

		if ((ip = dm_handle_to_ip(handlep, &tdp->td_type)) == NULL) {
			error = -EBADF;
		} else {
			tdp->td_vcount = 1;
			tdp->td_ip = ip;

			/* The handle is usable.  Check that the type of the
			   object matches one of the types that the caller
			   will accept.
			*/

			if (!(types & tdp->td_type)) {
				error = -EOPNOTSUPP;
			} else if (right > DM_RIGHT_NULL) {
				/* Attempt to get the rights required by the
				   caller.  If rights can't be obtained, return
				   an error.
				*/

				fsys_vector = dm_fsys_vector(tdp->td_ip);
				error = fsys_vector->request_right(tdp->td_ip,
					DM_RIGHT_NULL,
					(tdp->td_type == DM_TDT_VFS ?
					DM_FSYS_OBJ : 0),
					DM_RR_WAIT, right);
				if (!error) {
					tdp->td_right = right;
				}
			} else {
				error = 0;
			}
		}
		if (error != 0) {
			dm_put_tevp(tevp, tdp); /* destroy event risk, although tiny */
			return(error);
		}

		*lcp = mutex_spinlock(&tevp->te_lock);

		/* Wake up any threads which may have seen our tdp while we
		   were filling it in.
		*/

		if (!(flags & DM_FG_STHREAD)) {
			tdp->td_flags &= ~DM_TDF_STHREAD;
			if (tevp->te_app_slp)
				sv_broadcast(&tevp->te_app_queue);
		}

		mutex_spinunlock(&tevp->te_lock, *lcp);
		*tdpp = tdp;
		return(0);
	}
}


/* dm_app_get_tdp_by_token() is called whenever the application request
   contains a session ID and contains a token other than DM_NO_TOKEN.
   Most of the callers provide a right that is either DM_RIGHT_SHARED or
   DM_RIGHT_EXCL, but a few of the callers such as dm_obj_ref_hold() may
   specify a right of DM_RIGHT_NULL.
*/

static int
dm_app_get_tdp_by_token(
	dm_sessid_t	sid,		/* an existing session ID */
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,		/* an existing token */
	short		types,		/* acceptable object types */
	dm_right_t	right,		/* minimum right the object must have */
	u_int		flags,
	dm_tokdata_t	**tdpp)
{
	dm_tokevent_t	*tevp;
	dm_handle_t	handle;
	int		error;
	unsigned long	lc;		/* lock cookie */

	if (right < DM_RIGHT_NULL || right > DM_RIGHT_EXCL)
		return(-EINVAL);

	if ((error = dm_copyin_handle(hanp, hlen, &handle)) != 0)
		return(error);

	/* Find and lock the event which corresponds to the specified
	   session/token pair.
	*/

	if ((error = dm_find_msg_and_lock(sid, token, &tevp, &lc)) != 0)
		return(error);

	return(dm_app_lookup_tdp(&handle, tevp, &lc, types,
		right, flags, tdpp));
}


/* Function dm_app_get_tdp() must ONLY be called from routines associated with
   application calls, e.g. dm_read_invis, dm_set_disp, etc.  It must not be
   called by a thread responsible for generating an event such as
   dm_send_data_event()!

   dm_app_get_tdp() is the interface used by all application calls other than
   dm_get_events, dm_respond_event, dm_get_config, dm_get_config_events, and by
   the dm_obj_ref_* and dm_*_right families of requests.

   dm_app_get_tdp() converts a sid/hanp/hlen/token quad into a tdp pointer,
   increments the number of active application threads in the event, and
   increments the number of active application threads using the tdp.  The
   'right' parameter must be either DM_RIGHT_SHARED or DM_RIGHT_EXCL.  The
   token may either be DM_NO_TOKEN, or can be a token received in a synchronous
   event.
*/

int
dm_app_get_tdp(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,
	short		types,
	dm_right_t	right,		/* minimum right */
	dm_tokdata_t	**tdpp)
{
	dm_session_t	*s;
	dm_handle_t	handle;
	dm_tokevent_t	*tevp;
	int		error;
	unsigned long	lc;		/* lock cookie */

	ASSERT(right >= DM_RIGHT_SHARED);

	/* If a token other than DM_NO_TOKEN is specified, find the event on
	   this session which owns the token and increment its reference count.
	*/

	if (token != DM_NO_TOKEN) {	/* look up existing tokevent struct */
		return(dm_app_get_tdp_by_token(sid, hanp, hlen, token, types,
			right, DM_FG_MUSTEXIST, tdpp));
	}

	/* The token is DM_NO_TOKEN.  In this case we only want to verify that
	   the session ID is valid, and do not need to continue holding the
	   session lock after we know that to be true.
	*/

	if ((error = dm_copyin_handle(hanp, hlen, &handle)) != 0)
		return(error);

	if ((error = dm_find_session_and_lock(sid, &s, &lc)) != 0)
		return(error);
	mutex_spinunlock(&s->sn_qlock, lc);

	/* When DM_NO_TOKEN is used, we simply block until we can obtain the
	   right that we want (since the tevp contains no tdp structures).
	   The blocking when we eventually support it will occur within
	   fsys_vector->request_right().
	*/

	tevp = dm_init_tevp(0, 0);
	lc = mutex_spinlock(&tevp->te_lock);

	return(dm_app_lookup_tdp(&handle, tevp, &lc, types, right, 0, tdpp));
}


/* dm_get_config_tdp() is only called by dm_get_config() and
   dm_get_config_events(), which neither have a session ID nor a token.
   Both of these calls are supposed to work even if the filesystem is in the
   process of being mounted, as long as the caller only uses handles within
   the mount event.
*/

int
dm_get_config_tdp(
	void		__user *hanp,
	size_t		hlen,
	dm_tokdata_t	**tdpp)
{
	dm_handle_t	handle;
	dm_tokevent_t	*tevp;
	int		error;
	unsigned long	lc;		/* lock cookie */

	if ((error = dm_copyin_handle(hanp, hlen, &handle)) != 0)
		return(error);

	tevp = dm_init_tevp(0, 0);
	lc = mutex_spinlock(&tevp->te_lock);

	/* Try to use the handle provided by the caller and assume DM_NO_TOKEN.
	   This will fail if the filesystem is in the process of being mounted.
	*/

	error = dm_app_lookup_tdp(&handle, tevp, &lc, DM_TDT_ANY,
		DM_RIGHT_NULL, 0, tdpp);

	if (!error) {
		return(0);
	}

	/* Perhaps the filesystem is still mounting, in which case we need to
	   see if this is one of the handles in the DM_EVENT_MOUNT tevp.
	*/

	if ((tevp = dm_find_mount_tevp_and_lock(&handle.ha_fsid, &lc)) == NULL)
		return(-EBADF);

	return(dm_app_lookup_tdp(&handle, tevp, &lc, DM_TDT_ANY,
		DM_RIGHT_NULL, DM_FG_MUSTEXIST, tdpp));
}


/* dm_put_tdp() is called to release any right held on the inode, and to
   VN_RELE() all references held on the inode.	It is the caller's
   responsibility to ensure that no other application threads are using the
   tdp, and if necessary to unlink the tdp from the tevp before calling
   this routine and to free the tdp afterwards.
*/

static void
dm_put_tdp(
	dm_tokdata_t	*tdp)
{
	ASSERT(tdp->td_app_ref <= 1);

	/* If the application thread is holding a right, or if the event
	   thread had a right but it has disappeared because of a dm_pending
	   or Cntl-C, then we need to release it here.
	*/

	if (tdp->td_right != DM_RIGHT_NULL) {
		dm_fsys_vector_t *fsys_vector;

		fsys_vector = dm_fsys_vector(tdp->td_ip);
		(void)fsys_vector->release_right(tdp->td_ip, tdp->td_right,
			(tdp->td_type == DM_TDT_VFS ? DM_FSYS_OBJ : 0));
		tdp->td_right = DM_RIGHT_NULL;
	}

	/* Given that we wouldn't be here if there was still an event thread,
	   this VN_RELE loop has the potential of generating a DM_EVENT_DESTROY
	   event if some other thread has unlinked the file.
	*/

	while (tdp->td_vcount > 0) {
		iput(tdp->td_ip);
		tdp->td_vcount--;
	}

	tdp->td_flags &= ~(DM_TDF_HOLD|DM_TDF_RIGHT);
	tdp->td_ip = NULL;
}


/* Function dm_put_tevp() must ONLY be called from routines associated with
   application threads, e.g. dm_read_invis, dm_get_events, etc.	 It must not be
   called by a thread responsible for generating an event, such as
   dm_send_data_event.

   PLEASE NOTE: It is possible for this routine to generate DM_EVENT_DESTROY
   events, because its calls to dm_put_tdp drop inode references, and another
   thread may have already unlinked a file whose inode we are de-referencing.
   This sets the stage for various types of deadlock if the thread calling
   dm_put_tevp is the same thread that calls dm_respond_event!	In particular,
   the dm_sent_destroy_event routine needs to obtain the dm_reg_lock,
   dm_session_lock, and sn_qlock in order to queue the destroy event.  No
   caller of dm_put_tevp can hold any of these locks!

   Other possible deadlocks are that dm_send_destroy_event could block waiting
   for a thread to register for the event using	 dm_set_disp() and/or
   dm_set_return_on_destroy, or it could block because the session's sn_newq
   is at the dm_max_queued_msgs event limit.  The only safe solution
   (unimplemented) is to have a separate kernel thread for each filesystem
   whose only job is to do the inode-dereferencing.  That way dm_respond_event
   will not block, so the application can keep calling dm_get_events to read
   events even if the filesystem thread should block.  (If the filesystem
   thread blocks, so will all subsequent destroy events for the same
   filesystem.)
*/

void
dm_put_tevp(
	dm_tokevent_t	*tevp,
	dm_tokdata_t	*tdp)
{
	int		free_tdp = 0;
	unsigned long	lc;		/* lock cookie */

	lc = mutex_spinlock(&tevp->te_lock);

	if (tdp != NULL) {
		if (tdp->td_vcount > 1 || (tdp->td_flags & DM_TDF_EVTREF)) {
			ASSERT(tdp->td_app_ref > 0);

			iput(tdp->td_ip);
			tdp->td_vcount--;
		} else {
			ASSERT(tdp->td_app_ref == 1);

			/* The inode reference count is either already at
			   zero (e.g. a failed dm_handle_to_ip() call in
			   dm_app_lookup_tdp()) or is going to zero.  We can't
			   hold the lock while we decrement the count because
			   we could potentially end up being busy for a long
			   time in VOP_INACTIVATE.  Use single-threading to
			   lock others out while we clean house.
			*/

			tdp->td_flags |= DM_TDF_STHREAD;

			/* WARNING - A destroy event is possible here if we are
			   giving up the last reference on an inode which has
			   been previously unlinked by some other thread!
			*/

			mutex_spinunlock(&tevp->te_lock, lc);
			dm_put_tdp(tdp);
			lc = mutex_spinlock(&tevp->te_lock);

			/* If this tdp is not one of the original tdps in the
			   event, then remove it from the tevp.
			*/

			if (!(tdp->td_flags & DM_TDF_ORIG)) {
				dm_tokdata_t	**tdpp = &tevp->te_tdp;

				while (*tdpp && *tdpp != tdp) {
					tdpp = &(*tdpp)->td_next;
				}
				if (*tdpp == NULL) {
					panic("dm_remove_tdp_from_tevp: tdp "
						"%p not in tevp %p\n", tdp,
						tevp);
				}
				*tdpp = tdp->td_next;
				free_tdp++;
			}
		}

		/* If this is the last app thread actively using the tdp, clear
		   any single-threading and wake up any other app threads who
		   might be waiting to use this tdp, single-threaded or
		   otherwise.
		*/

		if (--tdp->td_app_ref == 0) {
			if (tdp->td_flags & DM_TDF_STHREAD) {
				tdp->td_flags &= ~DM_TDF_STHREAD;
				if (tevp->te_app_slp)
					sv_broadcast(&tevp->te_app_queue);
			}
		}

		if (free_tdp) {
			kmem_cache_free(dm_tokdata_cachep, tdp);
		}
	}

	/* If other application threads are using this token/event, they will
	   do the cleanup.
	*/

	if (--tevp->te_app_ref > 0) {
		mutex_spinunlock(&tevp->te_lock, lc);
		return;
	}

	/* If event generation threads are waiting for this thread to go away,
	   wake them up and let them do the cleanup.
	*/

	if (tevp->te_evt_ref > 0) {
		sv_broadcast(&tevp->te_evt_queue);
		mutex_spinunlock(&tevp->te_lock, lc);
		return;
	}

	/* This thread is the last active thread using the token/event.	 No
	   lock can be held while we disassemble the tevp because we could
	   potentially end up being busy for a long time in VOP_INACTIVATE.
	*/

	mutex_spinunlock(&tevp->te_lock, lc);

	/* WARNING - One or more destroy events are possible here if we are
	   giving up references on inodes which have been previously unlinked
	   by other kernel threads!
	*/

	while ((tdp = tevp->te_tdp) != NULL) {
		tevp->te_tdp = tdp->td_next;
		dm_put_tdp(tdp);
		kmem_cache_free(dm_tokdata_cachep, tdp);
	}
	spinlock_destroy(&tevp->te_lock);
	sv_destroy(&tevp->te_evt_queue);
	sv_destroy(&tevp->te_app_queue);
	kfree(tevp);
}


/* No caller of dm_app_put_tevp can hold either of the locks dm_reg_lock,
   dm_session_lock, or any sn_qlock!  (See dm_put_tevp for details.)
*/

void
dm_app_put_tdp(
	dm_tokdata_t	*tdp)
{
	dm_put_tevp(tdp->td_tevp, tdp);
}


/* dm_change_right is only called if the event thread is the one doing the
   cleanup on a completed event.  It looks at the current rights of a tdp
   and compares that with the rights it had on the tdp when the event was
   created.  If different, it reaquires the original rights, then transfers
   the rights back to being thread-based.
*/

static void
dm_change_right(
	dm_tokdata_t	*tdp)
{
#ifdef HAVE_DMAPI_RIGHTS
	dm_fsys_vector_t *fsys_vector;
	int		error;
	u_int		type;
#endif

	/* If the event doesn't have an inode reference, if the original right
	   was DM_RIGHT_NULL, or if the rights were never switched from being
	   thread-based to tdp-based, then there is nothing to do.
	*/

	if (!(tdp->td_flags & DM_TDF_EVTREF))
		return;

	if (tdp->td_orig_right == DM_RIGHT_NULL)
		return;

	/* DEBUG - Need a check here for event-based rights. */

#ifdef HAVE_DMAPI_RIGHTS
	/* The "rights" vectors are stubs now anyway.  When they are
	 * implemented then bhv locking will have to be sorted out.
	 */

	/* If the current right is not the same as it was when the event was
	   created, first get back the original right.
	*/

	if (tdp->td_right != tdp->td_orig_right) {
		fsys_vector = dm_fsys_vector(tdp->td_ip);
		type = (tdp->td_type == DM_TDT_VFS ? DM_FSYS_OBJ : 0);

		switch (tdp->td_orig_right) {
		case DM_RIGHT_SHARED:
			if (tdp->td_right == DM_RIGHT_EXCL) {
				error = fsys_vector->downgrade_right(
					tdp->td_ip, tdp->td_right, type);
				if (!error)
					break;
				(void)fsys_vector->release_right(tdp->td_ip,
					tdp->td_right, type);
			}
			(void)fsys_vector->request_right(tdp->td_ip,
				tdp->td_right, type, DM_RR_WAIT,
				tdp->td_orig_right);
			break;

		case DM_RIGHT_EXCL:
			if (tdp->td_right == DM_RIGHT_SHARED) {
				error = fsys_vector->upgrade_right(tdp->td_ip,
					tdp->td_right, type);
				if (!error)
					break;
				(void)fsys_vector->release_right(tdp->td_ip,
					tdp->td_right, type);
			}
			(void)fsys_vector->request_right(tdp->td_ip,
				tdp->td_right, type, DM_RR_WAIT,
				tdp->td_orig_right);
			break;
		case DM_RIGHT_NULL:
			break;
		}
	}
#endif

	/* We now have back the same level of rights as we had when the event
	   was generated.  Now transfer the rights from being tdp-based back
	   to thread-based.
	*/

	/* DEBUG - Add a call here to transfer rights back to thread-based. */

	/* Finally, update the tdp so that we don't mess with the rights when
	   we eventually call dm_put_tdp.
	*/

	tdp->td_right = DM_RIGHT_NULL;
}


/* This routine is only called by event threads.  The calls to dm_put_tdp
   are not a deadlock risk here because this is an event thread, and it is
   okay for such a thread to block on an induced destroy event.	 Okay, maybe
   there is a slight risk; say that the event contains three inodes all of
   which have DM_RIGHT_EXCL, and say that we are at the dm_max_queued_msgs
   limit, and that the first inode is already unlinked.	 In that case the
   destroy event will block waiting to be queued, and the application thread
   could happen to reference one of the other locked inodes.  Deadlock.
*/

void
dm_evt_rele_tevp(
	dm_tokevent_t	*tevp,
	int		droprights)	/* non-zero, evt thread loses rights */
{
	dm_tokdata_t	*tdp;
	unsigned long	lc;		/* lock cookie */

	lc = mutex_spinlock(&tevp->te_lock);

	/* If we are here without DM_TEF_FINAL set and with at least one
	   application reference still remaining, then one of several
	   possibilities is true:
	   1. This is an asynchronous event which has been queued but has not
	      yet been delivered, or which is in the process of being delivered.
	   2. This is an unmount event (pseudo-asynchronous) yet to be
	      delivered or in the process of being delivered.
	   3. This event had DM_FLAGS_NDELAY specified, and the application
	      has sent a dm_pending() reply for the event.
	   4. This is a DM_EVENT_READ, DM_EVENT_WRITE, or DM_EVENT_TRUNCATE
	      event and the user typed a Cntl-C.
	   In all of these cases, the correct behavior is to leave the
	   responsibility of releasing any rights to the application threads
	   when they are done.
	*/

	if (tevp->te_app_ref > 0 && !(tevp->te_flags & DM_TEF_FINAL)) {
		tevp->te_evt_ref--;
		for (tdp = tevp->te_tdp; tdp; tdp = tdp->td_next) {
			if (tdp->td_flags & DM_TDF_EVTREF) {
				tdp->td_flags &= ~DM_TDF_EVTREF;
				if (tdp->td_vcount == 0) {
					tdp->td_ip = NULL;
				}
			}
		}
		mutex_spinunlock(&tevp->te_lock, lc);
		return;		/* not the last thread */
	}

	/* If the application reference count is non-zero here, that can only
	   mean that dm_respond_event() has been called, but the application
	   still has one or more threads in the kernel that haven't let go of
	   the tevp.  In these cases, the event thread must wait until all
	   application threads have given up their references, and their
	   rights to handles within the event.
	*/

	while (tevp->te_app_ref) {
		sv_wait(&tevp->te_evt_queue, 1, &tevp->te_lock, lc);
		lc = mutex_spinlock(&tevp->te_lock);
	}

	/* This thread is the last active thread using the token/event.	 Reset
	   the rights of any inode that was part of the original event back
	   to their initial values before returning to the filesystem.	The
	   exception is if the event failed (droprights is non-zero), in which
	   case we chose to return to the filesystem with all rights released.
	   Release the rights on any inode that was not part of the original
	   event.  Give up all remaining application inode references
	   regardless of whether or not the inode was part of the original
	   event.
	*/

	mutex_spinunlock(&tevp->te_lock, lc);

	while ((tdp = tevp->te_tdp) != NULL) {
		tevp->te_tdp = tdp->td_next;
		if ((tdp->td_flags & DM_TDF_ORIG) &&
		    (tdp->td_flags & DM_TDF_EVTREF) &&
		    (!droprights)) {
			dm_change_right(tdp);
		}
		dm_put_tdp(tdp);
		kmem_cache_free(dm_tokdata_cachep, tdp);
	}
	spinlock_destroy(&tevp->te_lock);
	sv_destroy(&tevp->te_evt_queue);
	sv_destroy(&tevp->te_app_queue);
	kfree(tevp);
}


/* dm_obj_ref_hold() is just a fancy way to get an inode reference on an object
   to hold it in kernel memory.
*/

int
dm_obj_ref_hold(
	dm_sessid_t	sid,
	dm_token_t	token,
	void		__user *hanp,
	size_t		hlen)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_VNO,
		DM_RIGHT_NULL, DM_FG_STHREAD, &tdp);

	/* The tdp is single-threaded, so no mutex lock needed for update. */

	if (error == 0) {
		if (tdp->td_flags & DM_TDF_HOLD) {	/* if already held */
			error = -EBUSY;
		} else {
			tdp->td_flags |= DM_TDF_HOLD;
			tdp->td_vcount++;

			fsys_vector = dm_fsys_vector(tdp->td_ip);
			(void)fsys_vector->obj_ref_hold(tdp->td_ip);
		}
		dm_app_put_tdp(tdp);
	}
	return(error);
}


int
dm_obj_ref_rele(
	dm_sessid_t	sid,
	dm_token_t	token,
	void		__user *hanp,
	size_t		hlen)
{
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_VNO,
		DM_RIGHT_NULL, DM_FG_MUSTEXIST|DM_FG_STHREAD, &tdp);

	/* The tdp is single-threaded, so no mutex lock needed for update. */

	if (error == 0) {
		if (!(tdp->td_flags & DM_TDF_HOLD)) {	/* if not held */
			error = -EACCES; /* use the DM_FG_MUSTEXIST errno */
		} else {
			tdp->td_flags &= ~DM_TDF_HOLD;
			iput(tdp->td_ip);
			tdp->td_vcount--;
		}
		dm_app_put_tdp(tdp);
	}
	return(error);
}


int
dm_obj_ref_query_rvp(
	dm_sessid_t	sid,
	dm_token_t	token,
	void		__user *hanp,
	size_t		hlen,
	int		*rvp)
{
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_VNO,
		DM_RIGHT_NULL, DM_FG_DONTADD|DM_FG_STHREAD, &tdp);
	if (error != 0)
		return(error);

	/* If the request is valid but the handle just isn't present in the
	   event or the hold flag isn't set, return zero, else return one.
	*/

	if (tdp) {
		if (tdp->td_flags & DM_TDF_HOLD) {	/* if held */
			*rvp = 1;
		} else {
			*rvp = 0;
		}
		dm_app_put_tdp(tdp);
	} else {
		*rvp = 0;
	}
	return(0);
}


int
dm_downgrade_right(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_ANY,
		DM_RIGHT_EXCL, DM_FG_MUSTEXIST|DM_FG_STHREAD, &tdp);
	if (error != 0)
		return(error);

	/* Attempt the downgrade.  Filesystems which support rights but not
	   the downgrading of rights will return ENOSYS.
	*/

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->downgrade_right(tdp->td_ip, tdp->td_right,
		(tdp->td_type == DM_TDT_VFS ? DM_FSYS_OBJ : 0));

	/* The tdp is single-threaded, so no mutex lock needed for update. */

	if (error == 0)
		tdp->td_right = DM_RIGHT_SHARED;

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_query_right(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,
	dm_right_t	__user *rightp)
{
	dm_tokdata_t	*tdp;
	dm_right_t	right;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_ANY,
		DM_RIGHT_NULL, DM_FG_DONTADD|DM_FG_STHREAD, &tdp);
	if (error != 0)
		return(error);

	/* Get the current right and copy it to the caller.  The tdp is
	   single-threaded, so no mutex lock is needed.	 If the tdp is not in
	   the event we are supposed to return DM_RIGHT_NULL in order to be
	   compatible with Veritas.
	*/

	if (tdp) {
		right = tdp->td_right;
		dm_app_put_tdp(tdp);
	} else {
		right = DM_RIGHT_NULL;
	}
	if (copy_to_user(rightp, &right, sizeof(right)))
		return(-EFAULT);
	return(0);
}


int
dm_release_right(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_ANY,
		DM_RIGHT_SHARED, DM_FG_MUSTEXIST|DM_FG_STHREAD, &tdp);
	if (error != 0)
		return(error);

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->release_right(tdp->td_ip, tdp->td_right,
		(tdp->td_type == DM_TDT_VFS ? DM_FSYS_OBJ : 0));

	/* The tdp is single-threaded, so no mutex lock needed for update. */

	if (error == 0) {
		tdp->td_right = DM_RIGHT_NULL;
		if (tdp->td_flags & DM_TDF_RIGHT) {
			tdp->td_flags &= ~DM_TDF_RIGHT;
			iput(tdp->td_ip);
			tdp->td_vcount--;
		}
	}

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_request_right(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token,
	u_int		flags,
	dm_right_t	right)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_ANY,
		DM_RIGHT_NULL, DM_FG_STHREAD, &tdp);
	if (error != 0)
		return(error);

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->request_right(tdp->td_ip, tdp->td_right,
		(tdp->td_type == DM_TDT_VFS ? DM_FSYS_OBJ : 0), flags, right);

	/* The tdp is single-threaded, so no mutex lock is needed for update.

	   If this is the first dm_request_right call for this inode, then we
	   need to bump the inode reference count for two reasons.  First of
	   all, it is supposed to be impossible for the file to disappear or
	   for the filesystem to be unmounted while a right is held on a file;
	   bumping the file's inode reference count ensures this.  Second, if
	   rights are ever actually implemented, it will most likely be done
	   without changes to the on-disk inode, which means that we can't let
	   the inode become unreferenced while a right on it is held.
	*/

	if (error == 0) {
		if (!(tdp->td_flags & DM_TDF_RIGHT)) {	/* if first call */
			tdp->td_flags |= DM_TDF_RIGHT;
			tdp->td_vcount++;
			(void)fsys_vector->obj_ref_hold(tdp->td_ip);
		}
		tdp->td_right = right;
	}

	dm_app_put_tdp(tdp);
	return(error);
}


int
dm_upgrade_right(
	dm_sessid_t	sid,
	void		__user *hanp,
	size_t		hlen,
	dm_token_t	token)
{
	dm_fsys_vector_t *fsys_vector;
	dm_tokdata_t	*tdp;
	int		error;

	error = dm_app_get_tdp_by_token(sid, hanp, hlen, token, DM_TDT_ANY,
		DM_RIGHT_SHARED, DM_FG_MUSTEXIST|DM_FG_STHREAD, &tdp);
	if (error != 0)
		return(error);

	/* If the object already has the DM_RIGHT_EXCL right, no need to
	   attempt an upgrade.
	*/

	if (tdp->td_right == DM_RIGHT_EXCL) {
		dm_app_put_tdp(tdp);
		return(0);
	}

	/* Attempt the upgrade.	 Filesystems which support rights but not
	   the upgrading of rights will return ENOSYS.
	*/

	fsys_vector = dm_fsys_vector(tdp->td_ip);
	error = fsys_vector->upgrade_right(tdp->td_ip, tdp->td_right,
		(tdp->td_type == DM_TDT_VFS ? DM_FSYS_OBJ : 0));

	/* The tdp is single-threaded, so no mutex lock needed for update. */

	if (error == 0)
		tdp->td_right = DM_RIGHT_EXCL;

	dm_app_put_tdp(tdp);
	return(error);
}
