/*
 * file.c - NTFS kernel file operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001 Anton Altaparmakov.
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

struct file_operations ntfs_file_ops = {
	llseek:			generic_file_llseek,	/* Seek inside file. */
	read:			generic_file_read,	/* Read from file. */
	mmap:			generic_file_mmap,	/* Mmap file. */
	open:			generic_file_open,	/* Open file. */
};

struct inode_operations ntfs_file_inode_ops = {};

struct file_operations ntfs_empty_file_ops = {};

struct inode_operations ntfs_empty_inode_ops = {};

