/*
 * include/linux/nfsd/interface.h
 *
 * defines interface between nfsd and other bits of
 * the kernel.  Particularly filesystems (eventually).
 *
 * Copyright (C) 2000 Neil Brown <neilb@cse.unsw.edu.au>
 */

#ifndef LINUX_NFSD_INTERFACE_H
#define LINUX_NFSD_INTERFACE_H

#include <linux/config.h>

#ifdef CONFIG_NFSD_MODULE

extern struct nfsd_linkage {
	long (*do_nfsservctl)(int cmd, void *argp, void *resp);
} * nfsd_linkage;

#endif

#endif /* LINUX_NFSD_INTERFACE_H */
