/*
 * advansys.h - Linux Host Driver for AdvanSys SCSI Adapters
 * 
 * Copyright (c) 1995-2000 Advanced System Products, Inc.
 * Copyright (c) 2000-2001 ConnectCom Solutions, Inc.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * As of March 8, 2000 Advanced System Products, Inc. (AdvanSys)
 * changed its name to ConnectCom Solutions, Inc.
 *
 * There is an AdvanSys Linux WWW page at:
 *  http://www.connectcom.net/downloads/software/os/linux.html
 *  http://www.advansys.com/linux.html
 *
 * The latest released version of the AdvanSys driver is available at:
 *  ftp://ftp.advansys.com/pub/linux/linux.tgz
 *  ftp://ftp.connectcom.net/pub/linux/linux.tgz
 *
 * Please send questions, comments, bug reports to:
 *  linux@connectcom.net or bfrey@turbolinux.com.cn
 */

#ifndef _ADVANSYS_H
#define _ADVANSYS_H

#include <linux/config.h>
#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif /* LINUX_VERSION_CODE */

/* Convert Linux Version, Patch-level, Sub-level to LINUX_VERSION_CODE. */
#define ASC_LINUX_VERSION(V, P, S)    (((V) * 65536) + ((P) * 256) + (S))
/* Driver supported only in version 2.2 and version >= 2.4. */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(2,2,0) || \
    (LINUX_VERSION_CODE > ASC_LINUX_VERSION(2,3,0) && \
     LINUX_VERSION_CODE < ASC_LINUX_VERSION(2,4,0))
#error "AdvanSys driver supported only in 2.2 and 2.4 or greater kernels."
#endif
#define ASC_LINUX_KERNEL22 (LINUX_VERSION_CODE < ASC_LINUX_VERSION(2,4,0))
#define ASC_LINUX_KERNEL24 (LINUX_VERSION_CODE >= ASC_LINUX_VERSION(2,4,0))

/*
 * Scsi_Host_Template function prototypes.
 */
int advansys_detect(struct scsi_host_template *);
int advansys_release(struct Scsi_Host *);
const char *advansys_info(struct Scsi_Host *);
int advansys_queuecommand(struct scsi_cmnd *, void (* done)(struct scsi_cmnd *));
int advansys_reset(struct scsi_cmnd *);
int advansys_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int[]);
static int advansys_slave_configure(struct scsi_device *);

/* init/main.c setup function */
void advansys_setup(char *, int *);

#endif /* _ADVANSYS_H */
