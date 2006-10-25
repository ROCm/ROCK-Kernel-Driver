/*
 *	Copyright (C) 2000, 2001, 2004, 2005 Novell/SUSE
 *
 *	Immunix AppArmor LSM
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 */

#ifndef _SHARED_H
#define _SHARED_H

/* start of system offsets */
#define POS_AA_FILE_MIN			0
#define POS_AA_MAY_EXEC			POS_AA_FILE_MIN
#define POS_AA_MAY_WRITE		(POS_AA_MAY_EXEC + 1)
#define POS_AA_MAY_READ			(POS_AA_MAY_WRITE + 1)
#define POS_AA_MAY_APPEND		(POS_AA_MAY_READ + 1)
/* end of system offsets */

#define POS_AA_MAY_LINK			(POS_AA_MAY_APPEND + 1)
#define POS_AA_EXEC_INHERIT		(POS_AA_MAY_LINK + 1)
#define POS_AA_EXEC_UNCONSTRAINED	(POS_AA_EXEC_INHERIT + 1)
#define POS_AA_EXEC_PROFILE		(POS_AA_EXEC_UNCONSTRAINED + 1)
#define POS_AA_EXEC_MMAP		(POS_AA_EXEC_PROFILE + 1)
#define POS_AA_EXEC_UNSAFE		(POS_AA_EXEC_MMAP + 1)
#define POS_AA_FILE_MAX			POS_AA_EXEC_UNSAFE

/* Modeled after MAY_READ, MAY_WRITE, MAY_EXEC def'ns */
#define AA_MAY_EXEC			(0x01 << POS_AA_MAY_EXEC)
#define AA_MAY_WRITE			(0x01 << POS_AA_MAY_WRITE)
#define AA_MAY_READ			(0x01 << POS_AA_MAY_READ)
#define AA_MAY_LINK			(0x01 << POS_AA_MAY_LINK)
#define AA_EXEC_INHERIT			(0x01 << POS_AA_EXEC_INHERIT)
#define AA_EXEC_UNCONSTRAINED		(0x01 << POS_AA_EXEC_UNCONSTRAINED)
#define AA_EXEC_PROFILE			(0x01 << POS_AA_EXEC_PROFILE)
#define AA_EXEC_MMAP			(0x01 << POS_AA_EXEC_MMAP)
#define AA_EXEC_UNSAFE			(0x01 << POS_AA_EXEC_UNSAFE)

#define AA_EXEC_MODIFIERS		(AA_EXEC_INHERIT | \
					 AA_EXEC_UNCONSTRAINED | \
					 AA_EXEC_PROFILE)

#endif /* _SHARED_H */
