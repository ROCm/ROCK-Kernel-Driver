/*
 *  linux/fs/ufs/super.c
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 */

/* Derived from
 *
 *  linux/fs/ext2/super.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */
 
/*
 * Inspired by
 *
 *  linux/fs/ufs/super.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * Copyright (C) 1996  Eddie C. Dost  (ecd@skynet.be)
 *
 * Kernel module support added on 96/04/26 by
 * Stefan Reinauer <stepan@home.culture.mipt.ru>
 *
 * Module usage counts added on 96/04/29 by
 * Gertjan van Wingerde <gertjan@cs.vu.nl>
 *
 * Clean swab support on 19970406 by
 * Francois-Rene Rideau <fare@tunes.org>
 *
 * 4.4BSD (FreeBSD) support added on February 1st 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk> partially based
 * on code by Martin von Loewis <martin@mira.isdn.cs.tu-berlin.de>.
 *
 * NeXTstep support added on February 5th 1998 by
 * Niels Kristian Bech Jensen <nkbj@image.dk>.
 *
 * write support Daniel Pirkl <daniel.pirkl@email.cz> 1998
 * 
 * HP/UX hfs filesystem support added by
 * Martin K. Petersen <mkp@mkp.net>, August 1999
 *
 */


#include <linux/config.h>
#include <linux/module.h>

#include <stdarg.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ufs_fs.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/blkdev.h>
#include <linux/init.h>

#include "swab.h"
#include "util.h"

#undef UFS_SUPER_DEBUG
#undef UFS_SUPER_DEBUG_MORE

#ifdef UFS_SUPER_DEBUG
#define UFSD(x) printk("(%s, %d), %s: ", __FILE__, __LINE__, __FUNCTION__); printk x;
#else
#define UFSD(x)
#endif

#ifdef UFS_SUPER_DEBUG_MORE
/*
 * Print contents of ufs_super_block, useful for debugging
 */
void ufs_print_super_stuff(struct ufs_super_block_first * usb1,
	struct ufs_super_block_second * usb2, 
	struct ufs_super_block_third * usb3, unsigned swab)
{
	printk("ufs_print_super_stuff\n");
	printk("size of usb:     %u\n", sizeof(struct ufs_super_block));
	printk("  magic:         0x%x\n", SWAB32(usb3->fs_magic));
	printk("  sblkno:        %u\n", SWAB32(usb1->fs_sblkno));
	printk("  cblkno:        %u\n", SWAB32(usb1->fs_cblkno));
	printk("  iblkno:        %u\n", SWAB32(usb1->fs_iblkno));
	printk("  dblkno:        %u\n", SWAB32(usb1->fs_dblkno));
	printk("  cgoffset:      %u\n", SWAB32(usb1->fs_cgoffset));
	printk("  ~cgmask:       0x%x\n", ~SWAB32(usb1->fs_cgmask));
	printk("  size:          %u\n", SWAB32(usb1->fs_size));
	printk("  dsize:         %u\n", SWAB32(usb1->fs_dsize));
	printk("  ncg:           %u\n", SWAB32(usb1->fs_ncg));
	printk("  bsize:         %u\n", SWAB32(usb1->fs_bsize));
	printk("  fsize:         %u\n", SWAB32(usb1->fs_fsize));
	printk("  frag:          %u\n", SWAB32(usb1->fs_frag));
	printk("  fragshift:     %u\n", SWAB32(usb1->fs_fragshift));
	printk("  ~fmask:        %u\n", ~SWAB32(usb1->fs_fmask));
	printk("  fshift:        %u\n", SWAB32(usb1->fs_fshift));
	printk("  sbsize:        %u\n", SWAB32(usb1->fs_sbsize));
	printk("  spc:           %u\n", SWAB32(usb1->fs_spc));
	printk("  cpg:           %u\n", SWAB32(usb1->fs_cpg));
	printk("  ipg:           %u\n", SWAB32(usb1->fs_ipg));
	printk("  fpg:           %u\n", SWAB32(usb1->fs_fpg));
	printk("  csaddr:        %u\n", SWAB32(usb1->fs_csaddr));
	printk("  cssize:        %u\n", SWAB32(usb1->fs_cssize));
	printk("  cgsize:        %u\n", SWAB32(usb1->fs_cgsize));
	printk("  fstodb:        %u\n", SWAB32(usb1->fs_fsbtodb));
	printk("  contigsumsize: %d\n", SWAB32(usb3->fs_u2.fs_44.fs_contigsumsize));
	printk("  postblformat:  %u\n", SWAB32(usb3->fs_postblformat));
	printk("  nrpos:         %u\n", SWAB32(usb3->fs_nrpos));
	printk("  ndir           %u\n", SWAB32(usb1->fs_cstotal.cs_ndir));
	printk("  nifree         %u\n", SWAB32(usb1->fs_cstotal.cs_nifree));
	printk("  nbfree         %u\n", SWAB32(usb1->fs_cstotal.cs_nbfree));
	printk("  nffree         %u\n", SWAB32(usb1->fs_cstotal.cs_nffree));
	printk("\n");
}


/*
 * Print contents of ufs_cylinder_group, useful for debugging
 */
void ufs_print_cylinder_stuff(struct ufs_cylinder_group *cg, unsigned swab)
{
	printk("\nufs_print_cylinder_stuff\n");
	printk("size of ucg: %u\n", sizeof(struct ufs_cylinder_group));
	printk("  magic:        %x\n", SWAB32(cg->cg_magic));
	printk("  time:         %u\n", SWAB32(cg->cg_time));
	printk("  cgx:          %u\n", SWAB32(cg->cg_cgx));
	printk("  ncyl:         %u\n", SWAB16(cg->cg_ncyl));
	printk("  niblk:        %u\n", SWAB16(cg->cg_niblk));
	printk("  ndblk:        %u\n", SWAB32(cg->cg_ndblk));
	printk("  cs_ndir:      %u\n", SWAB32(cg->cg_cs.cs_ndir));
	printk("  cs_nbfree:    %u\n", SWAB32(cg->cg_cs.cs_nbfree));
	printk("  cs_nifree:    %u\n", SWAB32(cg->cg_cs.cs_nifree));
	printk("  cs_nffree:    %u\n", SWAB32(cg->cg_cs.cs_nffree));
	printk("  rotor:        %u\n", SWAB32(cg->cg_rotor));
	printk("  frotor:       %u\n", SWAB32(cg->cg_frotor));
	printk("  irotor:       %u\n", SWAB32(cg->cg_irotor));
	printk("  frsum:        %u, %u, %u, %u, %u, %u, %u, %u\n",
	    SWAB32(cg->cg_frsum[0]), SWAB32(cg->cg_frsum[1]),
	    SWAB32(cg->cg_frsum[2]), SWAB32(cg->cg_frsum[3]),
	    SWAB32(cg->cg_frsum[4]), SWAB32(cg->cg_frsum[5]),
	    SWAB32(cg->cg_frsum[6]), SWAB32(cg->cg_frsum[7]));
	printk("  btotoff:      %u\n", SWAB32(cg->cg_btotoff));
	printk("  boff:         %u\n", SWAB32(cg->cg_boff));
	printk("  iuseoff:      %u\n", SWAB32(cg->cg_iusedoff));
	printk("  freeoff:      %u\n", SWAB32(cg->cg_freeoff));
	printk("  nextfreeoff:  %u\n", SWAB32(cg->cg_nextfreeoff));
	printk("  clustersumoff %u\n", SWAB32(cg->cg_u.cg_44.cg_clustersumoff));
	printk("  clusteroff    %u\n", SWAB32(cg->cg_u.cg_44.cg_clusteroff));
	printk("  nclusterblks  %u\n", SWAB32(cg->cg_u.cg_44.cg_nclusterblks));
	printk("\n");
}
#endif /* UFS_SUPER_DEBUG_MORE */

static struct super_operations ufs_super_ops;

static char error_buf[1024];

void ufs_error (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	va_list args;

	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH);
		sb->s_dirt = 1;
		sb->s_flags |= MS_RDONLY;
	}
	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	switch (sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_ONERROR) {
	case UFS_MOUNT_ONERROR_PANIC:
		panic ("UFS-fs panic (device %s): %s: %s\n", 
			kdevname(sb->s_dev), function, error_buf);

	case UFS_MOUNT_ONERROR_LOCK:
	case UFS_MOUNT_ONERROR_UMOUNT:
	case UFS_MOUNT_ONERROR_REPAIR:
		printk (KERN_CRIT "UFS-fs error (device %s): %s: %s\n",
			kdevname(sb->s_dev), function, error_buf);
	}		
}

void ufs_panic (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	va_list args;
	
	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	
	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_clean = UFS_FSBAD;
		ubh_mark_buffer_dirty(USPI_UBH);
		sb->s_dirt = 1;
	}
	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	/* this is to prevent panic from syncing this filesystem */
	if (sb->s_lock)
		sb->s_lock = 0;
	sb->s_flags |= MS_RDONLY;
	printk (KERN_CRIT "UFS-fs panic (device %s): %s: %s\n",
		kdevname(sb->s_dev), function, error_buf);
}

void ufs_warning (struct super_block * sb, const char * function,
	const char * fmt, ...)
{
	va_list args;

	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);
	printk (KERN_WARNING "UFS-fs warning (device %s): %s: %s\n",
		kdevname(sb->s_dev), function, error_buf);
}

static int ufs_parse_options (char * options, unsigned * mount_options)
{
	char * this_char;
	char * value;
	
	UFSD(("ENTER\n"))
	
	if (!options)
		return 1;
		
	for (this_char = strtok (options, ",");
	     this_char != NULL;
	     this_char = strtok (NULL, ",")) {
	     
		if ((value = strchr (this_char, '=')) != NULL)
			*value++ = 0;
		if (!strcmp (this_char, "ufstype")) {
			ufs_clear_opt (*mount_options, UFSTYPE);
			if (!strcmp (value, "old"))
				ufs_set_opt (*mount_options, UFSTYPE_OLD);
			else if (!strcmp (value, "sun"))
				ufs_set_opt (*mount_options, UFSTYPE_SUN);
			else if (!strcmp (value, "44bsd"))
				ufs_set_opt (*mount_options, UFSTYPE_44BSD);
			else if (!strcmp (value, "nextstep"))
				ufs_set_opt (*mount_options, UFSTYPE_NEXTSTEP);
			else if (!strcmp (value, "nextstep-cd"))
				ufs_set_opt (*mount_options, UFSTYPE_NEXTSTEP_CD);
			else if (!strcmp (value, "openstep"))
				ufs_set_opt (*mount_options, UFSTYPE_OPENSTEP);
			else if (!strcmp (value, "sunx86"))
				ufs_set_opt (*mount_options, UFSTYPE_SUNx86);
			else if (!strcmp (value, "hp"))
				ufs_set_opt (*mount_options, UFSTYPE_HP);
			else {
				printk ("UFS-fs: Invalid type option: %s\n", value);
				return 0;
			}
		}
		else if (!strcmp (this_char, "onerror")) {
			ufs_clear_opt (*mount_options, ONERROR);
			if (!strcmp (value, "panic"))
				ufs_set_opt (*mount_options, ONERROR_PANIC);
			else if (!strcmp (value, "lock"))
				ufs_set_opt (*mount_options, ONERROR_LOCK);
			else if (!strcmp (value, "umount"))
				ufs_set_opt (*mount_options, ONERROR_UMOUNT);
			else if (!strcmp (value, "repair")) {
				printk("UFS-fs: Unable to do repair on error, "
					"will lock lock instead \n");
				ufs_set_opt (*mount_options, ONERROR_REPAIR);
			}
			else {
				printk ("UFS-fs: Invalid action onerror: %s\n", value);
				return 0;
			}
		}
		else {
			printk("UFS-fs: Invalid option: %s\n", this_char);
			return 0;
		}
	}
	return 1;
}

/*
 * Read on-disk structures associated with cylinder groups
 */
int ufs_read_cylinder_structures (struct super_block * sb) {
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * ubh;
	unsigned char * base, * space;
	unsigned size, blks, i;
	unsigned swab;
	
	UFSD(("ENTER\n"))
	
	uspi = sb->u.ufs_sb.s_uspi;
	swab = sb->u.ufs_sb.s_swab;
	
	/*
	 * Read cs structures from (usually) first data block
	 * on the device. 
	 */
	size = uspi->s_cssize;
	blks = (size + uspi->s_fsize - 1) >> uspi->s_fshift;
	base = space = kmalloc(size, GFP_KERNEL);
	if (!base)
		goto failed; 
	for (i = 0; i < blks; i += uspi->s_fpb) {
		size = uspi->s_bsize;
		if (i + uspi->s_fpb > blks)
			size = (blks - i) * uspi->s_fsize;
		ubh = ubh_bread(sb->s_dev, uspi->s_csaddr + i, size);
		if (!ubh)
			goto failed;
		ubh_ubhcpymem (space, ubh, size);
		sb->u.ufs_sb.s_csp[ufs_fragstoblks(i)] = (struct ufs_csum *)space;
		space += size;
		ubh_brelse (ubh);
		ubh = NULL;
	}

	/*
	 * Read cylinder group (we read only first fragment from block
	 * at this time) and prepare internal data structures for cg caching.
	 */
	if (!(sb->u.ufs_sb.s_ucg = kmalloc (sizeof(struct buffer_head *) * uspi->s_ncg, GFP_KERNEL)))
		goto failed;
	for (i = 0; i < uspi->s_ncg; i++) 
		sb->u.ufs_sb.s_ucg[i] = NULL;
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		sb->u.ufs_sb.s_ucpi[i] = NULL;
		sb->u.ufs_sb.s_cgno[i] = UFS_CGNO_EMPTY;
	}
	for (i = 0; i < uspi->s_ncg; i++) {
		UFSD(("read cg %u\n", i))
		if (!(sb->u.ufs_sb.s_ucg[i] = bread (sb->s_dev, ufs_cgcmin(i), sb->s_blocksize)))
			goto failed;
		if (!ufs_cg_chkmagic ((struct ufs_cylinder_group *) sb->u.ufs_sb.s_ucg[i]->b_data))
			goto failed;
#ifdef UFS_SUPER_DEBUG_MORE
		ufs_print_cylinder_stuff((struct ufs_cylinder_group *) sb->u.ufs_sb.s_ucg[i]->b_data, swab);
#endif
	}
	for (i = 0; i < UFS_MAX_GROUP_LOADED; i++) {
		if (!(sb->u.ufs_sb.s_ucpi[i] = kmalloc (sizeof(struct ufs_cg_private_info), GFP_KERNEL)))
			goto failed;
		sb->u.ufs_sb.s_cgno[i] = UFS_CGNO_EMPTY;
	}
	sb->u.ufs_sb.s_cg_loaded = 0;
	UFSD(("EXIT\n"))
	return 1;

failed:
	if (base) kfree (base);
	if (sb->u.ufs_sb.s_ucg) {
		for (i = 0; i < uspi->s_ncg; i++)
			if (sb->u.ufs_sb.s_ucg[i]) brelse (sb->u.ufs_sb.s_ucg[i]);
		kfree (sb->u.ufs_sb.s_ucg);
		for (i = 0; i < UFS_MAX_GROUP_LOADED; i++)
			if (sb->u.ufs_sb.s_ucpi[i]) kfree (sb->u.ufs_sb.s_ucpi[i]);
	}
	UFSD(("EXIT (FAILED)\n"))
	return 0;
}

/*
 * Put on-disk structures associated with cylinder groups and 
 * write them back to disk
 */
void ufs_put_cylinder_structures (struct super_block * sb) {
	struct ufs_sb_private_info * uspi;
	struct ufs_buffer_head * ubh;
	unsigned char * base, * space;
	unsigned blks, size, i;
	
	UFSD(("ENTER\n"))
	
	uspi = sb->u.ufs_sb.s_uspi;

	size = uspi->s_cssize;
	blks = (size + uspi->s_fsize - 1) >> uspi->s_fshift;
	base = space = (char*) sb->u.ufs_sb.s_csp[0];
	for (i = 0; i < blks; i += uspi->s_fpb) {
		size = uspi->s_bsize;
		if (i + uspi->s_fpb > blks)
			size = (blks - i) * uspi->s_fsize;
		ubh = ubh_bread (sb->s_dev, uspi->s_csaddr + i, size);
		ubh_memcpyubh (ubh, space, size);
		space += size;
		ubh_mark_buffer_uptodate (ubh, 1);
		ubh_mark_buffer_dirty (ubh);
		ubh_brelse (ubh);
	}
	for (i = 0; i < sb->u.ufs_sb.s_cg_loaded; i++) {
		ufs_put_cylinder (sb, i);
		kfree (sb->u.ufs_sb.s_ucpi[i]);
	}
	for (; i < UFS_MAX_GROUP_LOADED; i++) 
		kfree (sb->u.ufs_sb.s_ucpi[i]);
	for (i = 0; i < uspi->s_ncg; i++) 
		brelse (sb->u.ufs_sb.s_ucg[i]);
	kfree (sb->u.ufs_sb.s_ucg);
	kfree (base);
	UFSD(("EXIT\n"))
}

struct super_block * ufs_read_super (struct super_block * sb, void * data,
	int silent)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_second * usb2;
	struct ufs_super_block_third * usb3;
	struct ufs_buffer_head * ubh;	
	unsigned block_size, super_block_size;
	unsigned flags, swab;

	uspi = NULL;
	ubh = NULL;
	flags = 0;
	swab = 0;
	
	UFSD(("ENTER\n"))
		
	UFSD(("flag %u\n", (int)(sb->s_flags & MS_RDONLY)))
	
#ifndef CONFIG_UFS_FS_WRITE
	if (!(sb->s_flags & MS_RDONLY)) {
		printk("ufs was compiled with read-only support, "
		"can't be mounted as read-write\n");
		goto failed;
	}
#endif
	/*
	 * Set default mount options
	 * Parse mount options
	 */
	sb->u.ufs_sb.s_mount_opt = 0;
	ufs_set_opt (sb->u.ufs_sb.s_mount_opt, ONERROR_LOCK);
	if (!ufs_parse_options ((char *) data, &sb->u.ufs_sb.s_mount_opt)) {
		printk("wrong mount options\n");
		goto failed;
	}
	if (!(sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE)) {
		printk("You didn't specify the type of your ufs filesystem\n\n"
		"mount -t ufs -o ufstype="
		"sun|sunx86|44bsd|old|hp|nextstep|netxstep-cd|openstep ...\n\n"
		">>>WARNING<<< Wrong ufstype may corrupt your filesystem, "
		"default is ufstype=old\n");
		ufs_set_opt (sb->u.ufs_sb.s_mount_opt, UFSTYPE_OLD);
	}

	sb->u.ufs_sb.s_uspi = uspi =
		kmalloc (sizeof(struct ufs_sb_private_info), GFP_KERNEL);
	if (!uspi)
		goto failed;

	switch (sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE) {
	case UFS_MOUNT_UFSTYPE_44BSD:
		UFSD(("ufstype=44bsd\n"))
		uspi->s_fsize = block_size = 512;
		uspi->s_fmask = ~(512 - 1);
		uspi->s_fshift = 9;
		uspi->s_sbsize = super_block_size = 1536;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
		break;
		
	case UFS_MOUNT_UFSTYPE_SUN:
		UFSD(("ufstype=sun\n"))
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_maxsymlinklen = 56;
		flags |= UFS_DE_OLD | UFS_UID_EFT | UFS_ST_SUN | UFS_CG_SUN;
		break;

	case UFS_MOUNT_UFSTYPE_SUNx86:
		UFSD(("ufstype=sunx86\n"))
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		uspi->s_maxsymlinklen = 56;
		flags |= UFS_DE_OLD | UFS_UID_EFT | UFS_ST_SUNx86 | UFS_CG_SUN;
		break;

	case UFS_MOUNT_UFSTYPE_OLD:
		UFSD(("ufstype=old\n"))
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!(sb->s_flags & MS_RDONLY)) {
			printk(KERN_INFO "ufstype=old is supported read-only\n"); 
			sb->s_flags |= MS_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_NEXTSTEP:
		UFSD(("ufstype=nextstep\n"))
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!(sb->s_flags & MS_RDONLY)) {
			printk(KERN_INFO "ufstype=nextstep is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_NEXTSTEP_CD:
		UFSD(("ufstype=nextstep-cd\n"))
		uspi->s_fsize = block_size = 2048;
		uspi->s_fmask = ~(2048 - 1);
		uspi->s_fshift = 11;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!(sb->s_flags & MS_RDONLY)) {
			printk(KERN_INFO "ufstype=nextstep-cd is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_OPENSTEP:
		UFSD(("ufstype=openstep\n"))
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_44BSD | UFS_UID_44BSD | UFS_ST_44BSD | UFS_CG_44BSD;
		if (!(sb->s_flags & MS_RDONLY)) {
			printk(KERN_INFO "ufstype=openstep is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
		}
		break;
	
	case UFS_MOUNT_UFSTYPE_HP:
		UFSD(("ufstype=hp\n"))
		uspi->s_fsize = block_size = 1024;
		uspi->s_fmask = ~(1024 - 1);
		uspi->s_fshift = 10;
		uspi->s_sbsize = super_block_size = 2048;
		uspi->s_sbbase = 0;
		flags |= UFS_DE_OLD | UFS_UID_OLD | UFS_ST_OLD | UFS_CG_OLD;
		if (!(sb->s_flags & MS_RDONLY)) {
			printk(KERN_INFO "ufstype=hp is supported read-only\n");
			sb->s_flags |= MS_RDONLY;
 		}
 		break;
	default:
		printk("unknown ufstype\n");
		goto failed;
	}
	
again:	
	set_blocksize (sb->s_dev, block_size);

	/*
	 * read ufs super block from device
	 */
	ubh = ubh_bread_uspi (uspi, sb->s_dev, uspi->s_sbbase + UFS_SBLOCK/block_size, super_block_size);
	if (!ubh) 
		goto failed;
	
	usb1 = ubh_get_usb_first(USPI_UBH);
	usb2 = ubh_get_usb_second(USPI_UBH);
	usb3 = ubh_get_usb_third(USPI_UBH);

	/*
	 * Check ufs magic number
	 */
#if defined(__LITTLE_ENDIAN) || defined(__BIG_ENDIAN) /* sane bytesex */
	switch (usb3->fs_magic) {
		case UFS_MAGIC:
	        case UFS_MAGIC_LFN:
	        case UFS_MAGIC_FEA:
	        case UFS_MAGIC_4GB:
			swab = UFS_NATIVE_ENDIAN;
			goto magic_found;
		case UFS_CIGAM:
	        case UFS_CIGAM_LFN:
	        case UFS_CIGAM_FEA:
	        case UFS_CIGAM_4GB:
			swab = UFS_SWABBED_ENDIAN;
			goto magic_found;
	}
#else /* bytesex perversion */
	switch (le32_to_cpup(&usb3->fs_magic)) {
		case UFS_MAGIC:
		case UFS_MAGIC_LFN:
	        case UFS_MAGIC_FEA:
	        case UFS_MAGIC_4GB:
			swab = UFS_LITTLE_ENDIAN;
			goto magic_found;
		case UFS_CIGAM:
		case UFS_CIGAM_LFN:
	        case UFS_CIGAM_FEA:
	        case UFS_CIGAM_4GB:
			swab = UFS_BIG_ENDIAN;
			goto magic_found;
	}
#endif

	if ((((sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_NEXTSTEP) 
	  || ((sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_NEXTSTEP_CD) 
	  || ((sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE) == UFS_MOUNT_UFSTYPE_OPENSTEP)) 
	  && uspi->s_sbbase < 256) {
		ubh_brelse_uspi(uspi);
		ubh = NULL;
		uspi->s_sbbase += 8;
		goto again;
	}
	printk("ufs_read_super: bad magic number\n");
	goto failed;

magic_found:
	/*
	 * Check block and fragment sizes
	 */
	uspi->s_bsize = SWAB32(usb1->fs_bsize);
	uspi->s_fsize = SWAB32(usb1->fs_fsize);
	uspi->s_sbsize = SWAB32(usb1->fs_sbsize);
	uspi->s_fmask = SWAB32(usb1->fs_fmask);
	uspi->s_fshift = SWAB32(usb1->fs_fshift);

	if (uspi->s_bsize != 4096 && uspi->s_bsize != 8192 
	  && uspi->s_bsize != 32768) {
		printk("ufs_read_super: fs_bsize %u != {4096, 8192, 32768}\n", uspi->s_bsize);
		goto failed;
	}
	if (uspi->s_fsize != 512 && uspi->s_fsize != 1024 
	  && uspi->s_fsize != 2048 && uspi->s_fsize != 4096) {
		printk("ufs_read_super: fs_fsize %u != {512, 1024, 2048. 4096}\n", uspi->s_fsize);
		goto failed;
	}
	if (uspi->s_fsize != block_size || uspi->s_sbsize != super_block_size) {
		ubh_brelse_uspi(uspi);
		ubh = NULL;
		block_size = uspi->s_fsize;
		super_block_size = uspi->s_sbsize;
		UFSD(("another value of block_size or super_block_size %u, %u\n", block_size, super_block_size))
		goto again;
	}

#ifdef UFS_SUPER_DEBUG_MORE
	ufs_print_super_stuff (usb1, usb2, usb3, swab);
#endif

	/*
	 * Check, if file system was correctly unmounted.
	 * If not, make it read only.
	 */
	if (((flags & UFS_ST_MASK) == UFS_ST_44BSD) ||
	  ((flags & UFS_ST_MASK) == UFS_ST_OLD) ||
	  (((flags & UFS_ST_MASK) == UFS_ST_SUN || 
	  (flags & UFS_ST_MASK) == UFS_ST_SUNx86) && 
	  (ufs_get_fs_state(usb1, usb3) == (UFS_FSOK - SWAB32(usb1->fs_time))))) {
		switch(usb1->fs_clean) {
		case UFS_FSCLEAN:
			UFSD(("fs is clean\n"))
			break;
		case UFS_FSSTABLE:
			UFSD(("fs is stable\n"))
			break;
		case UFS_FSOSF1:
			UFSD(("fs is DEC OSF/1\n"))
			break;
		case UFS_FSACTIVE:
			printk("ufs_read_super: fs is active\n");
			sb->s_flags |= MS_RDONLY;
			break;
		case UFS_FSBAD:
			printk("ufs_read_super: fs is bad\n");
			sb->s_flags |= MS_RDONLY;
			break;
		default:
			printk("ufs_read_super: can't grok fs_clean 0x%x\n", usb1->fs_clean);
			sb->s_flags |= MS_RDONLY;
			break;
		}
	}
	else {
		printk("ufs_read_super: fs needs fsck\n");
		sb->s_flags |= MS_RDONLY;
	}

	/*
	 * Read ufs_super_block into internal data structures
	 */
	sb->s_blocksize =  SWAB32(usb1->fs_fsize);
	sb->s_blocksize_bits = SWAB32(usb1->fs_fshift);
	sb->s_op = &ufs_super_ops;
	sb->dq_op = NULL; /***/
	sb->s_magic = SWAB32(usb3->fs_magic);

	uspi->s_sblkno = SWAB32(usb1->fs_sblkno);
	uspi->s_cblkno = SWAB32(usb1->fs_cblkno);
	uspi->s_iblkno = SWAB32(usb1->fs_iblkno);
	uspi->s_dblkno = SWAB32(usb1->fs_dblkno);
	uspi->s_cgoffset = SWAB32(usb1->fs_cgoffset);
	uspi->s_cgmask = SWAB32(usb1->fs_cgmask);
	uspi->s_size = SWAB32(usb1->fs_size);
	uspi->s_dsize = SWAB32(usb1->fs_dsize);
	uspi->s_ncg = SWAB32(usb1->fs_ncg);
	/* s_bsize already set */
	/* s_fsize already set */
	uspi->s_fpb = SWAB32(usb1->fs_frag);
	uspi->s_minfree = SWAB32(usb1->fs_minfree);
	uspi->s_bmask = SWAB32(usb1->fs_bmask);
	uspi->s_fmask = SWAB32(usb1->fs_fmask);
	uspi->s_bshift = SWAB32(usb1->fs_bshift);
	uspi->s_fshift = SWAB32(usb1->fs_fshift);
	uspi->s_fpbshift = SWAB32(usb1->fs_fragshift);
	uspi->s_fsbtodb = SWAB32(usb1->fs_fsbtodb);
	/* s_sbsize already set */
	uspi->s_csmask = SWAB32(usb1->fs_csmask);
	uspi->s_csshift = SWAB32(usb1->fs_csshift);
	uspi->s_nindir = SWAB32(usb1->fs_nindir);
	uspi->s_inopb = SWAB32(usb1->fs_inopb);
	uspi->s_nspf = SWAB32(usb1->fs_nspf);
	uspi->s_npsect = ufs_get_fs_npsect(usb1, usb3);
	uspi->s_interleave = SWAB32(usb1->fs_interleave);
	uspi->s_trackskew = SWAB32(usb1->fs_trackskew);
	uspi->s_csaddr = SWAB32(usb1->fs_csaddr);
	uspi->s_cssize = SWAB32(usb1->fs_cssize);
	uspi->s_cgsize = SWAB32(usb1->fs_cgsize);
	uspi->s_ntrak = SWAB32(usb1->fs_ntrak);
	uspi->s_nsect = SWAB32(usb1->fs_nsect);
	uspi->s_spc = SWAB32(usb1->fs_spc);
	uspi->s_ipg = SWAB32(usb1->fs_ipg);
	uspi->s_fpg = SWAB32(usb1->fs_fpg);
	uspi->s_cpc = SWAB32(usb2->fs_cpc);
	uspi->s_contigsumsize = SWAB32(usb3->fs_u2.fs_44.fs_contigsumsize);
	uspi->s_qbmask = ufs_get_fs_qbmask(usb3);
	uspi->s_qfmask = ufs_get_fs_qfmask(usb3);
	uspi->s_postblformat = SWAB32(usb3->fs_postblformat);
	uspi->s_nrpos = SWAB32(usb3->fs_nrpos);
	uspi->s_postbloff = SWAB32(usb3->fs_postbloff);
	uspi->s_rotbloff = SWAB32(usb3->fs_rotbloff);

	/*
	 * Compute another frequently used values
	 */
	uspi->s_fpbmask = uspi->s_fpb - 1;
	uspi->s_apbshift = uspi->s_bshift - 2;
	uspi->s_2apbshift = uspi->s_apbshift * 2;
	uspi->s_3apbshift = uspi->s_apbshift * 3;
	uspi->s_apb = 1 << uspi->s_apbshift;
	uspi->s_2apb = 1 << uspi->s_2apbshift;
	uspi->s_3apb = 1 << uspi->s_3apbshift;
	uspi->s_apbmask = uspi->s_apb - 1;
	uspi->s_nspfshift = uspi->s_fshift - UFS_SECTOR_BITS;
	uspi->s_nspb = uspi->s_nspf << uspi->s_fpbshift;
	uspi->s_inopf = uspi->s_inopb >> uspi->s_fpbshift;
	uspi->s_bpf = uspi->s_fsize << 3;
	uspi->s_bpfshift = uspi->s_fshift + 3;
	uspi->s_bpfmask = uspi->s_bpf - 1;
	if ((sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE) ==
	    UFS_MOUNT_UFSTYPE_44BSD)
		uspi->s_maxsymlinklen =
		    SWAB32(usb3->fs_u2.fs_44.fs_maxsymlinklen);
	
	sb->u.ufs_sb.s_flags = flags;
	sb->u.ufs_sb.s_swab = swab;
	 	                                                          
	sb->s_root = d_alloc_root(iget(sb, UFS_ROOTINO));


	/*
	 * Read cylinder group structures
	 */
	if (!(sb->s_flags & MS_RDONLY))
		if (!ufs_read_cylinder_structures(sb))
			goto failed;

	UFSD(("EXIT\n"))
	return(sb);

failed:
	if (ubh) ubh_brelse_uspi (uspi);
	if (uspi) kfree (uspi);
	UFSD(("EXIT (FAILED)\n"))
	return(NULL);
}

void ufs_write_super (struct super_block * sb) {
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned flags, swab;

	UFSD(("ENTER\n"))
	swab = sb->u.ufs_sb.s_swab;
	flags = sb->u.ufs_sb.s_flags;
	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first(USPI_UBH);
	usb3 = ubh_get_usb_third(USPI_UBH);

	if (!(sb->s_flags & MS_RDONLY)) {
		usb1->fs_time = SWAB32(CURRENT_TIME);
		if ((flags & UFS_ST_MASK) == UFS_ST_SUN 
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNx86)
			ufs_set_fs_state(usb1, usb3, UFS_FSOK - SWAB32(usb1->fs_time));
		ubh_mark_buffer_dirty (USPI_UBH);
	}
	sb->s_dirt = 0;
	UFSD(("EXIT\n"))
}

void ufs_put_super (struct super_block * sb)
{
	struct ufs_sb_private_info * uspi;
	unsigned swab;
		
	UFSD(("ENTER\n"))

	uspi = sb->u.ufs_sb.s_uspi;
	swab = sb->u.ufs_sb.s_swab;

	if (!(sb->s_flags & MS_RDONLY))
		ufs_put_cylinder_structures (sb);
	
	ubh_brelse_uspi (uspi);
	kfree (sb->u.ufs_sb.s_uspi);
	return;
}


int ufs_remount (struct super_block * sb, int * mount_flags, char * data)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	struct ufs_super_block_third * usb3;
	unsigned new_mount_opt, ufstype;
	unsigned flags, swab;
	
	uspi = sb->u.ufs_sb.s_uspi;
	flags = sb->u.ufs_sb.s_flags;
	swab = sb->u.ufs_sb.s_swab;
	usb1 = ubh_get_usb_first(USPI_UBH);
	usb3 = ubh_get_usb_third(USPI_UBH);
	
	/*
	 * Allow the "check" option to be passed as a remount option.
	 * It is not possible to change ufstype option during remount
	 */
	ufstype = sb->u.ufs_sb.s_mount_opt & UFS_MOUNT_UFSTYPE;
	new_mount_opt = 0;
	ufs_set_opt (new_mount_opt, ONERROR_LOCK);
	if (!ufs_parse_options (data, &new_mount_opt))
		return -EINVAL;
	if (!(new_mount_opt & UFS_MOUNT_UFSTYPE)) {
		new_mount_opt |= ufstype;
	}
	else if ((new_mount_opt & UFS_MOUNT_UFSTYPE) != ufstype) {
		printk("ufstype can't be changed during remount\n");
		return -EINVAL;
	}

	if ((*mount_flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY)) {
		sb->u.ufs_sb.s_mount_opt = new_mount_opt;
		return 0;
	}
	
	/*
	 * fs was mouted as rw, remounting ro
	 */
	if (*mount_flags & MS_RDONLY) {
		ufs_put_cylinder_structures(sb);
		usb1->fs_time = SWAB32(CURRENT_TIME);
		if ((flags & UFS_ST_MASK) == UFS_ST_SUN
		  || (flags & UFS_ST_MASK) == UFS_ST_SUNx86) 
			ufs_set_fs_state(usb1, usb3, UFS_FSOK - SWAB32(usb1->fs_time));
		ubh_mark_buffer_dirty (USPI_UBH);
		sb->s_dirt = 0;
		sb->s_flags |= MS_RDONLY;
	}
	/*
	 * fs was mounted as ro, remounting rw
	 */
	else {
#ifndef CONFIG_UFS_FS_WRITE
		printk("ufs was compiled with read-only support, "
		"can't be mounted as read-write\n");
		return -EINVAL;
#else
		if (ufstype != UFS_MOUNT_UFSTYPE_SUN && 
		    ufstype != UFS_MOUNT_UFSTYPE_44BSD &&
		    ufstype != UFS_MOUNT_UFSTYPE_SUNx86) {
			printk("this ufstype is read-only supported\n");
			return -EINVAL;
		}
		if (!ufs_read_cylinder_structures (sb)) {
			printk("failed during remounting\n");
			return -EPERM;
		}
		sb->s_flags &= ~MS_RDONLY;
#endif
	}
	sb->u.ufs_sb.s_mount_opt = new_mount_opt;
	return 0;
}

int ufs_statfs (struct super_block * sb, struct statfs * buf)
{
	struct ufs_sb_private_info * uspi;
	struct ufs_super_block_first * usb1;
	unsigned swab;

	swab = sb->u.ufs_sb.s_swab;
	uspi = sb->u.ufs_sb.s_uspi;
	usb1 = ubh_get_usb_first (USPI_UBH);
	
	buf->f_type = UFS_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = uspi->s_dsize;
	buf->f_bfree = ufs_blkstofrags(SWAB32(usb1->fs_cstotal.cs_nbfree)) +
		SWAB32(usb1->fs_cstotal.cs_nffree);
	buf->f_bavail = (buf->f_bfree > ((buf->f_blocks / 100) * uspi->s_minfree))
		? (buf->f_bfree - ((buf->f_blocks / 100) * uspi->s_minfree)) : 0;
	buf->f_files = uspi->s_ncg * uspi->s_ipg;
	buf->f_ffree = SWAB32(usb1->fs_cstotal.cs_nifree);
	buf->f_namelen = UFS_MAXNAMLEN;
	return 0;
}

static struct super_operations ufs_super_ops = {
	read_inode:	ufs_read_inode,
	write_inode:	ufs_write_inode,
	delete_inode:	ufs_delete_inode,
	put_super:	ufs_put_super,
	write_super:	ufs_write_super,
	statfs:		ufs_statfs,
	remount_fs:	ufs_remount,
};

static DECLARE_FSTYPE_DEV(ufs_fs_type, "ufs", ufs_read_super);

static int __init init_ufs_fs(void)
{
	return register_filesystem(&ufs_fs_type);
}

static void __exit exit_ufs_fs(void)
{
	unregister_filesystem(&ufs_fs_type);
}

EXPORT_NO_SYMBOLS;

module_init(init_ufs_fs)
module_exit(exit_ufs_fs)
