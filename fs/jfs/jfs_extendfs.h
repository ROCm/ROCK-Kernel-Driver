/*
 *   Copyright (c) International Business Machines Corp., 2000-2001
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
#ifndef	_H_JFS_EXTENDFS
#define _H_JFS_EXTENDFS

/*
 *	extendfs parameter list
 */
typedef struct {
	u32 flag;		/* 4: */
	u8 dev;			/* 1: */
	u8 pad[3];		/* 3: */
	s64 LVSize;		/* 8: LV size in LV block */
	s64 FSSize;		/* 8: FS size in LV block */
	s32 LogSize;		/* 4: inlinelog size in LV block */
} extendfs_t;			/* (28) */

/* plist flag */
#define EXTENDFS_QUERY		0x00000001

#endif				/* _H_JFS_EXTENDFS */
