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


extern asmlinkage int pmdisk_arch_suspend(int resume);

/* Variables to be preserved over suspend */
extern int pagedir_order_check;
extern int nr_copy_pages_check;

/* For resume= kernel option */
static char resume_file[256] = CONFIG_PM_DISK_PARTITION;

static dev_t resume_device;
/* Local variables that should not be affected by save */
extern unsigned int nr_copy_pages;

/* Suspend pagedir is allocated before final copy, therefore it
   must be freed after resume 

   Warning: this is evil. There are actually two pagedirs at time of
   resume. One is "pagedir_save", which is empty frame allocated at
   time of suspend, that must be freed. Second is "pagedir_nosave", 
   allocated at time of resume, that travels through memory not to
   collide with anything.
 */
extern suspend_pagedir_t *pagedir_nosave;
extern suspend_pagedir_t *pagedir_save;
extern int pagedir_order;

extern struct swsusp_info swsusp_info;


#define PMDISK_SIG	"pmdisk-swap1"

struct pmdisk_header {
	char reserved[PAGE_SIZE - 20 - sizeof(swp_entry_t)];
	swp_entry_t pmdisk_info;
	char	orig_sig[10];
	char	sig[10];
} __attribute__((packed, aligned(PAGE_SIZE))) pmdisk_header;

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 */
#define PAGES_FOR_IO	512


/*
 * Saving part...
 */


/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

extern unsigned short swapfile_used[MAX_SWAPFILES];
extern unsigned short root_swap;
extern int swsusp_swap_check(void);
extern void swsusp_swap_lock(void);


static int mark_swapfiles(swp_entry_t prev)
{
	int error;

	rw_swap_page_sync(READ, 
			  swp_entry(root_swap, 0),
			  virt_to_page((unsigned long)&pmdisk_header));
	if (!memcmp("SWAP-SPACE",pmdisk_header.sig,10) ||
	    !memcmp("SWAPSPACE2",pmdisk_header.sig,10)) {
		memcpy(pmdisk_header.orig_sig,pmdisk_header.sig,10);
		memcpy(pmdisk_header.sig,PMDISK_SIG,10);
		pmdisk_header.pmdisk_info = prev;
		error = rw_swap_page_sync(WRITE, 
					  swp_entry(root_swap, 0),
					  virt_to_page((unsigned long)
						       &pmdisk_header));
	} else {
		pr_debug("pmdisk: Partition is not swap space.\n");
		error = -ENODEV;
	}
	return error;
}

extern int swsusp_write_page(unsigned long addr, swp_entry_t * entry);
extern int swsusp_data_write(void);
extern void swsusp_data_free(void);

/**
 *	free_pagedir - Free pages used by the page directory.
 */

static void free_pagedir_entries(void)
{
	int num = swsusp_info.pagedir_pages;
	int i;

	for (i = 0; i < num; i++)
		swap_free(swsusp_info.pagedir[i]);
}


/**
 *	write_pagedir - Write the array of pages holding the page directory.
 *	@last:	Last swap entry we write (needed for header).
 */

static int write_pagedir(void)
{
	unsigned long addr = (unsigned long)pagedir_nosave;
	int error = 0;
	int n = SUSPEND_PD_PAGES(nr_copy_pages);
	int i;

	swsusp_info.pagedir_pages = n;
	printk( "Writing pagedir (%d pages)\n", n);
	for (i = 0; i < n && !error; i++, addr += PAGE_SIZE)
		error = swsusp_write_page(addr,&swsusp_info.pagedir[i]);
	return error;
}

extern void swsusp_init_header(void);
extern int swsusp_write_header(swp_entry_t*);

/**
 *	write_suspend_image - Write entire image and metadata.
 *
 */

static int write_suspend_image(void)
{
	int error;
	swp_entry_t prev = { 0 };

	swsusp_init_header();
	if ((error = swsusp_data_write()))
		goto FreeData;

	if ((error = write_pagedir()))
		goto FreePagedir;

	if ((error = swsusp_write_header(&prev)))
		goto FreePagedir;

	error = mark_swapfiles(prev);
 Done:
	return error;
 FreePagedir:
	free_pagedir_entries();
 FreeData:
	swsusp_data_free();
	goto Done;
}


extern void free_suspend_pagedir(unsigned long);



/**
 *	suspend_save_image - Prepare and write saved image to swap.
 *
 *	IRQs are re-enabled here so we can resume devices and safely write
 *	to the swap devices. We disable them again before we leave.
 *
 *	The second swsusp_swap_lock() will unlock ignored swap devices since
 *	writing is finished.
 *	It is important _NOT_ to umount filesystems at this point. We want
 *	them synced (in case something goes wrong) but we DO not want to mark
 *	filesystem clean: it is not. (And it does not matter, if we resume
 *	correctly, we'll mark system clean, anyway.)
 */

static int suspend_save_image(void)
{
	int error;
	device_resume();
	swsusp_swap_lock();
	error = write_suspend_image();
	swsusp_swap_lock();
	return error;
}


/* More restore stuff */

extern struct block_device * resume_bdev;
extern int bio_read_page(pgoff_t page_off, void * page);
extern int bio_write_page(pgoff_t page_off, void * page);

extern dev_t __init name_to_dev_t(const char *line);


static int __init check_sig(void)
{
	int error;

	memset(&pmdisk_header,0,sizeof(pmdisk_header));
	if ((error = bio_read_page(0,&pmdisk_header)))
		return error;
	if (!memcmp(PMDISK_SIG,pmdisk_header.sig,10)) {
		memcpy(pmdisk_header.sig,pmdisk_header.orig_sig,10);

		/*
		 * Reset swap signature now.
		 */
		error = bio_write_page(0,&pmdisk_header);
	} else { 
		pr_debug(KERN_ERR "pmdisk: Invalid partition type.\n");
		return -EINVAL;
	}
	if (!error)
		pr_debug("pmdisk: Signature found, resuming\n");
	return error;
}


static int __init read_pagedir(void)
{
	unsigned long addr;
	int i, n = swsusp_info.pagedir_pages;
	int error = 0;

	pagedir_order = get_bitmask_order(n);

	addr =__get_free_pages(GFP_ATOMIC, pagedir_order);
	if (!addr)
		return -ENOMEM;
	pagedir_nosave = (struct pbe *)addr;

	pr_debug("pmdisk: Reading pagedir (%d Pages)\n",n);

	for (i = 0; i < n && !error; i++, addr += PAGE_SIZE) {
		unsigned long offset = swp_offset(swsusp_info.pagedir[i]);
		if (offset)
			error = bio_read_page(offset, (void *)addr);
		else
			error = -EFAULT;
	}
	if (error)
		free_pages((unsigned long)pagedir_nosave,pagedir_order);
	return error;
}


static int __init read_suspend_image(void)
{
	extern int swsusp_data_read(void);
	extern int swsusp_check_header(swp_entry_t);

	int error = 0;

	if ((error = check_sig()))
		return error;
	if ((error = swsusp_check_header(pmdisk_header.pmdisk_info)))
		return error;
	if ((error = read_pagedir()))
		return error;
	if ((error = swsusp_data_read())) {
		free_pages((unsigned long)pagedir_nosave,pagedir_order);
	}
	return error;
}


/**
 *	pmdisk_write - Write saved memory image to swap.
 *
 *	pmdisk_arch_suspend(0) returns after system is resumed.
 *
 *	pmdisk_arch_suspend() copies all "used" memory to "free" memory,
 *	then unsuspends all device drivers, and writes memory to disk
 *	using normal kernel mechanism.
 */

int pmdisk_write(void)
{
	return suspend_save_image();
}


/**
 *	pmdisk_read - Read saved image from swap.
 */

int __init pmdisk_read(void)
{
	int error;

	if (!strlen(resume_file))
		return -ENOENT;

	resume_device = name_to_dev_t(resume_file);
	pr_debug("pmdisk: Resume From Partition: %s\n", resume_file);

	resume_bdev = open_by_devnum(resume_device, FMODE_READ);
	if (!IS_ERR(resume_bdev)) {
		set_blocksize(resume_bdev, PAGE_SIZE);
		error = read_suspend_image();
		blkdev_put(resume_bdev);
	} else
		error = PTR_ERR(resume_bdev);

	if (!error)
		pr_debug("Reading resume file was successful\n");
	else
		pr_debug("pmdisk: Error %d resuming\n", error);
	return error;
}


/**
 *	pmdisk_free - Free memory allocated to hold snapshot.
 */

int pmdisk_free(void)
{
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

