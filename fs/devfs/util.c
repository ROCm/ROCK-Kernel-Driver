/*  devfs (Device FileSystem) utilities.

    Copyright (C) 1999-2002  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    ChangeLog

    19991031   Richard Gooch <rgooch@atnf.csiro.au>
               Created.
    19991103   Richard Gooch <rgooch@atnf.csiro.au>
               Created <_devfs_convert_name> and supported SCSI and IDE CD-ROMs
    20000203   Richard Gooch <rgooch@atnf.csiro.au>
               Changed operations pointer type to void *.
    20000621   Richard Gooch <rgooch@atnf.csiro.au>
               Changed interface to <devfs_register_series>.
    20000622   Richard Gooch <rgooch@atnf.csiro.au>
               Took account of interface change to <devfs_mk_symlink>.
               Took account of interface change to <devfs_mk_dir>.
    20010519   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation cleanup.
    20010709   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_*alloc_major> and <devfs_*alloc_devnum>.
    20010710   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_*alloc_unique_number>.
    20010730   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation typo fix.
    20010806   Richard Gooch <rgooch@atnf.csiro.au>
               Made <block_semaphore> and <char_semaphore> private.
    20010813   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bug in <devfs_alloc_unique_number>: limited to 128 numbers
    20010818   Richard Gooch <rgooch@atnf.csiro.au>
               Updated major masks up to Linus' "no new majors" proclamation.
	       Block: were 126 now 122 free, char: were 26 now 19 free.
    20020324   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bug in <devfs_alloc_unique_number>: was clearing beyond
	       bitfield.
    20020326   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bitfield data type for <devfs_*alloc_devnum>.
               Made major bitfield type and initialiser 64 bit safe.
    20020413   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed shift warning on 64 bit machines.
    20020428   Richard Gooch <rgooch@atnf.csiro.au>
               Copied and used macro for error messages from fs/devfs/base.c 
    20021013   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation fix.
    20030101   Adam J. Richter <adam@yggdrasil.com>
               Eliminate DEVFS_SPECIAL_{CHR,BLK}.  Use mode_t instead.
    20030106   Christoph Hellwig <hch@infradead.org>
               Rewrite devfs_{,de}alloc_devnum to look like C code.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <asm/bitops.h>
#include "internal.h"

#define PRINTK(format, args...) \
   {printk (KERN_ERR "%s" format, __FUNCTION__ , ## args);}

int devfs_register_tape(const char *name)
{
	char tname[32], dest[64];
	static unsigned int tape_counter;
	unsigned int n = tape_counter++;

	sprintf(dest, "../%s", name);
	sprintf(tname, "tapes/tape%u", n);
	devfs_mk_symlink(tname, dest);

	return n;
}
EXPORT_SYMBOL(devfs_register_tape);

void devfs_unregister_tape(int num)
{
	if (num >= 0)
		devfs_remove("tapes/tape%u", num);
}

EXPORT_SYMBOL(devfs_unregister_tape);

struct major_list
{
    spinlock_t lock;
    unsigned long bits[256 / BITS_PER_LONG];
};
#if BITS_PER_LONG == 32
#  define INITIALISER64(low,high) (low), (high)
#else
#  define INITIALISER64(low,high) ( (unsigned long) (high) << 32 | (low) )
#endif

/*  Block majors already assigned:
    0-3, 7-9, 11-63, 65-99, 101-113, 120-127, 199, 201, 240-255
    Total free: 122
*/
static struct major_list block_major_list =
{SPIN_LOCK_UNLOCKED,
    {INITIALISER64 (0xfffffb8f, 0xffffffff),  /*  Majors 0-31,    32-63    */
     INITIALISER64 (0xfffffffe, 0xff03ffef),  /*  Majors 64-95,   96-127   */
     INITIALISER64 (0x00000000, 0x00000000),  /*  Majors 128-159, 160-191  */
     INITIALISER64 (0x00000280, 0xffff0000),  /*  Majors 192-223, 224-255  */
    }
};

/*  Char majors already assigned:
    0-7, 9-151, 154-158, 160-211, 216-221, 224-230, 240-255
    Total free: 19
*/
static struct major_list char_major_list =
{SPIN_LOCK_UNLOCKED,
    {INITIALISER64 (0xfffffeff, 0xffffffff),  /*  Majors 0-31,    32-63    */
     INITIALISER64 (0xffffffff, 0xffffffff),  /*  Majors 64-95,   96-127   */
     INITIALISER64 (0x7cffffff, 0xffffffff),  /*  Majors 128-159, 160-191  */
     INITIALISER64 (0x3f0fffff, 0xffff007f),  /*  Majors 192-223, 224-255  */
    }
};


/**
 *	devfs_alloc_major - Allocate a major number.
 *	@mode: The file mode (must be block device or character device).
 *	Returns the allocated major, else -1 if none are available.
 *	This routine is thread safe and does not block.
 */


struct minor_list
{
    int major;
    unsigned long bits[256 / BITS_PER_LONG];
    struct minor_list *next;
};

static struct device_list {
	struct minor_list	*first;
	struct minor_list	*last;
	int			none_free;
} block_list, char_list;

static DECLARE_MUTEX(device_list_mutex);


/**
 *	devfs_alloc_devnum - Allocate a device number.
 *	@mode: The file mode (must be block device or character device).
 *
 *	Returns the allocated device number, else NODEV if none are available.
 *	This routine is thread safe and may block.
 */

dev_t devfs_alloc_devnum(umode_t mode)
{
	struct device_list *list;
	struct major_list *major_list;
	struct minor_list *entry;
	int minor;

	if (S_ISCHR(mode)) {
		major_list = &char_major_list;
		list = &char_list;
	} else {
		major_list = &block_major_list;
		list = &block_list;
	}

	down(&device_list_mutex);
	if (list->none_free)
		goto out_unlock;

	for (entry = list->first; entry; entry = entry->next) {
		minor = find_first_zero_bit (entry->bits, 256);
		if (minor >= 256)
			continue;
		goto out_done;
	}
	
	/*  Need to allocate a new major  */
	entry = kmalloc (sizeof *entry, GFP_KERNEL);
	if (!entry)
		goto out_full;
	memset(entry, 0, sizeof *entry);

	spin_lock(&major_list->lock);
	entry->major = find_first_zero_bit(major_list->bits, 256);
	if (entry->major >= 256) {
		spin_unlock(&major_list->lock);
		kfree(entry);
		goto out_full;
	}
	__set_bit(entry->major, major_list->bits);
	spin_unlock(&major_list->lock);

	if (!list->first)
		list->first = entry;
	else
		list->last->next = entry;
	list->last = entry;

	minor = 0;
 out_done:
	__set_bit(minor, entry->bits);
	up(&device_list_mutex);
	return MKDEV(entry->major, minor);
 out_full:
	list->none_free = 1;
 out_unlock:
	up(&device_list_mutex);
	return 0;
}


/**
 *	devfs_dealloc_devnum - Dellocate a device number.
 *	@mode: The file mode (must be block device or character device).
 *	@devnum: The device number.
 *
 *	This routine is thread safe and may block.
 */

void devfs_dealloc_devnum(umode_t mode, dev_t devnum)
{
	struct device_list *list = S_ISCHR(mode) ? &char_list : &block_list;
	struct minor_list *entry;

	if (!devnum)
		return;

	down(&device_list_mutex);
	for (entry = list->first; entry; entry = entry->next) {
		if (entry->major == MAJOR(devnum)) {
			if (__test_and_clear_bit(MINOR(devnum), entry->bits))
				list->none_free = 0;
			break;
		}
	}
	up(&device_list_mutex);
}

struct unique_numspace
{
    spinlock_t init_lock;
    unsigned char sem_initialised;
    unsigned int num_free;          /*  Num free in bits       */
    unsigned int length;            /*  Array length in bytes  */
    unsigned long *bits;
    struct semaphore semaphore;
};

#define UNIQUE_NUMBERSPACE_INITIALISER {SPIN_LOCK_UNLOCKED, 0, 0, 0, NULL}


/**
 *	devfs_alloc_unique_number - Allocate a unique (positive) number.
 *	@space: The number space to allocate from.
 *
 *	Returns the allocated unique number, else a negative error code.
 *	This routine is thread safe and may block.
 */

int devfs_alloc_unique_number (struct unique_numspace *space)
{
    int number;
    unsigned int length;

    /*  Get around stupid lack of semaphore initialiser  */
    spin_lock (&space->init_lock);
    if (!space->sem_initialised)
    {
	sema_init (&space->semaphore, 1);
	space->sem_initialised = 1;
    }
    spin_unlock (&space->init_lock);
    down (&space->semaphore);
    if (space->num_free < 1)
    {
	void *bits;

	if (space->length < 16) length = 16;
	else length = space->length << 1;
	if ( ( bits = vmalloc (length) ) == NULL )
	{
	    up (&space->semaphore);
	    return -ENOMEM;
	}
	if (space->bits != NULL)
	{
	    memcpy (bits, space->bits, space->length);
	    vfree (space->bits);
	}
	space->num_free = (length - space->length) << 3;
	space->bits = bits;
	memset (bits + space->length, 0, length - space->length);
	space->length = length;
    }
    number = find_first_zero_bit (space->bits, space->length << 3);
    --space->num_free;
    __set_bit (number, space->bits);
    up (&space->semaphore);
    return number;
}   /*  End Function devfs_alloc_unique_number  */
EXPORT_SYMBOL(devfs_alloc_unique_number);


/**
 *	devfs_dealloc_unique_number - Deallocate a unique (positive) number.
 *	@space: The number space to deallocate from.
 *	@number: The number to deallocate.
 *
 *	This routine is thread safe and may block.
 */

void devfs_dealloc_unique_number (struct unique_numspace *space, int number)
{
    int was_set;

    if (number < 0) return;
    down (&space->semaphore);
    was_set = __test_and_clear_bit (number, space->bits);
    if (was_set) ++space->num_free;
    up (&space->semaphore);
    if (!was_set) PRINTK ("(): number %d was already free\n", number);
}   /*  End Function devfs_dealloc_unique_number  */
EXPORT_SYMBOL(devfs_dealloc_unique_number);

static struct unique_numspace disc_numspace = UNIQUE_NUMBERSPACE_INITIALISER;
static struct unique_numspace cdrom_numspace = UNIQUE_NUMBERSPACE_INITIALISER;

void devfs_create_partitions(struct gendisk *disk)
{
	char dirname[64], diskname[64], symlink[16];

	if (!disk->devfs_name)
		sprintf(disk->devfs_name, "%s/disc%d", disk->disk_name,
				disk->first_minor >> disk->minor_shift);

	devfs_mk_dir(disk->devfs_name);
	disk->number = devfs_alloc_unique_number(&disc_numspace);

	sprintf(diskname, "%s/disc", disk->devfs_name);
	devfs_register(NULL, diskname, 0,
			disk->major, disk->first_minor,
			S_IFBLK | S_IRUSR | S_IWUSR,
			disk->fops, NULL);

	sprintf(symlink, "discs/disc%d", disk->number);
	sprintf(dirname, "../%s", disk->devfs_name);
	devfs_mk_symlink(symlink, dirname);

}

void devfs_create_cdrom(struct gendisk *disk)
{
	char dirname[64], cdname[64], symlink[16];

	if (!disk->devfs_name)
		strcat(disk->devfs_name, disk->disk_name);

	devfs_mk_dir(disk->devfs_name);
	disk->number = devfs_alloc_unique_number(&cdrom_numspace);

	sprintf(cdname, "%s/cd", disk->devfs_name);
	devfs_register(NULL, cdname, 0,
			disk->major, disk->first_minor,
			S_IFBLK | S_IRUGO | S_IWUGO,
			disk->fops, NULL);

	sprintf(symlink, "cdroms/cdrom%d", disk->number);
	sprintf(dirname, "../%s", disk->devfs_name);
	devfs_mk_symlink(symlink, dirname);
}

void devfs_register_partition(struct gendisk *dev, int part)
{
	char devname[64];

	sprintf(devname, "%s/part%d", dev->devfs_name, part);
	devfs_register(NULL, devname, 0,
			dev->major, dev->first_minor + part,
			S_IFBLK | S_IRUSR | S_IWUSR,
			dev->fops, NULL);
}

void devfs_remove_partitions(struct gendisk *disk)
{
	devfs_remove("discs/disc%d", disk->number);
	devfs_remove(disk->devfs_name);
	devfs_dealloc_unique_number(&disc_numspace, disk->number);
}

void devfs_remove_cdrom(struct gendisk *disk)
{
	devfs_remove("cdroms/cdrom%d", disk->number);
	devfs_remove(disk->devfs_name);
	devfs_dealloc_unique_number(&cdrom_numspace, disk->number);
}
