/*
 * 1999 Copyright (C) Pavel Machek, pavel@ucw.cz. This code is GPL.
 * 1999/11/04 Copyright (C) 1999 VMware, Inc. (Regis "HPReg" Duchesne)
 *            Made nbd_end_request() use the io_request_lock
 */

#ifndef LINUX_NBD_H
#define LINUX_NBD_H

#define NBD_SET_SOCK	_IO( 0xab, 0 )
#define NBD_SET_BLKSIZE	_IO( 0xab, 1 )
#define NBD_SET_SIZE	_IO( 0xab, 2 )
#define NBD_DO_IT	_IO( 0xab, 3 )
#define NBD_CLEAR_SOCK	_IO( 0xab, 4 )
#define NBD_CLEAR_QUE	_IO( 0xab, 5 )
#define NBD_PRINT_DEBUG	_IO( 0xab, 6 )
#define NBD_SET_SIZE_BLOCKS	_IO( 0xab, 7 )
#define NBD_DISCONNECT  _IO( 0xab, 8 )

#ifdef MAJOR_NR

#include <linux/locks.h>
#include <asm/semaphore.h>

#define LOCAL_END_REQUEST

#include <linux/blk.h>

#ifdef PARANOIA
extern int requests_in;
extern int requests_out;
#endif

static int 
nbd_end_request(struct request *req)
{
	unsigned long flags;
	int ret = 0;

#ifdef PARANOIA
	requests_out++;
#endif
	/*
	 * This is a very dirty hack that we have to do to handle
	 * merged requests because end_request stuff is a bit
	 * broken. The fact we have to do this only if there
	 * aren't errors looks even more silly.
	 */
	if (!req->errors) {
		req->sector += req->current_nr_sectors;
		req->nr_sectors -= req->current_nr_sectors;
	}

	spin_lock_irqsave(&io_request_lock, flags);
	if (end_that_request_first( req, !req->errors, "nbd" ))
		goto out;
	ret = 1;
	end_that_request_last( req );

out:
	spin_unlock_irqrestore(&io_request_lock, flags);
	return ret;
}

#define MAX_NBD 128

struct nbd_device {
	int refcnt;	
	int flags;
	int harderror;		/* Code of hard error			*/
#define NBD_READ_ONLY 0x0001
#define NBD_WRITE_NOCHK 0x0002
	struct socket * sock;
	struct file * file; 		/* If == NULL, device is not ready, yet	*/
	int magic;			/* FIXME: not if debugging is off	*/
	struct list_head queue_head;	/* Requests are added here...			*/
	struct semaphore queue_lock;
};
#endif

/* This now IS in some kind of include file...	*/

/* These are send over network in request/reply magic field */

#define NBD_REQUEST_MAGIC 0x25609513
#define NBD_REPLY_MAGIC 0x67446698
/* Do *not* use magics: 0x12560953 0x96744668. */

/*
 * This is packet used for communication between client and
 * server. All data are in network byte order.
 */
struct nbd_request {
	u32 magic;
	u32 type;	/* == READ || == WRITE 	*/
	char handle[8];
	u64 from;
	u32 len;
}
#ifdef __GNUC__
	__attribute__ ((packed))
#endif
;

struct nbd_reply {
	u32 magic;
	u32 error;		/* 0 = ok, else error	*/
	char handle[8];		/* handle you got from request	*/
};
#endif
