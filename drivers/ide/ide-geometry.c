/*
 * linux/drivers/ide/ide-geometry.c
 */
#include <linux/config.h>
#include <linux/ide.h>
#include <linux/mc146818rtc.h>
#include <asm/io.h>

extern unsigned long current_capacity (ide_drive_t *);

/*
 * If heads is nonzero: find a translation with this many heads and S=63.
 * Otherwise: find out how OnTrack Disk Manager would translate the disk.
 */

static void ontrack(ide_drive_t *drive, int heads, unsigned int *c, int *h, int *s) 
{
	static const u8 dm_head_vals[] = {4, 8, 16, 32, 64, 128, 255, 0};
	const u8 *headp = dm_head_vals;
	unsigned long total;

	/*
	 * The specs say: take geometry as obtained from Identify,
	 * compute total capacity C*H*S from that, and truncate to
	 * 1024*255*63. Now take S=63, H the first in the sequence
	 * 4, 8, 16, 32, 64, 128, 255 such that 63*H*1024 >= total.
	 * [Please tell aeb@cwi.nl in case this computes a
	 * geometry different from what OnTrack uses.]
	 */
	total = DRIVER(drive)->capacity(drive);

	*s = 63;

	if (heads) {
		*h = heads;
		*c = total / (63 * heads);
		return;
	}

	while (63 * headp[0] * 1024 < total && headp[1] != 0)
		 headp++;
	*h = headp[0];
	*c = total / (63 * headp[0]);
}

/*
 * This routine is called from the partition-table code in pt/msdos.c.
 * It has two tasks:
 * (i) to handle Ontrack DiskManager by offsetting everything by 63 sectors,
 *  or to handle EZdrive by remapping sector 0 to sector 1.
 * (ii) to invent a translated geometry.
 * Part (i) is suppressed if the user specifies the "noremap" option
 * on the command line.
 * Part (ii) is suppressed if the user specifies an explicit geometry.
 *
 * The ptheads parameter is either 0 or tells about the number of
 * heads shown by the end of the first nonempty partition.
 * If this is either 16, 32, 64, 128, 240 or 255 we'll believe it.
 *
 * The xparm parameter has the following meaning:
 *	 0 = convert to CHS with fewer than 1024 cyls
 *	     using the same method as Ontrack DiskManager.
 *	 1 = same as "0", plus offset everything by 63 sectors.
 *	-1 = similar to "0", plus redirect sector 0 to sector 1.
 *	 2 = convert to a CHS geometry with "ptheads" heads.
 *
 * Returns 0 if the translation was not possible, if the device was not 
 * an IDE disk drive, or if a geometry was "forced" on the commandline.
 * Returns 1 if the geometry translation was successful.
 */

int ide_xlate_1024 (struct block_device *bdev, int xparm, int ptheads, const char *msg)
{
	ide_drive_t *drive = bdev->bd_disk->private_data;
	const char *msg1 = "";
	int heads = 0;
	int c, h, s;
	int transl = 1;		/* try translation */
	int ret = 0;

	/* remap? */
	if (drive->remap_0_to_1 != 2) {
		if (xparm == 1) {		/* DM */
			drive->sect0 = 63;
			msg1 = " [remap +63]";
			ret = 1;
		} else if (xparm == -1) {	/* EZ-Drive */
			if (drive->remap_0_to_1 == 0) {
				drive->remap_0_to_1 = 1;
				msg1 = " [remap 0->1]";
				ret = 1;
			}
		}
	}

	/* There used to be code here that assigned drive->id->CHS
	   to drive->CHS and that to drive->bios_CHS. However,
	   some disks have id->C/H/S = 4092/16/63 but are larger than 2.1 GB.
	   In such cases that code was wrong.  Moreover,
	   there seems to be no reason to do any of these things. */

	/* translate? */
	if (drive->forced_geom)
		transl = 0;

	/* does ptheads look reasonable? */
	if (ptheads == 32 || ptheads == 64 || ptheads == 128 ||
	    ptheads == 240 || ptheads == 255)
		heads = ptheads;

	if (xparm == 2) {
		if (!heads ||
		   (drive->bios_head >= heads && drive->bios_sect == 63))
			transl = 0;
	}
	if (xparm == -1) {
		if (drive->bios_head > 16)
			transl = 0;     /* we already have a translation */
	}

	if (transl) {
		ontrack(drive, heads, &c, &h, &s);
		drive->bios_cyl = c;
		drive->bios_head = h;
		drive->bios_sect = s;
		ret = 1;
	}

	set_capacity(drive->disk, current_capacity(drive));

	if (ret)
		printk("%s%s [%d/%d/%d]", msg, msg1,
		       drive->bios_cyl, drive->bios_head, drive->bios_sect);
	return ret;
}
