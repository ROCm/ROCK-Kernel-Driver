/*
 *  linux/fs/fcblist.c ( File event callbacks handling )
 *  Copyright (C) 2001,...,2002  Davide Libenzi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/poll.h>
#include <asm/bitops.h>
#include <linux/fcblist.h>


long ion_band_table[NSIGPOLL] = {
	ION_IN,		/* POLL_IN */
	ION_OUT,	/* POLL_OUT */
	ION_IN,		/* POLL_MSG */
	ION_ERR,	/* POLL_ERR */
	0,		/* POLL_PRI */
	ION_HUP		/* POLL_HUP */
};

long poll_band_table[NSIGPOLL] = {
	POLLIN | POLLRDNORM,			/* POLL_IN */
	POLLOUT | POLLWRNORM | POLLWRBAND,	/* POLL_OUT */
	POLLIN | POLLRDNORM | POLLMSG,		/* POLL_MSG */
	POLLERR,				/* POLL_ERR */
	POLLPRI | POLLRDBAND,			/* POLL_PRI */
	POLLHUP | POLLERR			/* POLL_HUP */
};



/*
 * Walk through the file callback list by calling each registered callback
 * with the event that happened on the "filep" file. Callbacks are called
 * by holding a read lock on the callback list lock, and also by keeping
 * local IRQs disabled.
 */
void file_notify_event(struct file *filep, long *event)
{
	unsigned long flags;
	struct list_head *lnk, *lsthead;

	read_lock_irqsave(&filep->f_cblock, flags);

	lsthead = &filep->f_cblist;
	list_for_each(lnk, lsthead) {
		struct fcb_struct *fcbp = list_entry(lnk, struct fcb_struct, llink);

		fcbp->cbproc(filep, fcbp->data, fcbp->local, event);
	}

	read_unlock_irqrestore(&filep->f_cblock, flags);
}


/*
 * Add a new callback to the list of file callbacks.
 */
int file_notify_addcb(struct file *filep,
		      void (*cbproc)(struct file *, void *, unsigned long *, long *),
		      void *data)
{
	unsigned long flags;
	struct fcb_struct *fcbp;

	if (!(fcbp = (struct fcb_struct *) kmalloc(sizeof(struct fcb_struct), GFP_KERNEL)))
		return -ENOMEM;

	memset(fcbp, 0, sizeof(struct fcb_struct));
	fcbp->cbproc = cbproc;
	fcbp->data = data;

	write_lock_irqsave(&filep->f_cblock, flags);
	list_add_tail(&fcbp->llink, &filep->f_cblist);
	write_unlock_irqrestore(&filep->f_cblock, flags);

	return 0;
}


/*
 * Removes the callback "cbproc" from the file callback list.
 */
int file_notify_delcb(struct file *filep,
		      void (*cbproc)(struct file *, void *, unsigned long *, long *))
{
	unsigned long flags;
	struct list_head *lnk, *lsthead;

	write_lock_irqsave(&filep->f_cblock, flags);

	lsthead = &filep->f_cblist;
	list_for_each(lnk, lsthead) {
		struct fcb_struct *fcbp = list_entry(lnk, struct fcb_struct, llink);

		if (fcbp->cbproc == cbproc) {
			list_del(lnk);
			write_unlock_irqrestore(&filep->f_cblock, flags);
			kfree(fcbp);
			return 0;
		}
	}

	write_unlock_irqrestore(&filep->f_cblock, flags);

	return -ENOENT;
}


/*
 * It is called at file cleanup time and removes all the registered callbacks.
 */
void file_notify_cleanup(struct file *filep)
{
	unsigned long flags;
	struct list_head *lsthead;

	write_lock_irqsave(&filep->f_cblock, flags);

	lsthead = &filep->f_cblist;
	while (!list_empty(lsthead)) {
		struct fcb_struct *fcbp = list_entry(lsthead->next, struct fcb_struct, llink);

		list_del(lsthead->next);
		write_unlock_irqrestore(&filep->f_cblock, flags);
		kfree(fcbp);
		write_lock_irqsave(&filep->f_cblock, flags);
	}

	write_unlock_irqrestore(&filep->f_cblock, flags);
}

