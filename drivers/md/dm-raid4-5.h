/*
 * Copyright (C) 2006  Red Hat GmbH
 *
 * Module Author: Heinz Mauelshagen (Mauelshagen@RedHat.com)
 *
 * This file is released under the GPL.
 *
 */

#ifndef _DM_RAID45_H
#define _DM_RAID45_H

/* Factor out to dm.h! */
#define	STR_LEN(ptr, str) ptr, str, strlen(ptr)

enum lock_type { RAID45_EX, RAID45_SHARED };

struct dmraid45_locking_type {
        /* Request a lock on a stripe. */
        void* (*lock)(sector_t key, enum lock_type type);

        /* Release a lock on a stripe. */
        void (*unlock)(void *lock_handle);

};

#endif
