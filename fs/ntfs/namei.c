/*
 * namei.c - NTFS kernel directory inode operations. Part of the Linux-NTFS
 * 	     project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ntfs.h"

/**
 * ntfs_lookup - find the inode represented by a dentry in a directory inode
 * @dir_ino:	directory inode in which to look for the inode
 * @dent:	dentry representing the inode to look for
 *
 * In short, ntfs_lookup() looks for the inode represented by the dentry @dent
 * in the directory inode @dir_ino and if found attaches the inode to the
 * dentry @dent.
 *
 * In more detail, the dentry @dent specifies which inode to look for by
 * supplying the name of the inode in @dent->d_name.name. ntfs_lookup()
 * converts the name to Unicode and walks the contents of the directory inode
 * @dir_ino looking for the converted Unicode name. If the name is found in the
 * directory, the corresponding inode is loaded by calling iget() on its inode
 * number and the inode is associated with the dentry @dent via a call to
 * d_add().
 *
 * If the name is not found in the directory, a NULL inode is inserted into the
 * dentry @dent. The dentry is then termed a negative dentry.
 *
 * Only if an actual error occurs, do we return an error via ERR_PTR().
 */
static struct dentry *ntfs_lookup(struct inode *dir_ino, struct dentry *dent)
{
	ntfs_volume *vol = NTFS_SB(dir_ino->i_sb);
	struct inode *dent_inode;
	u64 mref;
	unsigned long dent_ino;
	uchar_t *uname;
	int uname_len;

	ntfs_debug("Looking up %s in directory inode 0x%lx.",
			dent->d_name.name, dir_ino->i_ino);
	/* Convert the name of the dentry to Unicode. */
	uname_len = ntfs_nlstoucs(vol, dent->d_name.name, dent->d_name.len,
			&uname);
	if (uname_len < 0) {
		ntfs_error(vol->sb, "Failed to convert name to Unicode.");
		return ERR_PTR(uname_len);
	}
	mref = ntfs_lookup_inode_by_name(NTFS_I(dir_ino), uname, uname_len);
	kmem_cache_free(ntfs_name_cache, uname);
	if (!IS_ERR_MREF(mref)) {
		dent_ino = (unsigned long)MREF(mref);
		ntfs_debug("Found inode 0x%lx. Calling iget.", dent_ino);
		dent_inode = iget(vol->sb, dent_ino);
		if (dent_inode) {
			/* Consistency check. */
			if (MSEQNO(mref) == NTFS_I(dent_inode)->seq_no ||
					dent_ino == FILE_MFT) {
				d_add(dent, dent_inode);
				ntfs_debug("Done.");
				return NULL;
			}
			ntfs_error(vol->sb, "Found stale reference to inode "
					"0x%Lx (reference sequence number = "
					"0x%x, inode sequence number = 0x%x, "
					"returning -EACCES. Run chkdsk.",
					(unsigned long long)MREF(mref),
					MSEQNO(mref),
					NTFS_I(dent_inode)->seq_no);
			iput(dent_inode);
		} else
			ntfs_error(vol->sb, "iget(0x%Lx) failed, returning "
					"-EACCES.",
					(unsigned long long)MREF(mref));
		return ERR_PTR(-EACCES);
	}
	if (MREF_ERR(mref) == -ENOENT) {
		ntfs_debug("Entry was not found, adding negative dentry.");
		/* The dcache will handle negative entries. */
		d_add(dent, NULL);
		ntfs_debug("Done.");
		return NULL;
	}
	ntfs_error(vol->sb, "ntfs_lookup_ino_by_name() failed with error "
			"code %i.", -MREF_ERR(mref));
	return ERR_PTR(MREF_ERR(mref));
}

struct inode_operations ntfs_dir_inode_ops = {
	lookup:		ntfs_lookup,	/* lookup directory. */
};

