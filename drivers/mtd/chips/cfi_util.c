/*
 * Common Flash Interface support:
 *   Generic utility functions not dependant on command set
 *
 * Copyright (C) 2002 Red Hat
 * Copyright (C) 2003 STMicroelectronics Limited
 *
 * This code is covered by the GPL.
 *
 * $Id: cfi_util.c,v 1.4 2004/07/14 08:38:44 dwmw2 Exp $
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mtd/map.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/compatmac.h>

struct cfi_extquery *
cfi_read_pri(struct map_info *map, __u16 adr, __u16 size, const char* name)
{
	struct cfi_private *cfi = map->fldrv_priv;
	__u32 base = 0; // cfi->chips[0].start;
	int ofs_factor = cfi->interleave * cfi->device_type;
	int i;
	struct cfi_extquery *extp = NULL;

	printk(" %s Extended Query Table at 0x%4.4X\n", name, adr);
	if (!adr)
		goto out;

	/* Switch it into Query Mode */
	cfi_send_gen_cmd(0x98, 0x55, base, map, cfi, cfi->device_type, NULL);

	extp = kmalloc(size, GFP_KERNEL);
	if (!extp) {
		printk(KERN_ERR "Failed to allocate memory\n");
		goto out;
	}
		
	/* Read in the Extended Query Table */
	for (i=0; i<size; i++) {
		((unsigned char *)extp)[i] = 
			cfi_read_query(map, base+((adr+i)*ofs_factor));
	}

	if (extp->MajorVersion != '1' || 
	    (extp->MinorVersion < '0' || extp->MinorVersion > '3')) {
		printk(KERN_WARNING "  Unknown %s Extended Query "
		       "version %c.%c.\n",  name, extp->MajorVersion,
		       extp->MinorVersion);
		kfree(extp);
		extp = NULL;
		goto out;
	}

out:
	/* Make sure it's in read mode */
	cfi_send_gen_cmd(0xf0, 0, base, map, cfi, cfi->device_type, NULL);
	cfi_send_gen_cmd(0xff, 0, base, map, cfi, cfi->device_type, NULL);

	return extp;
}

EXPORT_SYMBOL(cfi_read_pri);

void cfi_fixup(struct map_info *map, struct cfi_fixup* fixups)
{
	struct cfi_private *cfi = map->fldrv_priv;
	struct cfi_fixup *f;

	for (f=fixups; f->fixup; f++) {
		if (((f->mfr == CFI_MFR_ANY) || (f->mfr == cfi->mfr)) &&
		    ((f->id  == CFI_ID_ANY)  || (f->id  == cfi->id))) {
			f->fixup(map, f->param);
		}
	}
}

EXPORT_SYMBOL(cfi_fixup);

MODULE_LICENSE("GPL");
