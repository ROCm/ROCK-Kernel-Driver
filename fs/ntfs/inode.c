/*
 * inode.c
 *
 * Copyright (C) 1995-1999 Martin von Löwis
 * Copyright (C) 1996 Albert D. Cahalan
 * Copyright (C) 1996-1997 Régis Duchesne
 * Copyright (C) 1998 Joseph Malicki
 * Copyright (C) 1999 Steve Dodd
 * Copyright (C) 2000-2001 Anton Altaparmakov (AIA)
 */

#include "ntfstypes.h"
#include "ntfsendian.h"
#include "struct.h"
#include "inode.h"
#include <linux/errno.h>
#include "macros.h"
#include "attr.h"
#include "super.h"
#include "dir.h"
#include "support.h"
#include "util.h"
#include <linux/smp_lock.h>

typedef struct {
	int recno;
	unsigned char *record;
} ntfs_mft_record;

typedef struct {
	int size;
	int count;
	ntfs_mft_record *records;
} ntfs_disk_inode;

void ntfs_fill_mft_header(ntfs_u8 *mft, int record_size, int blocksize,
			  int sequence_number)
{
	int fixup_count = record_size / blocksize + 1;
	int attr_offset = (0x2a + (2 * fixup_count) + 7) & ~7;
	int fixup_offset = 0x2a;

	NTFS_PUTU32(mft + 0x00, 0x454c4946);	     /* FILE */
	NTFS_PUTU16(mft + 0x04, 0x2a);		     /* Offset to fixup. */
	NTFS_PUTU16(mft + 0x06, fixup_count);	     /* Number of fixups. */
	NTFS_PUTU16(mft + 0x10, sequence_number);    /* Sequence number. */
	NTFS_PUTU16(mft + 0x12, 1);                  /* Hard link count. */
	NTFS_PUTU16(mft + 0x14, attr_offset);	     /* Offset to attributes. */
	NTFS_PUTU16(mft + 0x16, 1);                  /* Flags: 1 = In use,
							       2 = Directory. */
	NTFS_PUTU32(mft + 0x18, attr_offset + 0x08); /* Bytes in use. */
	NTFS_PUTU32(mft + 0x1c, record_size);	     /* Total allocated size. */
	NTFS_PUTU16(mft + fixup_offset, 1);	     /* Fixup word. */
	NTFS_PUTU32(mft + attr_offset, 0xffffffff);  /* End marker. */
}

/* Search in an inode an attribute by type and name. 
 * FIXME: Check that when attributes are inserted all attribute list
 * attributes are expanded otherwise need to modify this function to deal
 * with attribute lists. (AIA) */
ntfs_attribute *ntfs_find_attr(ntfs_inode *ino, int type, char *name)
{
	int i;
	
	if (!ino) {
		ntfs_error("ntfs_find_attr: NO INODE!\n");
		return 0;
	}
	for (i = 0; i < ino->attr_count; i++) {
		if (type < ino->attrs[i].type)
			return 0;
		if (type == ino->attrs[i].type) {
			if (!name) {
				if (!ino->attrs[i].name)
					return ino->attrs + i;
			} else if (ino->attrs[i].name &&
				   !ntfs_ua_strncmp(ino->attrs[i].name, name,
						    strlen(name)))
				return ino->attrs + i;
		}
	}
	return 0;
}

/* FIXME: Need better strategy to extend the MFT. */
static int ntfs_extend_mft(ntfs_volume *vol)
{
	/* Try to allocate at least 0.1% of the remaining disk space for
	 * inodes. If the disk is almost full, make sure at least one inode is
	 * requested. */
	int rcount, error, block, blockbits;
	__s64 size;
	ntfs_attribute *mdata, *bmp;
	ntfs_u8 *buf;
	ntfs_io io;

	mdata = ntfs_find_attr(vol->mft_ino, vol->at_data, 0);
	if (!mdata)
		return -EINVAL;
	/* First check whether there is uninitialized space. */
	if (mdata->allocated < mdata->size + vol->mft_record_size) {
		size = (__s64)ntfs_get_free_cluster_count(vol->bitmap) <<
							vol->cluster_size_bits;
		/* On error, size will be negative. We can ignore this as we
		 * will fall back to the minimal size allocation below. (AIA) */
		block = vol->mft_record_size;
		blockbits = vol->mft_record_size_bits;
		size = max(s64, size >> 10, mdata->size + vol->mft_record_size);
		size = (__s64)((size + block - 1) >> blockbits) << blockbits;
		/* Require this to be a single chunk. */
		error = ntfs_extend_attr(vol->mft_ino, mdata, &size,
							   ALLOC_REQUIRE_SIZE);
		/* Try again, now we have the largest available fragment. */
		if (error == -ENOSPC) {
			/* Round down to multiple of mft record size. */
			size = (__s64)(size >> vol->mft_record_size_bits) <<
						vol->mft_record_size_bits;
			if (!size)
				return -ENOSPC;
			error = ntfs_extend_attr(vol->mft_ino, mdata, &size,
							   ALLOC_REQUIRE_SIZE);
		}
		if (error)
			return error;
	}
	/* Even though we might have allocated more than needed, we initialize
	 * only one record. */
	mdata->size += vol->mft_record_size;
	/* Now extend the bitmap if necessary. */
	rcount = mdata->size >> vol->mft_record_size_bits;
	bmp = ntfs_find_attr(vol->mft_ino, vol->at_bitmap, 0);
	if (!bmp)
		return -EINVAL;
	if (bmp->size * 8 < rcount) { /* Less bits than MFT records. */
		ntfs_u8 buf[1];
		/* Extend bitmap by one byte. */
		error = ntfs_resize_attr(vol->mft_ino, bmp, bmp->size + 1);
		if (error)
			return error;
		/* Write the single byte. */
		buf[0] = 0;
		io.fn_put = ntfs_put;
		io.fn_get = ntfs_get;
		io.param = buf;
		io.size = 1;
		error = ntfs_write_attr(vol->mft_ino, vol->at_bitmap, 0,
					bmp->size - 1, &io);
		if (error)
			return error;
		if (io.size != 1)
			return -EIO;
	}
	/* Now fill in the MFT header for the new block. */
	buf = ntfs_calloc(vol->mft_record_size);
	if (!buf)
		return -ENOMEM;
	ntfs_fill_mft_header(buf, vol->mft_record_size, vol->sector_size, 0);
	ntfs_insert_fixups(buf, vol->sector_size);
	io.param = buf;
	io.size = vol->mft_record_size;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	error = ntfs_write_attr(vol->mft_ino, vol->at_data, 0,
			(__s64)(rcount - 1) << vol->mft_record_size_bits, &io);
	if (error)
		return error;
	if (io.size != vol->mft_record_size)
		return -EIO;
	error = ntfs_update_inode(vol->mft_ino);
	if (error)
		return error;
	return 0;
}

/*
 * Insert all attributes from the record mftno of the MFT in the inode ino.
 * If mftno is a base mft record we abort as soon as we find the attribute
 * list, but only on the first pass. We will get called later when the attribute
 * list attribute is being parsed so we need to distinguish the two cases.
 * FIXME: We should be performing structural consistency checks. (AIA)
 * Return 0 on success or -errno on error.
 */
static int ntfs_insert_mft_attributes(ntfs_inode* ino, char *mft, int mftno)
{
	int i, error, type, len, present = 0;
	char *it;

	/* Check for duplicate extension record. */
	for(i = 0; i < ino->record_count; i++)
		if (ino->records[i] == mftno) {
			if (i)
				return 0;
			present = 1;
			break;
		}
	if (!present) {
		/* (re-)allocate space if necessary. */
		if (ino->record_count % 8 == 0)	{
			int *new;

			new = ntfs_malloc((ino->record_count + 8) *
								sizeof(int));
			if (!new)
				return -ENOMEM;
			if (ino->records) {
				for (i = 0; i < ino->record_count; i++)
					new[i] = ino->records[i];
				ntfs_free(ino->records);
			}
			ino->records = new;
		}
		ino->records[ino->record_count] = mftno;
		ino->record_count++;
	}
	it = mft + NTFS_GETU16(mft + 0x14); /* mft->attrs_offset */
	do {
		type = NTFS_GETU32(it);
		len = NTFS_GETU32(it + 4);
		if (type != -1) {
			error = ntfs_insert_attribute(ino, it);
			if (error)
				return error;
		}
		/* If we have just processed the attribute list and this is
		 * the first time we are parsing this (base) mft record then we
		 * are done so that the attribute list gets parsed before the
		 * entries in the base mft record. Otherwise we run into
		 * problems with encountering attributes out of order and when
		 * this happens with different attribute extents we die. )-:
		 * This way we are ok as the attribute list is always sorted
		 * fully and correctly. (-: */
		if (type == 0x20 && !present)
			return 0;
		it += len;
	} while (type != -1); /* Attribute listing ends with type -1. */
	return 0;
}

/*
 * Insert a single specific attribute from the record mftno of the MFT in the
 * inode ino. We disregard the attribute list assuming we have already parsed
 * it.
 * FIXME: We should be performing structural consistency checks. (AIA)
 * Return 0 on success or -errno on error.
 */
static int ntfs_insert_mft_attribute(ntfs_inode* ino, int mftno,
				     ntfs_u8 *attr)
{
	int i, error, present = 0;

	/* Check for duplicate extension record. */
	for(i = 0; i < ino->record_count; i++)
		if (ino->records[i] == mftno) {
			present = 1;
			break;
		}
	if (!present) {
		/* (re-)allocate space if necessary. */
		if (ino->record_count % 8 == 0)	{
			int *new;

			new = ntfs_malloc((ino->record_count + 8) *
								sizeof(int));
			if (!new)
				return -ENOMEM;
			if (ino->records) {
				for (i = 0; i < ino->record_count; i++)
					new[i] = ino->records[i];
				ntfs_free(ino->records);
			}
			ino->records = new;
		}
		ino->records[ino->record_count] = mftno;
		ino->record_count++;
	}
	if (NTFS_GETU32(attr) == -1) {
		ntfs_debug(DEBUG_FILE3, "ntfs_insert_mft_attribute: attribute "
				"type is -1.\n");
		return 0;
	}
	error = ntfs_insert_attribute(ino, attr);
	if (error)
		return error;
	return 0;
}

/* Read and insert all the attributes of an 'attribute list' attribute.
 * Return the number of remaining bytes in *plen. */
static int parse_attributes(ntfs_inode *ino, ntfs_u8 *alist, int *plen)
{
	ntfs_u8 *mft, *attr;
	int mftno, l, error;
	int last_mft = -1;
	int len = *plen;
	
	if (!ino->attr) {
		ntfs_error("parse_attributes: called on inode 0x%x without a "
				"loaded base mft record.\n", ino->i_number);
		return -EINVAL;
	}
	mft = ntfs_malloc(ino->vol->mft_record_size);
	if (!mft)
		return -ENOMEM;
	while (len > 8)	{
		l = NTFS_GETU16(alist + 4);
		if (l > len)
			break;
	        /* Process an attribute description. */
		mftno = NTFS_GETU32(alist + 0x10); 
			/* FIXME: The mft reference (alist + 0x10) is __s64.
			* - Not a problem unless we encounter a huge partition.
			* - Should be consistency checking the sequence numbers
			*   though! This should maybe happen in 
			*   ntfs_read_mft_record() itself and a hotfix could
			*   then occur there or the user notified to run
			*   ntfsck. (AIA) */
		if (mftno != ino->i_number && mftno != last_mft) {
			last_mft = mftno;
			error = ntfs_read_mft_record(ino->vol, mftno, mft);
			if (error) {
				ntfs_debug(DEBUG_FILE3, "parse_attributes: "
					"ntfs_read_mft_record(mftno = 0x%x) "
					"failed\n", mftno);
				ntfs_free(mft);
				return error;
			}
		}
		attr = ntfs_find_attr_in_mft_rec(
				ino->vol,		/* ntfs volume */
				mftno == ino->i_number ?/* mft record is: */
					ino->attr:	/*   base record */
					mft,		/*   extension record */
				NTFS_GETU32(alist + 0),	/* type */
				(wchar_t*)(alist + alist[7]),	/* name */
				alist[6], 		/* name length */
				1,			/* ignore case */
				NTFS_GETU16(alist + 24)	/* instance number */
				);
		if (!attr) {
			ntfs_error("parse_attributes: mft records 0x%x and/or "
				       "0x%x corrupt!\n", ino->i_number, mftno);
			ntfs_free(mft);
			return -EINVAL; /* FIXME: Better error code? (AIA) */
		}
		error = ntfs_insert_mft_attribute(ino, mftno, attr);
		if (error) {
			ntfs_debug(DEBUG_FILE3, "parse_attributes: "
				"ntfs_insert_mft_attribute(mftno 0x%x, "
				"attribute type 0x%x) failed\n", mftno,
				NTFS_GETU32(alist + 0));
			ntfs_free(mft);
			return error;
		}
		len -= l;
		alist += l;
	}
	ntfs_free(mft);
	*plen = len;
	return 0;
}

static void ntfs_load_attributes(ntfs_inode* ino)
{
	ntfs_attribute *alist;
	int datasize;
	int offset, len, delta;
	char *buf;
	ntfs_volume *vol = ino->vol;
	
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 1\n", ino->i_number);
	if (ntfs_insert_mft_attributes(ino, ino->attr, ino->i_number))
		return;
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 2\n", ino->i_number);
	alist = ntfs_find_attr(ino, vol->at_attribute_list, 0);
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 3\n", ino->i_number);
	if (!alist)
		return;
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 4\n", ino->i_number);
	datasize = alist->size;
	ntfs_debug(DEBUG_FILE2, "load_attributes %x: alist->size = 0x%x\n",
			ino->i_number, alist->size);
	if (alist->resident) {
		parse_attributes(ino, alist->d.data, &datasize);
		return;
	}
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 5\n", ino->i_number);
	buf = ntfs_malloc(1024);
	if (!buf)    /* FIXME: Should be passing error code to caller. (AIA) */
		return;
	delta = 0;
	for (offset = 0; datasize; datasize -= len, offset += len) {
		ntfs_io io;
		
		io.fn_put = ntfs_put;
		io.fn_get = 0;
		io.param = buf + delta;
		io.size = len = min(int, datasize, 1024 - delta);
		ntfs_debug(DEBUG_FILE2, "load_attributes %x: len = %i\n",
						ino->i_number, len);
		ntfs_debug(DEBUG_FILE2, "load_attributes %x: delta = %i\n",
						ino->i_number, delta);
		if (ntfs_read_attr(ino, vol->at_attribute_list, 0, offset,
				   &io))
			ntfs_error("error in load_attributes\n");
		delta += len;
		ntfs_debug(DEBUG_FILE2, "load_attributes %x: after += len, "
				"delta = %i\n", ino->i_number, delta);
		parse_attributes(ino, buf, &delta);
		ntfs_debug(DEBUG_FILE2, "load_attributes %x: after parse_attr, "
				"delta = %i\n", ino->i_number, delta);
		if (delta)
			/* Move remaining bytes to buffer start. */
			ntfs_memmove(buf, buf + len - delta, delta);
	}
	ntfs_debug(DEBUG_FILE2, "load_attributes %x 6\n", ino->i_number);
	ntfs_free(buf);
}
	
int ntfs_init_inode(ntfs_inode *ino, ntfs_volume *vol, int inum)
{
	char *buf;
	int error;

	ntfs_debug(DEBUG_FILE1, "Initializing inode %x\n", inum);
	ino->i_number = inum;
	ino->vol = vol;
	ino->attr = buf = ntfs_malloc(vol->mft_record_size);
	if (!buf)
		return -ENOMEM;
	error = ntfs_read_mft_record(vol, inum, ino->attr);
	if (error) {
		ntfs_debug(DEBUG_OTHER, "Init inode: %x failed\n", inum);
		return error;
	}
	ntfs_debug(DEBUG_FILE2, "Init inode: got mft %x\n", inum);
	ino->sequence_number = NTFS_GETU16(buf + 0x10);
	ino->attr_count = 0;
	ino->record_count = 0;
	ino->records = 0;
	ino->attrs = 0;
	ntfs_load_attributes(ino);
	ntfs_debug(DEBUG_FILE2, "Init inode: done %x\n", inum);
	return 0;
}

void ntfs_clear_inode(ntfs_inode *ino)
{
	int i;
	if (!ino->attr) {
		ntfs_error("ntfs_clear_inode: double free\n");
		return;
	}
	ntfs_free(ino->attr);
	ino->attr = 0;
	ntfs_free(ino->records);
	ino->records = 0;
	for (i = 0; i < ino->attr_count; i++) {
		if (ino->attrs[i].name)
			ntfs_free(ino->attrs[i].name);
		if (ino->attrs[i].resident) {
			if (ino->attrs[i].d.data)
				ntfs_free(ino->attrs[i].d.data);
		} else {
			if (ino->attrs[i].d.r.runlist)
				ntfs_vfree(ino->attrs[i].d.r.runlist);
		}
	}
	ntfs_free(ino->attrs);
	ino->attrs = 0;
}

/* Check and fixup a MFT record. */
int ntfs_check_mft_record(ntfs_volume *vol, char *record)
{
	return ntfs_fixup_record(vol, record, "FILE", vol->mft_record_size);
}

/* Return (in result) the value indicating the next available attribute 
 * chunk number. Works for inodes w/o extension records only. */
int ntfs_allocate_attr_number(ntfs_inode *ino, int *result)
{
	if (ino->record_count != 1)
		return -EOPNOTSUPP;
	*result = NTFS_GETU16(ino->attr + 0x28);
	NTFS_PUTU16(ino->attr + 0x28, (*result) + 1);
	return 0;
}

/* Find the location of an attribute in the inode. A name of NULL indicates
 * unnamed attributes. Return pointer to attribute or NULL if not found. */
char *ntfs_get_attr(ntfs_inode *ino, int attr, char *name)
{
	/* Location of first attribute. */
	char *it = ino->attr + NTFS_GETU16(ino->attr + 0x14);
	int type;
	int len;
	
	/* Only check for magic DWORD here, fixup should have happened before.*/
	if (!IS_MFT_RECORD(ino->attr))
		return 0;
	do {
		type = NTFS_GETU32(it);
		len = NTFS_GETU16(it + 4);
		/* We found the attribute type. Is the name correct, too? */
		if (type == attr) {
			int namelen = NTFS_GETU8(it + 9);
			char *name_it, *n = name;
			/* Match given name and attribute name if present.
			   Make sure attribute name is Unicode. */
			if (!name) {
				goto check_namelen;
			} else if (namelen) {
				for (name_it = it + NTFS_GETU16(it + 10);
				     namelen; n++, name_it += 2, namelen--)
					if (*name_it != *n || name_it[1])
						break;
check_namelen:
				if (!namelen)
					break;
			}
		}
		it += len;
	} while (type != -1); /* List of attributes ends with type -1. */
	if (type == -1)
		return 0;
	return it;
}

__s64 ntfs_get_attr_size(ntfs_inode *ino, int type, char *name)
{
	ntfs_attribute *attr = ntfs_find_attr(ino, type, name);
	if (!attr)
		return 0;
	return
		attr->size;
}
	
int ntfs_attr_is_resident(ntfs_inode *ino, int type, char *name)
{
	ntfs_attribute *attr = ntfs_find_attr(ino, type, name);
	if (!attr)
		return 0;
	return
		attr->resident;
}
	
/*
 * A run is coded as a type indicator, an unsigned length, and a signed cluster
 * offset.
 * . To save space, length and offset are fields of variable length. The low
 *   nibble of the type indicates the width of the length :), the high nibble
 *   the width of the offset.
 * . The first offset is relative to cluster 0, later offsets are relative to
 *   the previous cluster.
 *
 * This function decodes a run. Length is an output parameter, data and cluster
 * are in/out parameters.
 */
int ntfs_decompress_run(unsigned char **data, int *length, 
			ntfs_cluster_t *cluster, int *ctype)
{
	unsigned char type = *(*data)++;
	*ctype = 0;
	switch (type & 0xF) {
	case 1: 
		*length = NTFS_GETS8(*data);
		break;
	case 2: 
		*length = NTFS_GETS16(*data);
		break;
	case 3: 
		*length = NTFS_GETS24(*data);
		break;
        case 4: 
		*length = NTFS_GETS32(*data);
		break;
        	/* Note: cases 5-8 are probably pointless to code, since how
		 * many runs > 4GB of length are there? At the most, cases 5
		 * and 6 are probably necessary, and would also require making
		 * length 64-bit throughout. */
	default:
		ntfs_error("Can't decode run type field %x\n", type);
		return -1;
	}
//	ntfs_debug(DEBUG_FILE3, "ntfs_decompress_run: length = 0x%x\n",*length);
	if (*length < 0)
	{
		ntfs_error("Negative run length decoded\n");
		return -1;
	}
	*data += (type & 0xF);
	switch (type & 0xF0) {
	case 0:
		*ctype = 2;
		break;
	case 0x10:
		*cluster += NTFS_GETS8(*data);
		break;
	case 0x20:
		*cluster += NTFS_GETS16(*data);
		break;
	case 0x30:
		*cluster += NTFS_GETS24(*data);
		break;
	case 0x40:
		*cluster += NTFS_GETS32(*data);
		break;
#if 0 /* Keep for future, in case ntfs_cluster_t ever becomes 64bit. */
	case 0x50: 
		*cluster += NTFS_GETS40(*data);
		break;
	case 0x60: 
		*cluster += NTFS_GETS48(*data);
		break;
	case 0x70: 
		*cluster += NTFS_GETS56(*data);
		break;
	case 0x80: 
		*cluster += NTFS_GETS64(*data);
		break;
#endif
	default:
		ntfs_error("Can't decode run type field %x\n", type);
		return -1;
	}
//	ntfs_debug(DEBUG_FILE3, "ntfs_decompress_run: cluster = 0x%x\n",
//								*cluster);
	*data += (type >> 4);
	return 0;
}

/*
 * FIXME: ntfs_readwrite_attr() has the effect of writing @dest to @offset of
 * the attribute value of the attribute @attr in the in memory inode @ino.
 * If the attribute value of @attr is non-resident the value's contents at
 * @offset are actually written to disk (from @dest). The on disk mft record
 * describing the non-resident attribute value is not updated!
 * If the attribute value is resident then the value is written only in
 * memory. The on disk mft record containing the value is not written to disk.
 * A possible fix would be to call ntfs_update_inode() before returning. (AIA)
 */
/* Reads l bytes of the attribute (attr, name) of ino starting at offset on
 * vol into buf. Returns the number of bytes read in the ntfs_io struct.
 * Returns 0 on success, errno on failure */
int ntfs_readwrite_attr(ntfs_inode *ino, ntfs_attribute *attr, __s64 offset,
			ntfs_io *dest)
{
	int rnum;
	ntfs_cluster_t cluster, s_cluster, vcn, len;
	__s64 l, chunk, copied;
	int s_vcn;
	int error, clustersizebits;

	ntfs_debug(DEBUG_FILE3, "ntfs_readwrite_attr 0: inode = 0x%x, attr->"
		"type = 0x%x, offset = 0x%Lx, dest->size = 0x%x\n",
		ino->i_number, attr->type, offset, dest->size);
	l = dest->size;
	if (l == 0)
		return 0;
	if (dest->do_read) {
		/* If read _starts_ beyond end of stream, return nothing. */
		if (offset >= attr->size) {
			dest->size = 0;
			return 0;
		}
		/* If read _extends_ beyond end of stream, return as much
		 * initialised data as we have. */
		if (offset + l >= attr->size)
			l = dest->size = attr->size - offset;
	} else {
		/* Fixed by CSA: If writing beyond end, extend attribute. */
		/* If write extends beyond _allocated_ size, extend attrib. */
		if (offset + l > attr->allocated) {
			error = ntfs_resize_attr(ino, attr, offset + l);
			if (error)
				return error;
		}
		/* The amount of initialised data has increased: update. */
		/* FIXME: Shouldn't we zero-out the section between the old
		 * 	  initialised length and the write start? */
		if (offset + l > attr->initialized) {
			attr->initialized = offset + l;
			attr->size = offset + l;
		}
	}
	if (attr->resident) {
		if (dest->do_read)
			dest->fn_put(dest, (ntfs_u8*)attr->d.data + offset, l);
		else
			dest->fn_get((ntfs_u8*)attr->d.data + offset, dest, l);
		dest->size = l;
		return 0;
	}
	if (dest->do_read) {
		/* Read uninitialized data. */
		if (offset >= attr->initialized)
			return ntfs_read_zero(dest, l);
		if (offset + l > attr->initialized) {
			dest->size = chunk = offset + l - attr->initialized;
			error = ntfs_readwrite_attr(ino, attr, offset, dest);
			if (error)
				return error;
			return ntfs_read_zero(dest, l - chunk);
		}
		if (attr->compressed)
			return ntfs_read_compressed(ino, attr, offset, dest);
	} else {
		if (attr->compressed)
			return ntfs_write_compressed(ino, attr, offset, dest);
	}
	vcn = 0;
	clustersizebits = ino->vol->cluster_size_bits;
	s_vcn = offset >> clustersizebits;
	for (rnum = 0; rnum < attr->d.r.len && 
		       vcn + attr->d.r.runlist[rnum].len <= s_vcn; rnum++) {
		vcn += attr->d.r.runlist[rnum].len;
	}
	if (rnum == attr->d.r.len) {
		ntfs_debug(DEBUG_FILE3, "ntfs_readwrite_attr: EOPNOTSUPP: "
			"inode = 0x%x, rnum = %i, offset = 0x%Lx, vcn = , 0x%x"
			"s_vcn = 0x%x\n", ino->i_number, rnum, offset, vcn,
			s_vcn);
		/*FIXME: Should extend runlist. */
		return -EOPNOTSUPP;
	}
	copied = 0;
	while (l) {
		s_vcn = offset >> clustersizebits;
		cluster = attr->d.r.runlist[rnum].cluster;
		len = attr->d.r.runlist[rnum].len;
		s_cluster = cluster + s_vcn - vcn;
		chunk = min(s64, ((__s64)(vcn + len) << clustersizebits) - offset,
									l);
		dest->size = chunk;
		error = ntfs_getput_clusters(ino->vol, s_cluster, offset -
				((__s64)s_vcn << clustersizebits), dest);
		if (error) {
			ntfs_error("Read/write error\n");
			dest->size = copied;
			return error;
		}
		l -= chunk;
		copied += chunk;
		offset += chunk;
		if (l && offset >= ((__s64)(vcn + len) << clustersizebits)) {
			rnum++;
			vcn += len;
			cluster = attr->d.r.runlist[rnum].cluster;
			len = attr->d.r.runlist[rnum].len;
		}
	}
	dest->size = copied;
	return 0;
}

int ntfs_read_attr(ntfs_inode *ino, int type, char *name, __s64 offset,
		   ntfs_io *buf)
{
	ntfs_attribute *attr;

	buf->do_read = 1;
	attr = ntfs_find_attr(ino, type, name);
	if (!attr) {
		ntfs_debug(DEBUG_FILE3, "ntfs_read_attr: attr 0x%x not found "
				"in inode 0x%x\n", type, ino->i_number);
		return -EINVAL;
	}
	return ntfs_readwrite_attr(ino, attr, offset, buf);
}

int ntfs_write_attr(ntfs_inode *ino, int type, char *name, __s64 offset,
		    ntfs_io *buf)
{
	ntfs_attribute *attr;
	
	buf->do_read = 0;
	attr = ntfs_find_attr(ino, type, name);
	if (!attr)
		return -EINVAL;
	return ntfs_readwrite_attr(ino, attr, offset, buf);
}

int ntfs_vcn_to_lcn(ntfs_inode *ino, int vcn)
{
	int rnum;
	ntfs_attribute *data;
	
	data = ntfs_find_attr(ino, ino->vol->at_data, 0);
	/* It's hard to give an error code. */
	if (!data || data->resident || data->compressed)
		return -1;
	if (data->size <= (__s64)vcn << ino->vol->cluster_size_bits)
		return -1;
	/*
	 * For Linux, block number 0 represents a hole. Hopefully, nobody will
	 * attempt to bmap $Boot. FIXME: Hopes are not good enough! We need to
	 * fix this properly before reenabling mmap-ed stuff. (AIA)
	 */
	if (data->initialized <= (__s64)vcn << ino->vol->cluster_size_bits)
		return 0;
	for (rnum = 0; rnum < data->d.r.len &&
				vcn >= data->d.r.runlist[rnum].len; rnum++)
		vcn -= data->d.r.runlist[rnum].len;
	/* We need to cope with sparse runs. (AIA) */
	return data->d.r.runlist[rnum].cluster + vcn;
}

static int allocate_store(ntfs_volume *vol, ntfs_disk_inode *store, int count)
{
	int i;
	
	if (store->count > count)
		return 0;
	if (store->size < count) {
		ntfs_mft_record *n = ntfs_malloc((count + 4) * 
						 sizeof(ntfs_mft_record));
		if (!n)
			return -ENOMEM;
		if (store->size) {
			for (i = 0; i < store->size; i++)
				n[i] = store->records[i];
			ntfs_free(store->records);
		}
		store->size = count + 4;
		store->records = n;
	}
	for (i = store->count; i < count; i++) {
		store->records[i].record = ntfs_malloc(vol->mft_record_size);
		if (!store->records[i].record)
			return -ENOMEM;
		store->count++;
	}
	return 0;
}

static void deallocate_store(ntfs_disk_inode* store)
{
	int i;
	
	for (i = 0; i < store->count; i++)
		ntfs_free(store->records[i].record);
	ntfs_free(store->records);
	store->count = store->size = 0;
	store->records = 0;
}

/**
 * layout_runs - compress runlist into mapping pairs array
 * @attr:	attribute containing the runlist to compress
 * @rec:	destination buffer to hold the mapping pairs array
 * @offs:	current position in @rec (in/out variable)
 * @size:	size of the buffer @rec
 *
 * layout_runs walks the runlist in @attr, compresses it and writes it out the
 * resulting mapping pairs array into @rec (up to a maximum of @size bytes are
 * written). On entry @offs is the offset in @rec at which to begin writting the
 * mapping pairs array. On exit, it contains the offset in @rec of the first
 * byte after the end of the mapping pairs array.
 */
static int layout_runs(ntfs_attribute *attr, char *rec, int *offs, int size)
{
	int i, len, offset, coffs;
	/* ntfs_cluster_t MUST be signed! (AIA) */
	ntfs_cluster_t cluster, rclus;
	ntfs_runlist *rl = attr->d.r.runlist;
	cluster = 0;
	offset = *offs;
	for (i = 0; i < attr->d.r.len; i++) {
		/*
		 * We cheat with this check on the basis that cluster will never
		 * be less than -1 and the cluster delta will fit in signed
		 * 32-bits (ntfs_cluster_t). (AIA)
		 */
		if (rl[i].cluster < (ntfs_cluster_t)-1) {
			ntfs_error("layout_runs() encountered an out of bounds "
					"cluster delta!\n");
			return -ERANGE;
		}
		rclus = rl[i].cluster - cluster;
		len = rl[i].len;
		rec[offset] = 0;
 		if (offset + 9 > size)
			return -E2BIG; /* It might still fit, but this
					* simplifies testing. */
		/*
		 * Run length is stored as signed number, so deal with it
		 * properly, i.e. observe that a negative number will have all
		 * its most significant bits set to 1 but we don't store that
		 * in the mapping pairs array. We store the smallest type of
		 * negative number required, thus in the first if we check
		 * whether len fits inside a signed byte and if so we store it
		 * as such, the next ifs check for a signed short, then a signed
		 * 24-bit and finally the full blown signed 32-bit. Same goes
		 * for rlus below. (AIA)
		 */
		if (len >= -0x80 && len <= 0x7f) {
			NTFS_PUTU8(rec + offset + 1, len & 0xff);
			coffs = 1;
 		} else if (len >= -0x8000 && len <= 0x7fff) {
			NTFS_PUTU16(rec + offset + 1, len & 0xffff);
			coffs = 2;
 		} else if (len >= -0x800000 && len <= 0x7fffff) {
			NTFS_PUTU24(rec + offset + 1, len & 0xffffff);
			coffs = 3;
		} else /* if (len >= -0x80000000LL && len <= 0x7fffffff */ {
			NTFS_PUTU32(rec + offset + 1, len);
			coffs = 4;
		} /* else ... FIXME: When len becomes 64-bit we need to extend
		   * 		     the else if () statements. (AIA) */
		*(rec + offset) |= coffs++;
		if (rl[i].cluster == (ntfs_cluster_t)-1) /* Compressed run. */
			/* Nothing */;
		else if (rclus >= -0x80 && rclus <= 0x7f) {
			*(rec + offset) |= 0x10;
			NTFS_PUTS8(rec + offset + coffs, rclus & 0xff);
			coffs += 1;
		} else if (rclus >= -0x8000 && rclus <= 0x7fff) {
			*(rec + offset) |= 0x20;
			NTFS_PUTS16(rec + offset + coffs, rclus & 0xffff);
			coffs += 2;
		} else if (rclus >= -0x800000 && rclus <= 0x7fffff) {
			*(rec + offset) |= 0x30;
			NTFS_PUTS24(rec + offset + coffs, rclus & 0xffffff);
			coffs += 3;
		} else /* if (rclus >= -0x80000000LL && rclus <= 0x7fffffff)*/ {
			*(rec + offset) |= 0x40;
			NTFS_PUTS32(rec + offset + coffs, rclus
							/* & 0xffffffffLL */);
			coffs += 4;
		} /* FIXME: When rclus becomes 64-bit.
		else if (rclus >= -0x8000000000 && rclus <= 0x7FFFFFFFFF) {
			*(rec + offset) |= 0x50;
			NTFS_PUTS40(rec + offset + coffs, rclus &
							0xffffffffffLL);
			coffs += 5;
		} else if (rclus >= -0x800000000000 && 
						rclus <= 0x7FFFFFFFFFFF) {
			*(rec + offset) |= 0x60;
			NTFS_PUTS48(rec + offset + coffs, rclus &
							0xffffffffffffLL);
			coffs += 6;
		} else if (rclus >= -0x80000000000000 && 
						rclus <= 0x7FFFFFFFFFFFFF) {
			*(rec + offset) |= 0x70;
			NTFS_PUTS56(rec + offset + coffs, rclus &
							0xffffffffffffffLL);
			coffs += 7;
		} else {
			*(rec + offset) |= 0x80;
			NTFS_PUTS64(rec + offset + coffs, rclus);
			coffs += 8;
		} */
		offset += coffs;
		if (rl[i].cluster)
			cluster = rl[i].cluster;
	}
	if (offset >= size)
		return -E2BIG;
	/* Terminating null. */
	*(rec + offset++) = 0;
	*offs = offset;
	return 0;
}

static void count_runs(ntfs_attribute *attr, char *buf)
{
	ntfs_u32 first, count, last, i;
	
	first = 0;
	for (i = 0, count = 0; i < attr->d.r.len; i++)
		count += attr->d.r.runlist[i].len;
	last = first + count - 1;
	NTFS_PUTU64(buf + 0x10, first);
	NTFS_PUTU64(buf + 0x18, last);
} 

/**
 * layout_attr - convert in memory attribute to on disk attribute record
 * @attr:	in memory attribute to convert
 * @buf:	destination buffer for on disk attribute record
 * @size:	size of the destination buffer
 * @psize:	size of converted on disk attribute record (out variable)
 *
 * layout_attr takes the attribute @attr and converts it into the appropriate
 * on disk structure, writing it into @buf (up to @size bytes are written).
 * On return, @psize contains the actual size of the on disk attribute written
 * into @buf.
 */
static int layout_attr(ntfs_attribute* attr, char *buf, int size, int *psize)
{
	int nameoff, hdrsize, asize;
	
	if (attr->resident) {
		nameoff = 0x18;
		hdrsize = (nameoff + 2 * attr->namelen + 7) & ~7;
		asize = (hdrsize + attr->size + 7) & ~7;
		if (size < asize)
			return -E2BIG;
		NTFS_PUTU32(buf + 0x10, attr->size);
		NTFS_PUTU16(buf + 0x16, attr->indexed);
		NTFS_PUTU16(buf + 0x14, hdrsize);
		if (attr->size)
			ntfs_memcpy(buf + hdrsize, attr->d.data, attr->size);
	} else {
		int error;

 		if (attr->compressed)
 			nameoff = 0x48;
 		else
 			nameoff = 0x40;
 		hdrsize = (nameoff + 2 * attr->namelen + 7) & ~7;
 		if (size < hdrsize)
 			return -E2BIG;
 		/* Make asize point at the end of the attribute record header,
		   i.e. at the beginning of the mapping pairs array. */
 		asize = hdrsize;
 		error = layout_runs(attr, buf, &asize, size);
 		/* Now, asize points one byte beyond the end of the mapping
		   pairs array. */
		if (error)
 			return error;
 		/* The next attribute has to begin on 8-byte boundary. */
		asize = (asize + 7) & ~7;
		/* FIXME: fragments */
		count_runs(attr, buf);
		NTFS_PUTU16(buf + 0x20, hdrsize);
		NTFS_PUTU16(buf + 0x22, attr->cengine);
		NTFS_PUTU32(buf + 0x24, 0);
		NTFS_PUTS64(buf + 0x28, attr->allocated);
		NTFS_PUTS64(buf + 0x30, attr->size);
		NTFS_PUTS64(buf + 0x38, attr->initialized);
		if (attr->compressed)
			NTFS_PUTS64(buf + 0x40, attr->compsize);
	}
	NTFS_PUTU32(buf, attr->type);
	NTFS_PUTU32(buf + 4, asize);
	NTFS_PUTU8(buf + 8, attr->resident ? 0 : 1);
	NTFS_PUTU8(buf + 9, attr->namelen);
	NTFS_PUTU16(buf + 0xa, nameoff);
	NTFS_PUTU16(buf + 0xc, attr->compressed);
	NTFS_PUTU16(buf + 0xe, attr->attrno);
	if (attr->namelen)
		ntfs_memcpy(buf + nameoff, attr->name, 2 * attr->namelen);
	*psize = asize;
	return 0;
}

/**
 * layout_inode - convert an in memory inode into on disk mft record(s)
 * @ino:	in memory inode to convert
 * @store:	on disk inode, contain buffers for the on disk mft record(s)
 *
 * layout_inode takes the in memory inode @ino, converts it into a (sequence of)
 * mft record(s) and writes them to the appropriate buffers in the @store.
 *
 * Return 0 on success,
 * the required mft record count (>0) if the inode does not fit,
 * -ENOMEM if memory allocation problem or
 * -EOPNOTSUP if beyond our capabilities.
 */
int layout_inode(ntfs_inode *ino, ntfs_disk_inode *store)
{
	int offset, i, size, psize, error, count, recno;
	ntfs_attribute *attr;
	unsigned char *rec;

	error = allocate_store(ino->vol, store, ino->record_count);
	if (error)
		return error;
	size = ino->vol->mft_record_size;
 	count = i = 0;
 	do {
 		if (count < ino->record_count) {
 			recno = ino->records[count];
 		} else {
 			error = allocate_store(ino->vol, store, count + 1);
 			if (error)
 				return error;
	 		recno = -1;
		}
		/*
		 * FIXME: We need to support extension records properly.
		 * At the moment they wouldn't work. Probably would "just" get
		 * corrupted if we write to them... (AIA)
		 */
	 	store->records[count].recno = recno;
 		rec = store->records[count].record;
	 	count++;
 		/* Copy header. */
	 	offset = NTFS_GETU16(ino->attr + 0x14);
		ntfs_memcpy(rec, ino->attr, offset);
	 	/* Copy attributes. */
 		while (i < ino->attr_count) {
 			attr = ino->attrs + i;
	 		error = layout_attr(attr, rec + offset,
						size - offset - 8, &psize);
	 		if (error == -E2BIG && offset != NTFS_GETU16(ino->attr
						+ 0x14))
 				break;
 			if (error)
 				return error;
 			offset += psize;
 			i++;
 		}
 		/* Terminating attribute. */
		NTFS_PUTU32(rec + offset, 0xFFFFFFFF);
		offset += 4;
		NTFS_PUTU32(rec + offset, 0);
		offset += 4;
		NTFS_PUTU32(rec + 0x18, offset);
	} while (i < ino->attr_count || count < ino->record_count);
	return count - ino->record_count;
}

/*
 * FIXME: ntfs_update_inode() calls layout_inode() to create the mft record on
 * disk structure corresponding to the inode @ino. After that, ntfs_write_attr()
 * is called to write out the created mft record to disk.
 * We shouldn't need to re-layout every single time we are updating an mft
 * record. No wonder the ntfs driver is slow like hell. (AIA)
 */
int ntfs_update_inode(ntfs_inode *ino)
{
	int error, i;
	ntfs_disk_inode store;
	ntfs_io io;

	ntfs_bzero(&store, sizeof(store));
	error = layout_inode(ino, &store);
	if (error == -E2BIG) {
		error = ntfs_split_indexroot(ino);
		if (!error)
			error = layout_inode(ino, &store);
	}
	if (error == -E2BIG) {
		error = ntfs_attr_allnonresident(ino);
		if (!error)
			error = layout_inode(ino, &store);
	}
	if (error > 0) {
		/* FIXME: Introduce extension records. */
		error = -E2BIG;
	}
	if (error) {
		if (error == -E2BIG)
			ntfs_error("cannot handle saving inode %x\n",
				   ino->i_number);
		deallocate_store(&store);
		return error;
	}
	io.fn_get = ntfs_get;
	io.fn_put = 0;
	for (i = 0; i < store.count; i++) {
		ntfs_insert_fixups(store.records[i].record,
						ino->vol->sector_size);
		io.param = store.records[i].record;
		io.size = ino->vol->mft_record_size;
		/* FIXME: Is this the right way? */
		error = ntfs_write_attr(ino->vol->mft_ino, ino->vol->at_data,
					0, (__s64)store.records[i].recno <<
					ino->vol->mft_record_size_bits, &io);
		if (error || io.size != ino->vol->mft_record_size) {
			/* Big trouble, partially written file. */
			ntfs_error("Please unmount: Write error in inode "
					"0x%x\n", ino->i_number);
			deallocate_store(&store);
			return error ? error : -EIO;
		}
	}
	deallocate_store(&store);
	return 0;
}	

void ntfs_decompress(unsigned char *dest, unsigned char *src, ntfs_size_t l)
{
	int head, comp;
	int copied = 0;
	unsigned char *stop;
	int bits;
	int tag = 0;
	int clear_pos;
	
	while (1) {
		head = NTFS_GETU16(src) & 0xFFF;
		/* High bit indicates that compression was performed. */
		comp = NTFS_GETU16(src) & 0x8000;
		src += 2;
		stop = src + head;
		bits = 0;
		clear_pos = 0;
		if (head == 0)
			/* Block is not used. */
			return;/* FIXME: copied */
		if (!comp) { /* uncompressible */
			ntfs_memcpy(dest, src, 0x1000);
			dest += 0x1000;
			copied += 0x1000;
			src += 0x1000;
			if (l == copied)
				return;
			continue;
		}
		while (src <= stop) {
			if (clear_pos > 4096) {
				ntfs_error("Error 1 in decompress\n");
				return;
			}
			if (!bits) {
				tag = NTFS_GETU8(src);
				bits = 8;
				src++;
				if (src > stop)
					break;
			}
			if (tag & 1) {
				int i, len, delta, code, lmask, dshift;
				code = NTFS_GETU16(src);
				src += 2;
				if (!clear_pos) {
					ntfs_error("Error 2 in decompress\n");
					return;
				}
				for (i = clear_pos - 1, lmask = 0xFFF,
				     dshift = 12; i >= 0x10; i >>= 1) {
					lmask >>= 1;
					dshift--;
				}
				delta = code >> dshift;
				len = (code & lmask) + 3;
				for (i = 0; i < len; i++) {
					dest[clear_pos] = dest[clear_pos - 
								    delta - 1];
					clear_pos++;
					copied++;
					if (copied==l)
						return;
				}
			} else {
				dest[clear_pos++] = NTFS_GETU8(src);
				src++;
				copied++;
				if (copied==l)
					return;
			}
			tag >>= 1;
			bits--;
		}
		dest += clear_pos;
	}
}

/*
 * NOTE: Neither of the ntfs_*_bit functions are atomic! But we don't need
 * them atomic at present as we never operate on shared/cached bitmaps.
 */
static __inline__ int ntfs_get_bit(unsigned char *byte, const int bit)
{
	return byte[bit >> 3] & (1 << (bit & 7)) ? 1 : 0;
}

static __inline__ void ntfs_set_bit(unsigned char *byte, const int bit)
{
	byte[bit >> 3] |= 1 << (bit & 7);
}

static __inline__ void ntfs_clear_bit(unsigned char *byte, const int bit)
{
	byte[bit >> 3] &= ~(1 << (bit & 7));
}

__inline__ int ntfs_test_and_set_bit(unsigned char *byte, const int bit)
{
	unsigned char *ptr = byte + (bit >> 3);
	int b = 1 << (bit & 7);
	int oldbit = *ptr & b ? 1 : 0;
	*ptr |= b;
	return oldbit;
}

static __inline__ int ntfs_test_and_clear_bit(unsigned char *byte,
					      const int bit)
{
	unsigned char *ptr = byte + (bit >> 3);
	int b = 1 << (bit & 7);
	int oldbit = *ptr & b ? 1 : 0;
	*ptr &= ~b;
	return oldbit;
}

/**
 * ntfs_new_inode - allocate an mft record
 * @vol:	volume to allocate an mft record on
 * @result:	the mft record number allocated
 *
 * Allocate a new mft record on disk by finding the first free mft record
 * and allocating it in the mft bitmap.
 * Return 0 on success or -ERRNO on error.
 *
 * TODO(AIA): Implement mft bitmap caching.
 */
static int ntfs_new_inode(ntfs_volume *vol, unsigned long *result)
{
	int byte, bit, error, size, length;
	unsigned char value;
	ntfs_u8 *buffer;
	ntfs_io io;
	ntfs_attribute *data;

	*result = 0;
	/* Determine the number of mft records in the mft. */
	data = ntfs_find_attr(vol->mft_ino, vol->at_data, 0);
	if (!data)
		return -EINVAL;
	length = data->size >> vol->mft_record_size_bits;
	/* Allocate sufficient space for the mft bitmap attribute value,
	   inferring it from the number of mft records. */
	buffer = ntfs_malloc((length + 7) >> 3);
	if (!buffer)
		return -ENOMEM;
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
try_again:
	io.param = buffer;
	io.size = (length + 7) >> 3;
	error = ntfs_read_attr(vol->mft_ino, vol->at_bitmap, 0, 0, &io);
	if (error)
		goto err_out;
	size = io.size;
	/* Start at byte 0, as the bits for all 16 system files are already
	   set in the bitmap. */
	for (bit = byte = 0; (byte << 3) < length; byte++) {
		value = buffer[byte];
		if (value == 0xFF)
			continue;
		bit = ffz(value);
		if (bit < 8)
			break;
	}
	if ((byte << 3) + bit >= length) {
		/* No free space left. Need to extend the mft. */
		error = -ENOSPC;
		goto err_out;
	}
	/* Get the byte containing our bit again, now taking the BKL. */
	io.param = buffer;
	io.size = 1;
	lock_kernel();
	error = ntfs_read_attr(vol->mft_ino, vol->at_bitmap, 0, byte, &io);
	if (error || (io.size != 1 && (error = -EIO, 1)))
		goto err_unl_out;
	if (ntfs_test_and_set_bit(buffer, bit)) {
		unlock_kernel();
		/* Give other process(es) a chance to finish. */
		schedule();
		goto try_again;
	}
	io.param = buffer;
 	error = ntfs_write_attr(vol->mft_ino, vol->at_bitmap, 0, byte, &io);
	if (error || (io.size != 1 && (error = -EIO, 1)))
		goto err_unl_out;
	/* Change mft on disk, required when mft bitmap is resident. */
	error = ntfs_update_inode(vol->mft_ino);
	if (!error)
		*result = (byte << 3) + bit;
err_unl_out:
	unlock_kernel();
err_out:
	ntfs_free(buffer);
#ifdef DEBUG
	if (error)
		printk(KERN_INFO "ntfs_new_inode() failed to allocate an "
				 "mft record. Error = %i\n", error);
	else
		printk(KERN_INFO "ntfs_new_inode() allocated mft record number "
				 "0x%lx\n", *result);
#endif
	return error;
	/*
	 * FIXME: Don't forget $MftMirr, though this probably belongs
	 * in ntfs_update_inode() (or even deeper). (AIA)
	 */
}

static int add_mft_header(ntfs_inode *ino)
{
	unsigned char *mft;
	ntfs_volume *vol = ino->vol;

	mft = ino->attr;
	ntfs_bzero(mft, vol->mft_record_size);
	ntfs_fill_mft_header(mft, vol->mft_record_size, vol->sector_size,
			     ino->sequence_number);
	return 0;
}

/* We need 0x48 bytes in total. */
static int add_standard_information(ntfs_inode *ino)
{
	ntfs_time64_t now;
	char data[0x30];
	char *position = data;
	int error;
	ntfs_attribute *si;

	now = ntfs_now();
	NTFS_PUTU64(position + 0x00, now);		/* File creation */
	NTFS_PUTU64(position + 0x08, now);		/* Last modification */
	NTFS_PUTU64(position + 0x10, now);		/* Last mod for MFT */
	NTFS_PUTU64(position + 0x18, now);		/* Last access */
	NTFS_PUTU64(position + 0x20, 0);		/* MSDOS file perms */
	NTFS_PUTU64(position + 0x28, 0);		/* unknown */
	error = ntfs_create_attr(ino, ino->vol->at_standard_information, 0,
				 data, sizeof(data), &si);
	return error;
}

static int add_filename(ntfs_inode* ino, ntfs_inode* dir, 
			const unsigned char *filename, int length, 
			ntfs_u32 flags)
{
	unsigned char *position;
	unsigned int size;
	ntfs_time64_t now;
	int count, error;
	unsigned char* data;
	ntfs_attribute *fn;

	/* Work out the size. */
	size = 0x42 + 2 * length;
	data = ntfs_malloc(size);
	if (!data)
		return -ENOMEM;
	/* Search for a position. */
	position = data;
	NTFS_PUTINUM(position, dir);			/* Inode num of dir */
	now = ntfs_now();
	NTFS_PUTU64(position + 0x08, now);		/* File creation */
	NTFS_PUTU64(position + 0x10, now);		/* Last modification */
	NTFS_PUTU64(position + 0x18, now);		/* Last mod for MFT */
	NTFS_PUTU64(position + 0x20, now);		/* Last access */
	/* FIXME: Get the following two sizes by finding the data attribute
	 * in ino->attr and copying the corresponding fields from there.
	 * If no data present then set to zero. In current implementation
	 * add_data is called after add_filename so zero is correct on
	 * creation. Need to change when we have hard links / support different
	 * filename namespaces. (AIA) */
	NTFS_PUTS64(position + 0x28, 0);		/* Allocated size */
	NTFS_PUTS64(position + 0x30, 0);		/* Data size */
	NTFS_PUTU32(position + 0x38, flags);		/* File flags */
	NTFS_PUTU32(position + 0x3c, 0);		/* We don't use these
							 * features yet. */
	NTFS_PUTU8(position + 0x40, length);		/* Filename length */
	NTFS_PUTU8(position + 0x41, 0);			/* Only long name */
		/* FIXME: This is madness. We are defining the POSIX namespace
		 * for the filename here which can mean that the file will be
		 * invisible when in Windows NT/2k! )-: (AIA) */
	position += 0x42;
	for (count = 0; count < length; count++) {
		NTFS_PUTU16(position + 2 * count, filename[count]);
	}
	error = ntfs_create_attr(ino, ino->vol->at_file_name, 0, data, size,
				 &fn);
	if (!error)
		error = ntfs_dir_add(dir, ino, fn);
	ntfs_free(data);
	return error;
}

int add_security(ntfs_inode* ino, ntfs_inode* dir)
{
	int error;
	char *buf;
	int size;
	ntfs_attribute* attr;
	ntfs_io io;
	ntfs_attribute *se;

	attr = ntfs_find_attr(dir, ino->vol->at_security_descriptor, 0);
	if (!attr)
		return -EOPNOTSUPP; /* Need security in directory. */
	size = attr->size;
	if (size > 512)
		return -EOPNOTSUPP;
	buf = ntfs_malloc(size);
	if (!buf)
		return -ENOMEM;
	io.fn_get = ntfs_get;
	io.fn_put = ntfs_put;
	io.param = buf;
	io.size = size;
	error = ntfs_read_attr(dir, ino->vol->at_security_descriptor, 0, 0,&io);
	if (!error && io.size != size)
		ntfs_error("wrong size in add_security");
	if (error) {
		ntfs_free(buf);
		return error;
	}
	/* FIXME: Consider ACL inheritance. */
	error = ntfs_create_attr(ino, ino->vol->at_security_descriptor,
				 0, buf, size, &se);
	ntfs_free(buf);
	return error;
}

static int add_data(ntfs_inode* ino, unsigned char *data, int length)
{
	int error;
	ntfs_attribute *da;
	
	error = ntfs_create_attr(ino, ino->vol->at_data, 0, data, length, &da);
	return error;
}

/* We _could_ use 'dir' to help optimise inode allocation. */
int ntfs_alloc_inode(ntfs_inode *dir, ntfs_inode *result, const char *filename,
		     int namelen, ntfs_u32 flags)
{
	int error;
	ntfs_volume *vol = dir->vol;
	ntfs_u8 buffer[2];
	ntfs_io io;

	error = ntfs_new_inode(vol, &(result->i_number));
	if (error == -ENOSPC) {
		error = ntfs_extend_mft(vol);
		if (error)
			return error;
		error = ntfs_new_inode(vol, &(result->i_number));
	}
	if (error) {
		if (error == -ENOSPC)
			ntfs_error("ntfs_get_empty_inode: no free inodes\n");
		return error;
	}
	/* Get the sequence number. */
	io.fn_put = ntfs_put;
	io.fn_get = ntfs_get;
	io.param = buffer;
	io.size = 2;
	error = ntfs_read_attr(vol->mft_ino, vol->at_data, 0, 
			((__s64)result->i_number << vol->mft_record_size_bits)
			+ 0x10, &io);
	if (error)
		return error;
	/* Increment the sequence number skipping zero. */
	result->sequence_number = (NTFS_GETU16(buffer) + 1) & 0xffff;
	if (!result->sequence_number)
		result->sequence_number++;
	result->vol = vol;
	result->attr = ntfs_malloc(vol->mft_record_size);
	if (!result->attr)
		return -ENOMEM;
	result->attr_count = 0;
	result->attrs = 0;
	result->record_count = 1;
	result->records = ntfs_malloc(8 * sizeof(int));
	if (!result->records) {
		ntfs_free(result->attr);
		result->attr = 0;
		return -ENOMEM;
	}
	result->records[0] = result->i_number;
	error = add_mft_header(result);
	if (error)
		return error;
	error = add_standard_information(result);
	if (error)
		return error;
	error = add_filename(result, dir, filename, namelen, flags);
	if (error)
		return error;
	error = add_security(result, dir);
	if (error)
		return error;
	return 0;
}

int ntfs_alloc_file(ntfs_inode *dir, ntfs_inode *result, char *filename,
		    int namelen)
{
	int error = ntfs_alloc_inode(dir, result, filename, namelen, 0);
	if (error)
		return error;
	error = add_data(result, 0, 0);
	return error;
}

