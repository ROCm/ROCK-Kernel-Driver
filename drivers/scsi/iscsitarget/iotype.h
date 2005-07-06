/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include "iscsi.h"

#ifndef __IOTYPE_H__
#define __IOTYPE_H__

struct iotype {
	const char *name;
	struct list_head iot_list;

	int (*attach)(struct iet_volume *dev, char *args);
	int (*make_request)(struct iet_volume *dev, struct tio *tio, int rw);
	int (*sync)(struct iet_volume *dev, struct tio *tio);
	void (*detach)(struct iet_volume *dev);
	void (*show)(struct iet_volume *dev, struct seq_file *seq);
};

extern struct iotype fileio;
extern struct iotype nullio;

extern int iotype_init(void);
extern void iotype_exit(void);

#endif
