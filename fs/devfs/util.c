/*  devfs (Device FileSystem) utilities.

    Copyright (C) 1999-2001  Richard Gooch

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
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/bitops.h>


/*  Private functions follow  */

/**
 *	_devfs_convert_name - Convert from an old style location-based name to new style.
 *	@new: The new name will be written here.
 *	@old: The old name.
 *	@disc: If true, disc partitioning information should be processed.
 */

static void __init _devfs_convert_name (char *new, const char *old, int disc)
{
    int host, bus, target, lun;
    char *ptr;
    char part[8];

    /*  Decode "c#b#t#u#"  */
    if (old[0] != 'c') return;
    host = simple_strtol (old + 1, &ptr, 10);
    if (ptr[0] != 'b') return;
    bus = simple_strtol (ptr + 1, &ptr, 10);
    if (ptr[0] != 't') return;
    target = simple_strtol (ptr + 1, &ptr, 10);
    if (ptr[0] != 'u') return;
    lun = simple_strtol (ptr + 1, &ptr, 10);
    if (disc)
    {
	/*  Decode "p#"  */
	if (ptr[0] == 'p') sprintf (part, "part%s", ptr + 1);
	else strcpy (part, "disc");
    }
    else part[0] = '\0';
    sprintf (new, "/host%d/bus%d/target%d/lun%d/%s",
	     host, bus, target, lun, part);
}   /*  End Function _devfs_convert_name  */


/*  Public functions follow  */

/**
 *	devfs_make_root - Create the root FS device entry if required.
 *	@name: The name of the root FS device, as passed by "root=".
 */

void __init devfs_make_root (const char *name)
{
    char dest[64];

    if ( (strncmp (name, "sd/", 3) == 0) || (strncmp (name, "sr/", 3) == 0) )
    {
	strcpy (dest, "../scsi");
	_devfs_convert_name (dest + 7, name + 3, (name[1] == 'd') ? 1 : 0);
    }
    else if ( (strncmp (name, "ide/hd/", 7) == 0) ||
	      (strncmp (name, "ide/cd/", 7) == 0) )
    {
	strcpy (dest, "..");
	_devfs_convert_name (dest + 2, name + 7, (name[4] == 'h') ? 1 : 0);
    }
    else return;
    devfs_mk_symlink (NULL, name, DEVFS_FL_DEFAULT, dest, NULL, NULL);
}   /*  End Function devfs_make_root  */


/**
 *	devfs_register_tape - Register a tape device in the "/dev/tapes" hierarchy.
 *	@de: Any tape device entry in the device directory.
 */

void devfs_register_tape (devfs_handle_t de)
{
    int pos;
    devfs_handle_t parent, slave;
    char name[16], dest[64];
    static unsigned int tape_counter;
    static devfs_handle_t tape_dir;

    if (tape_dir == NULL) tape_dir = devfs_mk_dir (NULL, "tapes", NULL);
    parent = devfs_get_parent (de);
    pos = devfs_generate_path (parent, dest + 3, sizeof dest - 3);
    if (pos < 0) return;
    strncpy (dest + pos, "../", 3);
    sprintf (name, "tape%u", tape_counter++);
    devfs_mk_symlink (tape_dir, name, DEVFS_FL_DEFAULT, dest + pos,
		      &slave, NULL);
    devfs_auto_unregister (de, slave);
}   /*  End Function devfs_register_tape  */
EXPORT_SYMBOL(devfs_register_tape);


/**
 *	devfs_register_series - Register a sequence of device entries.
 *	@dir: The handle to the parent devfs directory entry. If this is %NULL
 *		the new names are relative to the root of the devfs.
 *	@format: The printf-style format string. A single "\%u" is allowed.
 *	@num_entries: The number of entries to register.
 *	@flags: A set of bitwise-ORed flags (DEVFS_FL_*).
 *	@major: The major number. Not needed for regular files.
 *	@minor_start: The starting minor number. Not needed for regular files.
 *	@mode: The default file mode.
 *	@ops: The &file_operations or &block_device_operations structure.
 *		This must not be externally deallocated.
 *	@info: An arbitrary pointer which will be written to the private_data
 *		field of the &file structure passed to the device driver. You
 *		can set this to whatever you like, and change it once the file
 *		is opened (the next file opened will not see this change).
 */

void devfs_register_series (devfs_handle_t dir, const char *format,
			    unsigned int num_entries, unsigned int flags,
			    unsigned int major, unsigned int minor_start,
			    umode_t mode, void *ops, void *info)
{
    unsigned int count;
    char devname[128];

    for (count = 0; count < num_entries; ++count)
    {
	sprintf (devname, format, count);
	devfs_register (dir, devname, flags, major, minor_start + count,
			mode, ops, info);
    }
}   /*  End Function devfs_register_series  */
EXPORT_SYMBOL(devfs_register_series);


struct major_list
{
    spinlock_t lock;
    __u32 bits[8];
};

/*  Block majors already assigned:
    0-3, 7-9, 11-63, 65-99, 101-113, 120-127, 199, 201, 240-255
    Total free: 122
*/
static struct major_list block_major_list =
{SPIN_LOCK_UNLOCKED,
    {0xfffffb8f,  /*  Majors 0   to 31   */
     0xffffffff,  /*  Majors 32  to 63   */
     0xfffffffe,  /*  Majors 64  to 95   */
     0xff03ffef,  /*  Majors 96  to 127  */
     0x00000000,  /*  Majors 128 to 159  */
     0x00000000,  /*  Majors 160 to 191  */
     0x00000280,  /*  Majors 192 to 223  */
     0xffff0000}  /*  Majors 224 to 255  */
};

/*  Char majors already assigned:
    0-7, 9-151, 154-158, 160-211, 216-221, 224-230, 240-255
    Total free: 19
*/
static struct major_list char_major_list =
{SPIN_LOCK_UNLOCKED,
    {0xfffffeff,  /*  Majors 0   to 31   */
     0xffffffff,  /*  Majors 32  to 63   */
     0xffffffff,  /*  Majors 64  to 95   */
     0xffffffff,  /*  Majors 96  to 127  */
     0x7cffffff,  /*  Majors 128 to 159  */
     0xffffffff,  /*  Majors 160 to 191  */
     0x3f0fffff,  /*  Majors 192 to 223  */
     0xffff007f}  /*  Majors 224 to 255  */
};


/**
 *	devfs_alloc_major - Allocate a major number.
 *	@type: The type of the major (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK)

 *	Returns the allocated major, else -1 if none are available.
 *	This routine is thread safe and does not block.
 */

int devfs_alloc_major (char type)
{
    int major;
    struct major_list *list;

    list = (type == DEVFS_SPECIAL_CHR) ? &char_major_list : &block_major_list;
    spin_lock (&list->lock);
    major = find_first_zero_bit (list->bits, 256);
    if (major < 256) __set_bit (major, list->bits);
    else major = -1;
    spin_unlock (&list->lock);
    return major;
}   /*  End Function devfs_alloc_major  */
EXPORT_SYMBOL(devfs_alloc_major);


/**
 *	devfs_dealloc_major - Deallocate a major number.
 *	@type: The type of the major (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK)
 *	@major: The major number.
 *	This routine is thread safe and does not block.
 */

void devfs_dealloc_major (char type, int major)
{
    int was_set;
    struct major_list *list;

    if (major < 0) return;
    list = (type == DEVFS_SPECIAL_CHR) ? &char_major_list : &block_major_list;
    spin_lock (&list->lock);
    was_set = __test_and_clear_bit (major, list->bits);
    spin_unlock (&list->lock);
    if (!was_set)
	printk (KERN_ERR __FUNCTION__ "(): major %d was already free\n",
		major);
}   /*  End Function devfs_dealloc_major  */
EXPORT_SYMBOL(devfs_dealloc_major);


struct minor_list
{
    int major;
    __u32 bits[8];
    struct minor_list *next;
};

struct device_list
{
    struct minor_list *first, *last;
    int none_free;
};

static DECLARE_MUTEX (block_semaphore);
static struct device_list block_list;

static DECLARE_MUTEX (char_semaphore);
static struct device_list char_list;


/**
 *	devfs_alloc_devnum - Allocate a device number.
 *	@type: The type (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK).
 *
 *	Returns the allocated device number, else NODEV if none are available.
 *	This routine is thread safe and may block.
 */

kdev_t devfs_alloc_devnum (char type)
{
    int minor;
    struct semaphore *semaphore;
    struct device_list *list;
    struct minor_list *entry;

    if (type == DEVFS_SPECIAL_CHR)
    {
	semaphore = &char_semaphore;
	list = &char_list;
    }
    else
    {
	semaphore = &block_semaphore;
	list = &block_list;
    }
    if (list->none_free) return NODEV;  /*  Fast test  */
    down (semaphore);
    if (list->none_free)
    {
	up (semaphore);
	return NODEV;
    }
    for (entry = list->first; entry != NULL; entry = entry->next)
    {
	minor = find_first_zero_bit (entry->bits, 256);
	if (minor >= 256) continue;
	__set_bit (minor, entry->bits);
	up (semaphore);
	return MKDEV (entry->major, minor);
    }
    /*  Need to allocate a new major  */
    if ( ( entry = kmalloc (sizeof *entry, GFP_KERNEL) ) == NULL )
    {
	list->none_free = 1;
	up (semaphore);
	return NODEV;
    }
    memset (entry, 0, sizeof *entry);
    if ( ( entry->major = devfs_alloc_major (type) ) < 0 )
    {
	list->none_free = 1;
	up (semaphore);
	kfree (entry);
	return NODEV;
    }
    __set_bit (0, entry->bits);
    if (list->first == NULL) list->first = entry;
    else list->last->next = entry;
    list->last = entry;
    up (semaphore);
    return MKDEV (entry->major, 0);
}   /*  End Function devfs_alloc_devnum  */
EXPORT_SYMBOL(devfs_alloc_devnum);


/**
 *	devfs_dealloc_devnum - Dellocate a device number.
 *	@type: The type (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK).
 *	@devnum: The device number.
 *
 *	This routine is thread safe and does not block.
 */

void devfs_dealloc_devnum (char type, kdev_t devnum)
{
    int major, minor;
    struct semaphore *semaphore;
    struct device_list *list;
    struct minor_list *entry;

    if (devnum == NODEV) return;
    if (type == DEVFS_SPECIAL_CHR)
    {
	semaphore = &char_semaphore;
	list = &char_list;
    }
    else
    {
	semaphore = &block_semaphore;
	list = &block_list;
    }
    major = MAJOR (devnum);
    minor = MINOR (devnum);
    down (semaphore);
    for (entry = list->first; entry != NULL; entry = entry->next)
    {
	int was_set;

	if (entry->major != major) continue;
	was_set = __test_and_clear_bit (minor, entry->bits);
	if (was_set) list->none_free = 0;
	up (semaphore);
	if (!was_set)
	    printk ( KERN_ERR __FUNCTION__ "(): device %s was already free\n",
		     kdevname (devnum) );
	return;
    }
    up (semaphore);
    printk ( KERN_ERR __FUNCTION__ "(): major for %s not previously allocated\n",
	     kdevname (devnum) );
}   /*  End Function devfs_dealloc_devnum  */
EXPORT_SYMBOL(devfs_dealloc_devnum);


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
    __u32 *bits;

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
    if (!was_set)
	printk (KERN_ERR __FUNCTION__ "(): number %d was already free\n",
		number);
}   /*  End Function devfs_dealloc_unique_number  */
EXPORT_SYMBOL(devfs_dealloc_unique_number);
