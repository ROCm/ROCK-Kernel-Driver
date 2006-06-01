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
#define POS_SD_FILE_MIN			0
#define POS_SD_MAY_EXEC			POS_SD_FILE_MIN
#define POS_SD_MAY_WRITE		(POS_SD_MAY_EXEC + 1)
#define POS_SD_MAY_READ			(POS_SD_MAY_WRITE + 1)
/* not used by Subdomain */
#define POS_SD_MAY_APPEND		(POS_SD_MAY_READ + 1)
/* end of system offsets */

#define POS_SD_MAY_LINK			(POS_SD_MAY_APPEND + 1)
#define POS_SD_EXEC_INHERIT		(POS_SD_MAY_LINK + 1)
#define POS_SD_EXEC_UNCONSTRAINED	(POS_SD_EXEC_INHERIT + 1)
#define POS_SD_EXEC_PROFILE		(POS_SD_EXEC_UNCONSTRAINED + 1)
#define POS_SD_EXEC_MMAP		(POS_SD_EXEC_PROFILE + 1)
#define POS_SD_EXEC_UNSAFE		(POS_SD_EXEC_MMAP + 1)
#define POS_SD_FILE_MAX			POS_SD_EXEC_UNSAFE

/* Modeled after MAY_READ, MAY_WRITE, MAY_EXEC def'ns */
#define SD_MAY_EXEC			(0x01 << POS_SD_MAY_EXEC)
#define SD_MAY_WRITE			(0x01 << POS_SD_MAY_WRITE)
#define SD_MAY_READ			(0x01 << POS_SD_MAY_READ)
#define SD_MAY_LINK			(0x01 << POS_SD_MAY_LINK)
#define SD_EXEC_INHERIT			(0x01 << POS_SD_EXEC_INHERIT)
#define SD_EXEC_UNCONSTRAINED		(0x01 << POS_SD_EXEC_UNCONSTRAINED)
#define SD_EXEC_PROFILE			(0x01 << POS_SD_EXEC_PROFILE)
#define SD_EXEC_MMAP			(0x01 << POS_SD_EXEC_MMAP)
#define SD_EXEC_UNSAFE			(0x01 << POS_SD_EXEC_UNSAFE)

#define SD_EXEC_MODIFIERS		(SD_EXEC_INHERIT | \
					 SD_EXEC_UNCONSTRAINED | \
					 SD_EXEC_PROFILE)

#endif /* _SHARED_H */
