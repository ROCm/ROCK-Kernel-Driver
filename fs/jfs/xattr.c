/*
 *   Copyright (c) International Business Machines  Corp., 2000-2002
 *   Copyright (c) Christoph Hellwig, 2002
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or 
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/fs.h>
#include <linux/xattr.h>
#include "jfs_incore.h"
#include "jfs_dmap.h"
#include "jfs_debug.h"
#include "jfs_dinode.h"
#include "jfs_extent.h"
#include "jfs_metapage.h"
#include "jfs_xattr.h"

/*
 *	jfs_xattr.c: extended attribute service
 *
 * Overall design --
 *
 * Format:
 *
 *   Extended attribute lists (jfs_ea_list) consist of an overall size (32 bit
 *   value) and a variable (0 or more) number of extended attribute
 *   entries.  Each extended attribute entry (jfs_ea) is a <name,value> double
 *   where <name> is constructed from a null-terminated ascii string
 *   (1 ... 255 bytes in the name) and <value> is arbitrary 8 bit data
 *   (1 ... 65535 bytes).  The in-memory format is
 *
 *   0       1        2        4                4 + namelen + 1
 *   +-------+--------+--------+----------------+-------------------+
 *   | Flags | Name   | Value  | Name String \0 | Data . . . .      |
 *   |       | Length | Length |                |                   |
 *   +-------+--------+--------+----------------+-------------------+
 *
 *   A jfs_ea_list then is structured as
 *
 *   0            4                   4 + EA_SIZE(ea1)
 *   +------------+-------------------+--------------------+-----
 *   | Overall EA | First FEA Element | Second FEA Element | ..... 
 *   | List Size  |                   |                    |
 *   +------------+-------------------+--------------------+-----
 *
 *   On-disk:
 *
 *     FEALISTs are stored on disk using blocks allocated by dbAlloc() and
 *     written directly. An EA list may be in-lined in the inode if there is
 *     sufficient room available.
 */

struct ea_buffer {
	int flag;		/* Indicates what storage xattr points to */
	int max_size;		/* largest xattr that fits in current buffer */
	dxd_t new_ea;		/* dxd to replace ea when modifying xattr */
	struct metapage *mp;	/* metapage containing ea list */
	struct jfs_ea_list *xattr;	/* buffer containing ea list */
};

/*
 * ea_buffer.flag values
 */
#define EA_INLINE	0x0001
#define EA_EXTENT	0x0002
#define EA_NEW		0x0004
#define EA_MALLOC	0x0008

/* Forward references */
void ea_release(struct inode *inode, struct ea_buffer *ea_buf);

/*
 * NAME: jfs_WriteEAInLine
 *                                                                    
 * FUNCTION: Attempt to write an EA inline if area is available
 *                                                                    
 * PRE CONDITIONS:
 *	Already verified that the specified EA is small enough to fit inline
 *
 * PARAMETERS:
 *	ip	- Inode pointer
 *	ealist	- EA list pointer
 *	size	- size of ealist in bytes
 *	ea	- dxd_t structure to be filled in with necessary EA information
 *		  if we successfully copy the EA inline
 *
 * NOTES:
 *	Checks if the inode's inline area is available.  If so, copies EA inline
 *	and sets <ea> fields appropriately.  Otherwise, returns failure, EA will
 *	have to be put into an extent.
 *
 * RETURNS: 0 for successful copy to inline area; -1 if area not available
 */
static int jfs_WriteEAInLine(struct inode *ip, struct jfs_ea_list *ealist,
			     int size, dxd_t * ea)
{
	struct jfs_inode_info *ji = JFS_IP(ip);

	/*
	 * Make sure we have an EA -- the NULL EA list is valid, but you
	 * can't copy it!
	 */
	if (ealist && size > sizeof (struct jfs_ea_list)) {
		assert(size <= sizeof (ji->i_inline_ea));

		/*
		 * See if the space is available or if it is already being
		 * used for an inline EA.
		 */
		if (!(ji->mode2 & INLINEEA) && !(ji->ea.flag & DXD_INLINE))
			return -1;

		DXDsize(ea, size);
		DXDlength(ea, 0);
		DXDaddress(ea, 0);
		memcpy(ji->i_inline_ea, ealist, size);
		ea->flag = DXD_INLINE;
		ji->mode2 &= ~INLINEEA;
	} else {
		ea->flag = 0;
		DXDsize(ea, 0);
		DXDlength(ea, 0);
		DXDaddress(ea, 0);

		/* Free up INLINE area */
		if (ji->ea.flag & DXD_INLINE)
			ji->mode2 |= INLINEEA;
	}

	mark_inode_dirty(ip);
	return 0;
}

/*
 * NAME: jfs_WriteEA
 *                                                                    
 * FUNCTION: Write an EA for an inode
 *                                                                    
 * PRE CONDITIONS: EA has been verified 
 *
 * PARAMETERS:
 *	ip	- Inode pointer
 *	ealist	- EA list pointer
 *	size	- size of ealist in bytes
 *	ea	- dxd_t structure to be filled in appropriately with where the
 *		  EA was copied
 *
 * NOTES: Will write EA inline if able to, otherwise allocates blocks for an
 *	extent and synchronously writes it to those blocks.
 *
 * RETURNS: 0 for success; Anything else indicates failure
 */
static int jfs_WriteEA(struct inode *ip, struct jfs_ea_list *ealist, int size,
		       dxd_t * ea)
{
	struct super_block *sb = ip->i_sb;
	struct jfs_inode_info *ji = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	int nblocks;
	s64 blkno;
	int rc = 0, i;
	char *cp;
	s32 nbytes, nb;
	s32 bytes_to_write;
	metapage_t *mp;

	/*
	 * Quick check to see if this is an in-linable EA.  Short EAs
	 * and empty EAs are all in-linable, provided the space exists.
	 */
	if (!ealist || size <= sizeof (ji->i_inline_ea)) {
		if (!jfs_WriteEAInLine(ip, ealist, size, ea))
			return 0;
	}

	/* figure out how many blocks we need */
	nblocks = (size + (sb->s_blocksize - 1)) >> sb->s_blocksize_bits;

	rc = dbAlloc(ip, INOHINT(ip), nblocks, &blkno);
	if (rc)
		return -rc;

	/*
	 * Now have nblocks worth of storage to stuff into the FEALIST.
	 * loop over the FEALIST copying data into the buffer one page at
	 * a time.
	 */
	cp = (char *) ealist;
	nbytes = size;
	for (i = 0; i < nblocks; i += sbi->nbperpage) {
		/*
		 * Determine how many bytes for this request, and round up to
		 * the nearest aggregate block size
		 */
		nb = min(PSIZE, nbytes);
		bytes_to_write =
		    ((((nb + sb->s_blocksize - 1)) >> sb->s_blocksize_bits))
		    << sb->s_blocksize_bits;

		if (!(mp = get_metapage(ip, blkno + i, bytes_to_write, 1))) {
			rc = -EIO;
			goto failed;
		}

		memcpy(mp->data, cp, nb);

		/*
		 * We really need a way to propagate errors for
		 * forced writes like this one.  --hch
		 *
		 * (__write_metapage => release_metapage => flush_metapage)
		 */
#ifdef _JFS_FIXME
		if ((rc = flush_metapage(mp))) {
			/*
			 * the write failed -- this means that the buffer
			 * is still assigned and the blocks are not being
			 * used.  this seems like the best error recovery
			 * we can get ...
			 */
			goto failed;
		}
#else
		flush_metapage(mp);
#endif

		cp += PSIZE;
		nbytes -= nb;
	}

	ea->flag = DXD_EXTENT;
	DXDsize(ea, ealist->size);
	DXDlength(ea, nblocks);
	DXDaddress(ea, blkno);

	/* Free up INLINE area */
	if (ji->ea.flag & DXD_INLINE)
		ip->i_mode |= INLINEEA;

	return 0;

      failed:
	dbFree(ip, blkno, nblocks);
	return rc;
}

/*
 * NAME: jfs_ReadEAInLine
 *                                                                    
 * FUNCTION: Read an inlined EA into user's buffer
 *                                                                    
 * PARAMETERS:
 *	ip	- Inode pointer
 *	ealist	- Pointer to buffer to fill in with EA
 *
 * RETURNS: 0
 */
static int jfs_ReadEAInLine(struct inode *ip, struct jfs_ea_list *ealist)
{
	struct jfs_inode_info *ji = JFS_IP(ip);
	int ea_size = sizeDXD(&ji->ea);

	if (ea_size == 0) {
		ealist->size = 0;
		return 0;
	}

	/* Sanity Check */
	if ((sizeDXD(&ji->ea) > sizeof (ji->i_inline_ea)))
		return -EIO;
	if (((struct jfs_ea_list *) &ji->i_inline_ea)->size != ea_size)
		return -EIO;

	memcpy(ealist, ji->i_inline_ea, ea_size);
	return 0;
}

/*
 * NAME: jfs_ReadEA
 *                                                                    
 * FUNCTION: copy EA data into user's buffer
 *                                                                    
 * PARAMETERS:
 *	ip	- Inode pointer
 *	ealist	- Pointer to buffer to fill in with EA
 *
 * NOTES:  If EA is inline calls jfs_ReadEAInLine() to copy EA.
 *
 * RETURNS: 0 for success; other indicates failure
 */
static int jfs_ReadEA(struct inode *ip, struct jfs_ea_list *ealist)
{
	struct super_block *sb = ip->i_sb;
	struct jfs_inode_info *ji = JFS_IP(ip);
	struct jfs_sb_info *sbi = JFS_SBI(sb);
	int nblocks;
	s64 blkno;
	char *cp = (char *) ealist;
	int i;
	int nbytes, nb;
	s32 bytes_to_read;
	metapage_t *mp;

	/* quick check for in-line EA */
	if (ji->ea.flag & DXD_INLINE)
		return jfs_ReadEAInLine(ip, ealist);

	nbytes = sizeDXD(&ji->ea);
	assert(nbytes);

	/* 
	 * Figure out how many blocks were allocated when this EA list was
	 * originally written to disk.
	 */
	nblocks = lengthDXD(&ji->ea) << sbi->l2nbperpage;
	blkno = addressDXD(&ji->ea) << sbi->l2nbperpage;

	/*
	 * I have found the disk blocks which were originally used to store
	 * the FEALIST.  now i loop over each contiguous block copying the
	 * data into the buffer.
	 */
	for (i = 0; i < nblocks; i += sbi->nbperpage) {
		/*
		 * Determine how many bytes for this request, and round up to
		 * the nearest aggregate block size
		 */
		nb = min(PSIZE, nbytes);
		bytes_to_read =
		    ((((nb + sb->s_blocksize - 1)) >> sb->s_blocksize_bits))
		    << sb->s_blocksize_bits;

		if (!(mp = read_metapage(ip, blkno + i, bytes_to_read, 1)))
			return -EIO;

		memcpy(cp, mp->data, nb);
		release_metapage(mp);

		cp += PSIZE;
		nbytes -= nb;
	}

	return 0;
}

/*
 * NAME: ea_get
 *                                                                    
 * FUNCTION: Returns buffer containing existing extended attributes.
 *	     The size of the buffer will be the larger of the existing
 *	     attributes size, or min_size.
 *
 *	     The buffer, which may be inlined in the inode or in the
 * 	     page cache must be release by calling ea_release or ea_put
 *                                                                    
 * PARAMETERS:
 *	inode	- Inode pointer
 *	ea_buf	- Structure to be populated with ealist and its metadata
 *	min_size- minimum size of buffer to be returned
 *
 * RETURNS: 0 for success; Other indicates failure
 */
int ea_get(struct inode *inode, struct ea_buffer *ea_buf, int min_size)
{
	struct jfs_inode_info *ji = JFS_IP(inode);
	struct super_block *sb = inode->i_sb;
	int size;
	int ea_size = sizeDXD(&ji->ea);
	int blocks_needed, current_blocks;
	s64 blkno;
	int rc;

	/* When fsck.jfs clears a bad ea, it doesn't clear the size */
	if (ji->ea.flag == 0)
		ea_size = 0;

	if (ea_size == 0) {
		if (min_size == 0) {
			ea_buf->flag = 0;
			ea_buf->max_size = 0;
			ea_buf->xattr = NULL;
			return 0;
		}
		if ((min_size <= sizeof (ji->i_inline_ea)) &&
		    (ji->mode2 & INLINEEA)) {
			ea_buf->flag = EA_INLINE | EA_NEW;
			ea_buf->max_size = sizeof (ji->i_inline_ea);
			ea_buf->xattr = (struct jfs_ea_list *) ji->i_inline_ea;
			DXDlength(&ea_buf->new_ea, 0);
			DXDaddress(&ea_buf->new_ea, 0);
			ea_buf->new_ea.flag = DXD_INLINE;
			DXDsize(&ea_buf->new_ea, min_size);
			return 0;
		}
		current_blocks = 0;
	} else if (ji->ea.flag & DXD_INLINE) {
		if (min_size <= sizeof (ji->i_inline_ea)) {
			ea_buf->flag = EA_INLINE;
			ea_buf->max_size = sizeof (ji->i_inline_ea);
			ea_buf->xattr = (struct jfs_ea_list *) ji->i_inline_ea;
			goto size_check;
		}
		current_blocks = 0;
	} else {
		assert(ji->ea.flag & DXD_EXTENT);
		current_blocks = (ea_size + sb->s_blocksize - 1) >>
		    sb->s_blocksize_bits;
	}
	size = max(min_size, ea_size);

	if (size > PSIZE) {
		/*
		 * To keep the rest of the code simple.  Allocate a
		 * contiguous buffer to work with
		 */
		ea_buf->xattr = kmalloc(size, GFP_KERNEL);
		if (ea_buf->xattr == NULL)
			return -ENOMEM;

		ea_buf->flag |= EA_MALLOC;
		ea_buf->max_size = (size + sb->s_blocksize - 1) &
		    ~(sb->s_blocksize - 1);

		if (ea_size == 0)
			return 0;

		rc = jfs_ReadEA(inode, ea_buf->xattr);
		if (rc) {
			kfree(ea_buf->xattr);
			ea_buf->xattr = NULL;
			return rc;
		}
		goto size_check;
	}
	blocks_needed = (min_size + sb->s_blocksize - 1) >>
	    sb->s_blocksize_bits;

	if (blocks_needed > current_blocks) {
		rc = dbAlloc(inode, INOHINT(inode), (s64) blocks_needed,
			     &blkno);
		if (rc)
			return -rc;

		DXDlength(&ea_buf->new_ea, blocks_needed);
		DXDaddress(&ea_buf->new_ea, blkno);
		ea_buf->new_ea.flag = DXD_EXTENT;
		DXDsize(&ea_buf->new_ea, min_size);

		ea_buf->flag = EA_EXTENT | EA_NEW;

		ea_buf->mp = get_metapage(inode, blkno,
					  blocks_needed << sb->s_blocksize_bits,
					  1);
		if (ea_buf->mp == NULL) {
			dbFree(inode, blkno, (s64) blocks_needed);
			return -EIO;
		}
		ea_buf->xattr = ea_buf->mp->data;
		ea_buf->max_size = (min_size + sb->s_blocksize - 1) &
		    ~(sb->s_blocksize - 1);
		if (ea_size == 0)
			return 0;
		rc = jfs_ReadEA(inode, ea_buf->xattr);
		if (rc) {
			discard_metapage(ea_buf->mp);
			dbFree(inode, blkno, (s64) blocks_needed);
			return rc;
		}
		goto size_check;
	}
	ea_buf->flag = EA_EXTENT;
	ea_buf->mp = read_metapage(inode, addressDXD(&ji->ea),
				   lengthDXD(&ji->ea), 1);
	if (ea_buf->mp == NULL)
		return -EIO;
	ea_buf->xattr = ea_buf->mp->data;
	ea_buf->max_size = (ea_size + sb->s_blocksize - 1) &
	    ~(sb->s_blocksize - 1);

      size_check:
	if (EALIST_SIZE(ea_buf->xattr) != ea_size) {
		printk(KERN_ERR "ea_get: invalid extended attribute\n");
		dump_mem("xattr", ea_buf->xattr, ea_size);
		ea_release(inode, ea_buf);
		return -EIO;
	}

	return ea_size;
}

void ea_release(struct inode *inode, struct ea_buffer *ea_buf)
{
	if (ea_buf->flag & EA_MALLOC)
		kfree(ea_buf->xattr);
	else if (ea_buf->flag & EA_EXTENT) {
		assert(ea_buf->mp);
		release_metapage(ea_buf->mp);

		if (ea_buf->flag & EA_NEW)
			dbFree(inode, addressDXD(&ea_buf->new_ea),
			       lengthDXD(&ea_buf->new_ea));
	}
}

int ea_put(struct inode *inode, struct ea_buffer *ea_buf, int new_size)
{
	struct jfs_inode_info *ji = JFS_IP(inode);
	unsigned long old_blocks, new_blocks;
	int rc = 0;
	tid_t tid;

	if (new_size == 0) {
		ea_release(inode, ea_buf);
		ea_buf = 0;
	} else if (ea_buf->flag & EA_INLINE) {
		assert(new_size <= sizeof (ji->i_inline_ea));
		ji->mode2 &= ~INLINEEA;
		ea_buf->new_ea.flag = DXD_INLINE;
		DXDsize(&ea_buf->new_ea, new_size);
		DXDaddress(&ea_buf->new_ea, 0);
		DXDlength(&ea_buf->new_ea, 0);
	} else if (ea_buf->flag & EA_MALLOC) {
		rc = jfs_WriteEA(inode, ea_buf->xattr, new_size,
				 &ea_buf->new_ea);
		kfree(ea_buf->xattr);
	} else if (ea_buf->flag & EA_NEW) {
		/* We have already allocated a new dxd */
		flush_metapage(ea_buf->mp);
	} else {
		/* ->xattr must point to original ea's metapage */
		rc = jfs_WriteEA(inode, ea_buf->xattr, new_size,
				 &ea_buf->new_ea);
		discard_metapage(ea_buf->mp);
	}
	if (rc)
		return rc;

	tid = txBegin(inode->i_sb, 0);
	down(&ji->commit_sem);

	old_blocks = new_blocks = 0;

	if (ji->ea.flag & DXD_EXTENT) {
		invalidate_dxd_metapages(inode, ji->ea);
		old_blocks = lengthDXD(&ji->ea);
	}

	if (ea_buf) {
		txEA(tid, inode, &ji->ea, &ea_buf->new_ea);
		if (ea_buf->new_ea.flag & DXD_EXTENT) {
			new_blocks = lengthDXD(&ea_buf->new_ea);
			if (ji->ea.flag & DXD_INLINE)
				ji->mode2 |= INLINEEA;
		}
		ji->ea = ea_buf->new_ea;
	} else {
		txEA(tid, inode, &ji->ea, 0);
		if (ji->ea.flag & DXD_INLINE)
			ji->mode2 |= INLINEEA;
		ji->ea.flag = 0;
		ji->ea.size = 0;
	}

	inode->i_blocks += LBLK2PBLK(inode->i_sb, new_blocks - old_blocks);
	rc = txCommit(tid, 1, &inode, 0);
	txEnd(tid);
	up(&ji->commit_sem);

	return rc;
}

int jfs_setxattr(struct dentry *dentry, const char *name,
		 void *value, size_t value_len, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct jfs_ea_list *ealist;
	struct jfs_ea *ea, *old_ea = NULL, *next_ea = NULL;
	struct ea_buffer ea_buf;
	int old_ea_size = 0;
	int xattr_size;
	int new_size;
	int namelen = strlen(name);
	int found = 0;
	int rc = 0;
	int length;

	if (!value)
		value_len = 0;

	IWRITE_LOCK(inode);

	xattr_size = ea_get(inode, &ea_buf, 0);
	if (xattr_size < 0) {
		rc = xattr_size;
		goto unlock;
	}

      again:
	ealist = (struct jfs_ea_list *) ea_buf.xattr;
	new_size = sizeof (struct jfs_ea_list);

	if (xattr_size) {
		for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist);
		     ea = NEXT_EA(ea)) {
			if ((namelen == ea->namelen) &&
			    (memcmp(name, ea->name, namelen) == 0)) {
				found = 1;
				if (flags & XATTR_CREATE) {
					rc = -EEXIST;
					goto release;
				}
				old_ea = ea;
				old_ea_size = EA_SIZE(ea);
				next_ea = NEXT_EA(ea);
			} else
				new_size += EA_SIZE(ea);
		}
	}

	if (!found) {
		if (flags & XATTR_REPLACE) {
			rc = -ENODATA;
			goto release;
		}
		if (value_len == 0) {
			rc = 0;
			goto release;
		}
	}
	if (value_len)
		new_size += sizeof (struct jfs_ea) + namelen + 1 + value_len;

	if (new_size > ea_buf.max_size) {
		/*
		 * We need to allocate more space for merged ea list.
		 * We should only have loop to again: once.
		 */
		ea_release(inode, &ea_buf);
		xattr_size = ea_get(inode, &ea_buf, new_size);
		if (xattr_size < 0) {
			rc = xattr_size;
			goto unlock;
		}
		goto again;
	}

	/* Remove old ea of the same name */
	if (found) {
		/* number of bytes following target EA */
		length = (char *) END_EALIST(ealist) - (char *) next_ea;
		if (length > 0)
			memmove(old_ea, next_ea, length);
		xattr_size -= old_ea_size;
	}

	/* Add new entry to the end */
	if (value_len) {
		if (xattr_size == 0)
			/* Completely new ea list */
			xattr_size = sizeof (struct jfs_ea_list);

		ea = (struct jfs_ea *) ((char *) ealist + xattr_size);
		ea->flag = 0;
		ea->namelen = namelen;
		ea->valuelen = (cpu_to_le16(value_len));
		memcpy(ea->name, name, namelen);
		ea->name[namelen] = 0;
		memcpy(&ea->name[namelen + 1], value, value_len);
		xattr_size += EA_SIZE(ea);
	}

	/* DEBUG - If we did this right, these number match */
	if (xattr_size != new_size) {
		printk(KERN_ERR
		       "jfs_xsetattr: xattr_size = %d, new_size = %d\n",
		       xattr_size, new_size);

		rc = EINVAL;
		goto release;
	}

	ealist->size = cpu_to_le32(new_size);

	rc = ea_put(inode, &ea_buf, new_size);

	goto unlock;
      release:
	ea_release(inode, &ea_buf);
      unlock:
	IWRITE_UNLOCK(inode);
	return rc;
}

ssize_t jfs_getxattr(struct dentry * dentry, const char *name,
		     void *data, size_t buf_size)
{
	struct inode *inode = dentry->d_inode;
	struct jfs_ea_list *ealist;
	struct jfs_ea *ea;
	struct ea_buffer ea_buf;
	int xattr_size;
	ssize_t size;
	int namelen = strlen(name);
	char *value;

	IREAD_LOCK(inode);

	xattr_size = ea_get(inode, &ea_buf, 0);
	if (xattr_size < 0) {
		size = xattr_size;
		goto unlock;
	}

	if (xattr_size == 0) {
		size = 0;
		goto release;
	}

	ealist = (struct jfs_ea_list *) ea_buf.xattr;

	/* Find the named attribute */
	for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist); ea = NEXT_EA(ea))
		if ((namelen == ea->namelen) &&
		    memcmp(name, ea->name, namelen) == 0) {
			/* Found it */
			size = le16_to_cpu(ea->valuelen);
			if (!data)
				goto release;
			else if (size > buf_size) {
				size = -ERANGE;
				goto release;
			}
			value = ((char *) &ea->name) + ea->namelen + 1;
			memcpy(data, value, size);
			goto release;
		}
	/* not found */
	size = -ENODATA;
      release:
	ea_release(inode, &ea_buf);
      unlock:
	IREAD_UNLOCK(inode);

	return size;
}

ssize_t jfs_listxattr(struct dentry * dentry, char *data, size_t buf_size)
{
	struct inode *inode = dentry->d_inode;
	char *buffer;
	ssize_t size = 0;
	int xattr_size;
	struct jfs_ea_list *ealist;
	struct jfs_ea *ea;
	struct ea_buffer ea_buf;

	IREAD_LOCK(inode);

	xattr_size = ea_get(inode, &ea_buf, 0);
	if (xattr_size < 0) {
		size = xattr_size;
		goto unlock;
	}

	if (xattr_size == 0)
		goto release;

	ealist = (struct jfs_ea_list *) ea_buf.xattr;

	/* compute required size of list */
	for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist); ea = NEXT_EA(ea))
		size += ea->namelen + 1;

	if (!data)
		goto release;

	if (size > buf_size) {
		size = -ERANGE;
		goto release;
	}

	/* Copy attribute names to buffer */
	buffer = data;
	for (ea = FIRST_EA(ealist); ea < END_EALIST(ealist); ea = NEXT_EA(ea)) {
		memcpy(buffer, ea->name, ea->namelen);
		buffer[ea->namelen] = 0;
		buffer += ea->namelen + 1;
	}

      release:
	ea_release(inode, &ea_buf);
      unlock:
	IREAD_UNLOCK(inode);
	return size;
}

int jfs_removexattr(struct dentry *dentry, const char *name)
{
	return jfs_setxattr(dentry, name, 0, 0, XATTR_REPLACE);
}
