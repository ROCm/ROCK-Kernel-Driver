/*
 *  include/linux/fcblist.h ( File event callbacks handling )
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

#ifndef __LINUX_FCBLIST_H
#define __LINUX_FCBLIST_H

#include <linux/config.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/file.h>



/* file callback notification events */
#define ION_IN		1
#define ION_OUT		2
#define ION_HUP		3
#define ION_ERR		4

#define FCB_LOCAL_SIZE	4


struct fcb_struct {
	struct list_head llink;
	void (*cbproc)(struct file *, void *, unsigned long *, long *);
	void *data;
	unsigned long local[FCB_LOCAL_SIZE];
};


extern long ion_band_table[];
extern long poll_band_table[];


void file_notify_event(struct file *filep, long *event);

int file_notify_addcb(struct file *filep,
		      void (*cbproc)(struct file *, void *, unsigned long *, long *),
		      void *data);

int file_notify_delcb(struct file *filep,
		      void (*cbproc)(struct file *, void *, unsigned long *, long *));

void file_notify_cleanup(struct file *filep);


static inline void file_notify_init(struct file *filep)
{
	rwlock_init(&filep->f_cblock);
	INIT_LIST_HEAD(&filep->f_cblist);
}

static inline void file_send_notify(struct file *filep, long ioevt, long plevt)
{
	long event[] = { ioevt, plevt, -1 };

	file_notify_event(filep, event);
}

#endif
