/*
 * kernel/power/pmdisk.c - Suspend-to-disk implmentation
 *
 * This STD implementation is initially derived from swsusp (suspend-to-swap).
 * The original copyright on that was: 
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001,2002 Pavel Machek <pavel@suse.cz>
 *
 * The additional parts are: 
 * 
 * Copyright (C) 2003 Patrick Mochel
 * Copyright (C) 2003 Open Source Development Lab
 * 
 * This file is released under the GPLv2. 
 *
 * For more information, please see the text files in Documentation/power/
 *
 */

#undef DEBUG

#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/suspend.h>
#include <linux/version.h>
#include <linux/reboot.h>
#include <linux/device.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>

#include <asm/mmu_context.h>

#include "power.h"

/* For resume= kernel option */
static char resume_file[256] = CONFIG_PM_DISK_PARTITION;
extern suspend_pagedir_t *pagedir_save;

/*
 * Saving part...
 */


/**
 *	pmdisk_free - Free memory allocated to hold snapshot.
 */

int pmdisk_free(void)
{
	extern void free_suspend_pagedir(unsigned long this_pagedir);
	pr_debug( "Freeing prev allocated pagedir\n" );
	free_suspend_pagedir((unsigned long)pagedir_save);
	return 0;
}

static int __init pmdisk_setup(char *str)
{
	if (strlen(str)) {
		if (!strcmp(str,"off"))
			resume_file[0] = '\0';
		else
			strncpy(resume_file, str, 255);
	} else
		resume_file[0] = '\0';
	return 1;
}

__setup("pmdisk=", pmdisk_setup);

