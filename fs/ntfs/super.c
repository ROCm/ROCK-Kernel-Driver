/*
 * super.c
 *
 * Copyright (C) 1995-1997, 1999 Martin von Löwis
 * Copyright (C) 1996-1997 Régis Duchesne
 * Copyright (C) 1999 Steve Dodd
 * Copyright (C) 2000-2001 Anton Altparmakov (AIA)
 */

#include <linux/ntfs_fs.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include "ntfstypes.h"
#include "struct.h"
#include "super.h"
#include "macros.h"
#include "inode.h"
#include "support.h"
#include "util.h"
#include <linux/smp_lock.h>

/* All important structures in NTFS use 2 consistency checks:
 * . a magic structure identifier (FILE, INDX, RSTR, RCRD...)
 * . a fixup technique : the last word of each sector (called a fixup) of a
 *   structure's record should end with the word at offset <n> of the first
 *   sector, and if it is the case, must be replaced with the words following
 *   <n>. The value of <n> and the number of fixups is taken from the fields
 *   at the offsets 4 and 6. Note that the sector size is defined as
 *   NTFS_SECTOR_SIZE and not as the hardware sector size (this is concordant
 *   with what the Windows NTFS driver does).
 *
 * This function perform these 2 checks, and _fails_ if:
 * . the input size is invalid
 * . the fixup header is invalid
 * . the size does not match the number of sectors
 * . the magic identifier is wrong
 * . a fixup is invalid
 */
int ntfs_fixup_record(char *record, char *magic, int size)
{
	int start, count, offset;
	ntfs_u16 fixup;

	if (!IS_MAGIC(record, magic))
		return 0;
	start = NTFS_GETU16(record + 4);
	count = NTFS_GETU16(record + 6) - 1;
	if (size & (NTFS_SECTOR_SIZE - 1) || start & 1 ||
			start + count * 2 > size || size >> 9 != count) {
		if (size <= 0)
			printk(KERN_ERR "NTFS: BUG: ntfs_fixup_record() got "
					"zero size! Please report this to "
					"linux-ntfs-dev@lists.sf.net\n");
		return 0;
	}
	fixup = NTFS_GETU16(record + start);
	start += 2;
	offset = NTFS_SECTOR_SIZE - 2;
	while (count--) {
		if (NTFS_GETU16(record + offset) != fixup)
			return 0;
		NTFS_PUTU16(record + offset, NTFS_GETU16(record + start));
		start += 2;
		offset += NTFS_SECTOR_SIZE;
	}
	return 1;
}

/*
 * Get vital informations about the ntfs partition from the boot sector.
 * Return 0 on success or -1 on error.
 */
int ntfs_init_volume(ntfs_volume *vol, char *boot)
{
	int sectors_per_cluster_bits;
	__s64 ll;

	/* System defined default values, in case we don't load $AttrDef. */
	vol->at_standard_information = 0x10;
	vol->at_attribute_list = 0x20;
	vol->at_file_name = 0x30;
	vol->at_volume_version = 0x40;
	vol->at_security_descriptor = 0x50;
	vol->at_volume_name = 0x60;
	vol->at_volume_information = 0x70;
	vol->at_data = 0x80;
	vol->at_index_root = 0x90;
	vol->at_index_allocation = 0xA0;
	vol->at_bitmap = 0xB0;
	vol->at_symlink = 0xC0;
	/* Sector size. */
	vol->sector_size = NTFS_GETU16(boot + 0xB);
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->sector_size = 0x%x\n",
				vol->sector_size);
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: sectors_per_cluster = "
				"0x%x\n", NTFS_GETU8(boot + 0xD));
	sectors_per_cluster_bits = ffs(NTFS_GETU8(boot + 0xD)) - 1;
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: sectors_per_cluster_bits "
				"= 0x%x\n", sectors_per_cluster_bits); 
	vol->mft_clusters_per_record = NTFS_GETS8(boot + 0x40);
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->mft_clusters_per_record"
				" = 0x%x\n", vol->mft_clusters_per_record); 
	vol->index_clusters_per_record = NTFS_GETS8(boot + 0x44);
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: "
				"vol->index_clusters_per_record = 0x%x\n",
				vol->index_clusters_per_record); 
	vol->cluster_size = vol->sector_size << sectors_per_cluster_bits;
	vol->cluster_size_bits = ffs(vol->cluster_size) - 1;
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->cluster_size = 0x%x\n",
				vol->cluster_size); 
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->cluster_size_bits = "
				"0x%x\n", vol->cluster_size_bits); 
	if (vol->mft_clusters_per_record > 0)
		vol->mft_record_size = vol->cluster_size <<
				(ffs(vol->mft_clusters_per_record) - 1);
	else
		/*
		 * When mft_record_size < cluster_size, mft_clusters_per_record
		 * = -log2(mft_record_size) bytes. mft_record_size normaly is
		 * 1024 bytes, which is encoded as 0xF6 (-10 in decimal).
		 */
		vol->mft_record_size = 1 << -vol->mft_clusters_per_record;
	vol->mft_record_size_bits = ffs(vol->mft_record_size) - 1;
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->mft_record_size = 0x%x"
				"\n", vol->mft_record_size); 
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->mft_record_size_bits = "
				"0x%x\n", vol->mft_record_size_bits); 
	if (vol->index_clusters_per_record > 0)
		vol->index_record_size = vol->cluster_size <<
				(ffs(vol->index_clusters_per_record) - 1);
	else
		/*
		 * When index_record_size < cluster_size,
		 * index_clusters_per_record = -log2(index_record_size) bytes.
		 * index_record_size normaly equals 4096 bytes, which is
		 * encoded as 0xF4 (-12 in decimal).
		 */
		vol->index_record_size = 1 << -vol->index_clusters_per_record;
	vol->index_record_size_bits = ffs(vol->index_record_size) - 1;
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->index_record_size = "
				"0x%x\n", vol->index_record_size);
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->index_record_size_bits "
				"= 0x%x\n", vol->index_record_size_bits);
	/*
	 * Get the size of the volume in clusters (ofs 0x28 is nr_sectors) and
	 * check for 64-bit-ness. Windows currently only uses 32 bits to save
	 * the clusters so we do the same as it is much faster on 32-bit CPUs.
	 */
	ll = NTFS_GETS64(boot + 0x28) >> sectors_per_cluster_bits;
	if (ll >= (__s64)1 << 31) {
		ntfs_error("Cannot handle 64-bit clusters. Please inform "
				"linux-ntfs-dev@lists.sf.net that you got this "
				"error.\n");
		return -1;
	}
	vol->nr_clusters = (ntfs_cluster_t)ll;
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->nr_clusters = 0x%x\n",
			vol->nr_clusters);
	/*
	 * Determine MFT zone size. FIXME: Need to take into consideration
	 * where the mft zone starts (at vol->mft_lcn) as vol->mft_zone_end
	 * needs to be relative to that.
	 */
	vol->mft_zone_end = vol->nr_clusters;
	switch (vol->mft_zone_multiplier) {  /* % of volume size in clusters */
	case 4:
		vol->mft_zone_end = vol->mft_zone_end >> 1;	/* 50%   */
		break;
	case 3:
		vol->mft_zone_end = vol->mft_zone_end * 3 >> 3;	/* 37.5% */
		break;
	case 2:
		vol->mft_zone_end = vol->mft_zone_end >> 2;	/* 25%   */
		break;
	/* case 1: */
	default:
		vol->mft_zone_end = vol->mft_zone_end >> 3;	/* 12.5% */
		break;
	}
	ntfs_debug(DEBUG_FILE3, "ntfs_init_volume: vol->mft_zone_end = %x\n",
			vol->mft_zone_end);
	vol->mft_lcn = (ntfs_cluster_t)NTFS_GETS64(boot + 0x30);
	vol->mft_mirr_lcn = (ntfs_cluster_t)NTFS_GETS64(boot + 0x38);
	/* This will be initialized later. */
	vol->upcase = 0;
	vol->upcase_length = 0;
	vol->mft_ino = 0;
	return 0;
}

static void ntfs_init_upcase(ntfs_inode *upcase)
{
	ntfs_io io;
#define UPCASE_LENGTH  256
	upcase->vol->upcase = ntfs_malloc(UPCASE_LENGTH << 1);
	if (!upcase->vol->upcase)
		return;
	io.fn_put = ntfs_put;
	io.fn_get = 0;
	io.param = (char*)upcase->vol->upcase;
	io.size = UPCASE_LENGTH << 1;
	ntfs_read_attr(upcase, upcase->vol->at_data, 0, 0, &io);
	upcase->vol->upcase_length = io.size >> 1;
}

static int process_attrdef(ntfs_inode* attrdef, ntfs_u8* def)
{
	int type = NTFS_GETU32(def+0x80);
	int check_type = 0;
	ntfs_volume *vol = attrdef->vol;
	ntfs_u16* name = (ntfs_u16*)def;

	if (!type) {
		ntfs_debug(DEBUG_OTHER, "process_atrdef: finished processing "
							"and returning 1\n");
		return 1;
	}
	if (ntfs_ua_strncmp(name, "$STANDARD_INFORMATION", 64) == 0) {
		vol->at_standard_information = type;
		check_type = 0x10;
	} else if (ntfs_ua_strncmp(name, "$ATTRIBUTE_LIST", 64) == 0) {
		vol->at_attribute_list = type;
		check_type = 0x20;
	} else if (ntfs_ua_strncmp(name, "$FILE_NAME", 64) == 0) {
		vol->at_file_name = type;
		check_type = 0x30;
	} else if (ntfs_ua_strncmp(name, "$VOLUME_VERSION", 64) == 0) {
		vol->at_volume_version = type;
		check_type = 0x40;
	} else if (ntfs_ua_strncmp(name, "$SECURITY_DESCRIPTOR", 64) == 0) {
		vol->at_security_descriptor = type;
		check_type = 0x50;
	} else if (ntfs_ua_strncmp(name, "$VOLUME_NAME", 64) == 0) {
		vol->at_volume_name = type;
		check_type = 0x60;
	} else if (ntfs_ua_strncmp(name, "$VOLUME_INFORMATION", 64) == 0) {
		vol->at_volume_information = type;
		check_type = 0x70;
	} else if (ntfs_ua_strncmp(name, "$DATA", 64) == 0) {
		vol->at_data = type;
		check_type = 0x80;
	} else if (ntfs_ua_strncmp(name, "$INDEX_ROOT", 64) == 0) {
		vol->at_index_root = type;
		check_type = 0x90;
	} else if (ntfs_ua_strncmp(name, "$INDEX_ALLOCATION", 64) == 0) {
		vol->at_index_allocation = type;
		check_type = 0xA0;
	} else if (ntfs_ua_strncmp(name, "$BITMAP", 64) == 0) {
		vol->at_bitmap = type;
		check_type = 0xB0;
	} else if (ntfs_ua_strncmp(name, "$SYMBOLIC_LINK", 64) == 0 ||
		 ntfs_ua_strncmp(name, "$REPARSE_POINT", 64) == 0) {
		vol->at_symlink = type;
		check_type = 0xC0;
	}
	if (check_type && check_type != type) {
		ntfs_error("process_attrdef: unexpected type 0x%x for 0x%x\n",
							type, check_type);
		return -EINVAL;
	}
	ntfs_debug(DEBUG_OTHER, "process_attrdef: found %s attribute of type "
			"0x%x\n", check_type ? "known" : "unknown", type);
	return 0;
}

int ntfs_init_attrdef(ntfs_inode* attrdef)
{
	ntfs_u8 *buf;
	ntfs_io io;
	__s64 offset;
	unsigned i;
	int error;
	ntfs_attribute *data;

	ntfs_debug(DEBUG_BSD, "Entered ntfs_init_attrdef()\n");
	buf = ntfs_malloc(4050); /* 90*45 */
	if (!buf)
		return -ENOMEM;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	io.do_read = 1;
	offset = 0;
	data = ntfs_find_attr(attrdef, attrdef->vol->at_data, 0);
	ntfs_debug(DEBUG_BSD, "In ntfs_init_attrdef() after call to "
			"ntfs_find_attr.\n");
	if (!data) {
		ntfs_free(buf);
		return -EINVAL;
	}
	do {
		io.param = buf;
		io.size = 4050;
		ntfs_debug(DEBUG_BSD, "In ntfs_init_attrdef() going to call "
				"ntfs_readwrite_attr.\n");
		error = ntfs_readwrite_attr(attrdef, data, offset, &io);
		ntfs_debug(DEBUG_BSD, "In ntfs_init_attrdef() after call to "
				"ntfs_readwrite_attr.\n");
		for (i = 0; !error && i <= io.size - 0xA0; i += 0xA0) {
			ntfs_debug(DEBUG_BSD, "In ntfs_init_attrdef() going "
					"to call process_attrdef.\n");
			error = process_attrdef(attrdef, buf + i);
			ntfs_debug(DEBUG_BSD, "In ntfs_init_attrdef() after "
					"call to process_attrdef.\n");
		}
		offset += 4096;
	} while (!error && io.size);
	ntfs_debug(DEBUG_BSD, "Exiting ntfs_init_attrdef()\n");
	ntfs_free(buf);
	return error == 1 ? 0 : error;
}

/* ntfs_get_version will determine the NTFS version of the volume and will
 * return the version in a BCD format, with the MSB being the major version
 * number and the LSB the minor one. Otherwise return <0 on error.
 * Example: version 3.1 will be returned as 0x0301. This has the obvious
 * limitation of not coping with version numbers above 0x80 but that shouldn't
 * be a problem... */
int ntfs_get_version(ntfs_inode* volume)
{
	ntfs_attribute *volinfo;

	volinfo = ntfs_find_attr(volume, volume->vol->at_volume_information, 0);
	if (!volinfo) 
		return -EINVAL;
	if (!volinfo->resident) {
		ntfs_error("Volume information attribute is not resident!\n");
		return -EINVAL;
	}
	return ((ntfs_u8*)volinfo->d.data)[8] << 8 | 
	       ((ntfs_u8*)volinfo->d.data)[9];
}

int ntfs_load_special_files(ntfs_volume *vol)
{
	int error;
	ntfs_inode upcase, attrdef, volume;

	vol->mft_ino = (ntfs_inode*)ntfs_calloc(sizeof(ntfs_inode));
	vol->mftmirr = (ntfs_inode*)ntfs_calloc(sizeof(ntfs_inode));
	vol->bitmap = (ntfs_inode*)ntfs_calloc(sizeof(ntfs_inode));
	vol->ino_flags = 4 | 2 | 1;
	error = -ENOMEM;
	ntfs_debug(DEBUG_BSD, "Going to load MFT\n");
	if (!vol->mft_ino || (error = ntfs_init_inode(vol->mft_ino, vol,
			FILE_$Mft))) {
		ntfs_error("Problem loading MFT\n");
		return error;
	}
	ntfs_debug(DEBUG_BSD, "Going to load MIRR\n");
	if ((error = ntfs_init_inode(vol->mftmirr, vol, FILE_$MftMirr))) {
		ntfs_error("Problem %d loading MFTMirr\n", error);
		return error;
	}
	ntfs_debug(DEBUG_BSD, "Going to load BITMAP\n");
	if ((error = ntfs_init_inode(vol->bitmap, vol, FILE_$BitMap))) {
		ntfs_error("Problem loading Bitmap\n");
		return error;
	}
	ntfs_debug(DEBUG_BSD, "Going to load UPCASE\n");
	error = ntfs_init_inode(&upcase, vol, FILE_$UpCase);
	if (error)
		return error;
	ntfs_init_upcase(&upcase);
	ntfs_clear_inode(&upcase);
	ntfs_debug(DEBUG_BSD, "Going to load ATTRDEF\n");
	error = ntfs_init_inode(&attrdef, vol, FILE_$AttrDef);
	if (error)
		return error;
	error = ntfs_init_attrdef(&attrdef);
	ntfs_clear_inode(&attrdef);
	if (error)
		return error;

	/* Check for NTFS version and if Win2k version (ie. 3.0+) do not allow
	 * write access since the driver write support is broken. */
	ntfs_debug(DEBUG_BSD, "Going to load VOLUME\n");
	error = ntfs_init_inode(&volume, vol, FILE_$Volume);
	if (error)
		return error;
	if ((error = ntfs_get_version(&volume)) >= 0x0300 &&
	    !(NTFS_SB(vol)->s_flags & MS_RDONLY)) {
		NTFS_SB(vol)->s_flags |= MS_RDONLY;
		ntfs_error("Warning! NTFS volume version is Win2k+: Mounting "
			   "read-only\n");
	}
	ntfs_clear_inode(&volume);
	if (error < 0)
		return error;
	ntfs_debug(DEBUG_BSD, "NTFS volume is version %d.%d\n", error >> 8,
								error & 0xff);
	return 0;
}

int ntfs_release_volume(ntfs_volume *vol)
{
	if (((vol->ino_flags & 1) == 1) && vol->mft_ino) {
		ntfs_clear_inode(vol->mft_ino);
		ntfs_free(vol->mft_ino);
		vol->mft_ino = 0;
	}
	if (((vol->ino_flags & 2) == 2) && vol->mftmirr) {
		ntfs_clear_inode(vol->mftmirr);
		ntfs_free(vol->mftmirr);
		vol->mftmirr = 0;
	}
	if (((vol->ino_flags & 4) == 4) && vol->bitmap) {
		ntfs_clear_inode(vol->bitmap);
		ntfs_free(vol->bitmap);
		vol->bitmap = 0;
	}
	ntfs_free(vol->mft);
	ntfs_free(vol->upcase);
	return 0;
}

/*
 * Writes the volume size (units of clusters) into vol_size.
 * Returns 0 if successful or error.
 */
int ntfs_get_volumesize(ntfs_volume *vol, ntfs_s64 *vol_size)
{
	ntfs_io io;
	char *cluster0;

	if (!vol_size)
		return -EFAULT;
	cluster0 = ntfs_malloc(vol->cluster_size);
	if (!cluster0)
		return -ENOMEM;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	io.param = cluster0;
	io.do_read = 1;
	io.size = vol->cluster_size;
	ntfs_getput_clusters(vol, 0, 0, &io);
	*vol_size = NTFS_GETU64(cluster0 + 0x28) >>
					(ffs(NTFS_GETU8(cluster0 + 0xD)) - 1);
	ntfs_free(cluster0);
	return 0;
}

static int nc[16]={4,3,3,2,3,2,2,1,3,2,2,1,2,1,1,0};

int ntfs_get_free_cluster_count(ntfs_inode *bitmap)
{
	ntfs_io io;
	int offset, error, clusters;
	unsigned char *bits = ntfs_malloc(2048);
	if (!bits)
		return -ENOMEM;
	offset = clusters = 0;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	while (1) {
		register int i;
		io.param = bits;
		io.size = 2048;
		error = ntfs_read_attr(bitmap, bitmap->vol->at_data, 0, offset,
									&io);
		if (error || io.size == 0)
			break;
		/* I never thought I would do loop unrolling some day */
		for (i = 0; i < io.size - 8; ) {
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
			clusters+=nc[bits[i]>>4];clusters+=nc[bits[i++] & 0xF];
		}
		while (i < io.size) {
			clusters += nc[bits[i] >> 4];
			clusters += nc[bits[i++] & 0xF];
		}
		offset += io.size;
	}
	ntfs_free(bits);
	return clusters;
}

/*
 * Insert the fixups for the record. The number and location of the fixes
 * is obtained from the record header but we double check with @rec_size and
 * use that as the upper boundary, if necessary overwriting the count value in
 * the record header.
 *
 * We return 0 on success or -1 if fixup header indicated the beginning of the
 * update sequence array to be beyond the valid limit.
 */
int ntfs_insert_fixups(unsigned char *rec, int rec_size)
{
	int first;
	int count;
	int offset = -2;
	ntfs_u16 fix;
	
	first = NTFS_GETU16(rec + 4);
	count = (rec_size >> NTFS_SECTOR_BITS) + 1;
	if (first + count * 2 > NTFS_SECTOR_SIZE - 2) {
		printk(KERN_CRIT "NTFS: ntfs_insert_fixups() detected corrupt "
				"NTFS record update sequence array position. - "
				"Cannot hotfix.\n");
		return -1;
	}
	if (count != NTFS_GETU16(rec + 6)) {
		printk(KERN_ERR "NTFS: ntfs_insert_fixups() detected corrupt "
				"NTFS record update sequence array size. - "
				"Applying hotfix.\n");
		NTFS_PUTU16(rec + 6, count);
	}
	fix = (NTFS_GETU16(rec + first) + 1) & 0xffff;
	if (fix == 0xffff || !fix)
		fix = 1;
	NTFS_PUTU16(rec + first, fix);
	count--;
	while (count--) {
		first += 2;
		offset += NTFS_SECTOR_SIZE;
		NTFS_PUTU16(rec + first, NTFS_GETU16(rec + offset));
		NTFS_PUTU16(rec + offset, fix);
	}
	return 0;
}

/* Search the bitmap bits of l bytes for *cnt zero bits. Return the bit number
 * in *loc, which is initially set to the number of the first bit. Return the
 * largest block found in *cnt. Return 0 on success, -ENOSPC if all bits are
 * used. */
static int search_bits(unsigned char* bits, ntfs_cluster_t *loc, int *cnt,int l)
{
	unsigned char c = 0;
	int bc = 0;
	int bstart = 0, bstop = 0, found = 0;
	int start, stop = 0, in = 0;
	/* Special case searching for a single block. */
	if (*cnt == 1) {
		while (l && *bits == 0xFF) {
			bits++;
			*loc += 8;
			l--;
		}
		if (!l)
			return -ENOSPC;
		for (c = *bits; c & 1; c >>= 1)
			(*loc)++;
		return 0;
	}
	start = *loc;
	while (l || bc) {
		if (bc == 0) {
			c = *bits;
			if (l) {
				l--;
				bits++;
			}
			bc = 8;
		}
		if (in) {
			if ((c & 1) == 0)
				stop++;
			else { /* End of sequence of zeroes. */
				in = 0;
				if (!found || bstop-bstart < stop-start) {
					bstop = stop; bstart = start;
					found = 1;
					if (bstop - bstart > *cnt)
						break;
				}
				start = stop + 1;
			}
		} else {
			if (c & 1)
				start++;
			else { /* Start of sequence. */
				in = 1;
				stop = start + 1;
			}
		}
		bc--;
		c >>= 1;
	}
	if (in && (!found || bstop - bstart < stop - start)) {
		bstop = stop;
		bstart = start;
		found = 1;
	}
	if (!found)
		return -ENOSPC;
	*loc = bstart;
	if (*cnt > bstop - bstart)
		*cnt = bstop - bstart;
	return 0;
}

int ntfs_set_bitrange(ntfs_inode* bitmap, ntfs_cluster_t loc, int cnt, int bit)
{
	int bsize, locit, error;
	unsigned char *bits, *it;
	ntfs_io io;

	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	bsize = (cnt + (loc & 7) + 7) >> 3; /* Round up to multiple of 8. */
	bits = ntfs_malloc(bsize);
	if (!bits)
		return -ENOMEM;
	io.param = bits;
	io.size = bsize;
	error = ntfs_read_attr(bitmap, bitmap->vol->at_data, 0, loc >> 3, &io);
	if (error || io.size != bsize) {
		ntfs_free(bits);
		return error ? error : -EIO;
	}
	/* Now set the bits. */
	it = bits;
	locit = loc;
	while (locit % 8 && cnt) { /* Process first byte. */
		if (bit)
			*it |= 1 << (locit % 8);
		else
			*it &= ~(1 << (locit % 8));
		cnt--;
		locit++;
		if (locit % 8 == 0)
			it++;
	}
	while (cnt > 8) { /* Process full bytes. */
		*it = bit ? 0xFF : 0;
		cnt -= 8;
		locit += 8;
		it++;
	}
	while (cnt) { /* Process last byte. */
		if (bit)
			*it |= 1 << (locit % 8);
		else
			*it &= ~(1 << (locit % 8));
		cnt--;
		locit++;
	}
	/* Reset to start. */
	io.param = bits;
	io.size = bsize;
	error = ntfs_write_attr(bitmap, bitmap->vol->at_data, 0, loc >> 3, &io);
	ntfs_free(bits);
	if (error)
		return error;
	if (io.size != bsize)
		return -EIO;
	return 0;
}
	
/* Allocate count clusters around location. If location is -1, it does not
 * matter where the clusters are. Result is 0 if success, in which case
 * location and count says what they really got. */
int ntfs_search_bits(ntfs_inode* bitmap, ntfs_cluster_t *location, int *count,
		     int flags)
{
	unsigned char *bits;
	ntfs_io io;
	int error = 0, found = 0;
	int cnt, bloc = -1, bcnt = 0;
	int start;
	ntfs_cluster_t loc;

	bits = ntfs_malloc(2048);
	if (!bits)
		return -ENOMEM;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	io.param = bits;

	/* first search within +/- 8192 clusters */
	start = *location >> 3;
	start = start > 1024 ? start - 1024 : 0;
	io.size = 2048;
	error = ntfs_read_attr(bitmap, bitmap->vol->at_data, 0, start, &io);
	if (error)
		goto fail;
	loc = start * 8;
	cnt = *count;
	error = search_bits(bits, &loc, &cnt, io.size);
	if (error)
		goto fail;
	if (*count == cnt){
		bloc = loc;
		bcnt = cnt;
		goto success;
	}
	/* Now search from the beginning. */
	for (start = 0; 1; start += 2048) {
		io.param = bits;
		io.size = 2048;
		error = ntfs_read_attr(bitmap, bitmap->vol->at_data, 0, start,
									&io);
		if (error)
			goto fail;
		if (io.size == 0) {
			if(found)
				goto success;
			else {
				error = -ENOSPC;
				goto fail;
			}
		}
		loc = start * 8;
		cnt = *count;
		error = search_bits(bits, &loc, &cnt, io.size);
		if (error)
			goto fail;
		if (*count == cnt)
			goto success;
		if (bcnt < cnt) {
			bcnt = cnt;
			bloc = loc;
			found = 1;
		}
	}
 success:
	ntfs_free(bits);
	/* check flags */
	if ((flags & ALLOC_REQUIRE_LOCATION) && *location != bloc)
		error = -ENOSPC;
	else if ((flags & ALLOC_REQUIRE_SIZE) && *count != bcnt)
		error = -ENOSPC;
	else
		ntfs_set_bitrange(bitmap, bloc, bcnt, 1);
	/* If allocation failed due to the flags, tell the caller what he could
	 * have gotten */
	*location = bloc;
	*count = bcnt;
	return 0;
 fail:
	*location = -1;
	*count = 0;
	ntfs_free(bits);
	return error;
}

int ntfs_allocate_clusters(ntfs_volume *vol, ntfs_cluster_t *location,
			   int *count, int flags)
{
	int error;
	error = ntfs_search_bits(vol->bitmap, location, count, flags);
	return error;
}

int ntfs_deallocate_clusters(ntfs_volume *vol, ntfs_cluster_t location,
			     int count)
{
	int error;
	error = ntfs_set_bitrange(vol->bitmap, location, count, 0);
	return error;
}

