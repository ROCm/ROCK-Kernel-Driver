/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

#include <linux/types.h>
#include <linux/random.h>
#include <linux/time.h>

#ifdef __sparc__
#include <asm/idprom.h>
#else
#include <linux/netdevice.h>
#endif

#include <xfs_types.h>
#include <xfs_arch.h>
#include "time.h"
#include "move.h"
#include "uuid.h"

#ifndef CONFIG_NET
#define dev_get_by_name(x)	(NULL)
#define dev_put(x)		do { } while (0)
#endif

/* NODE_SIZE is the number of bytes used for the node identifier portion. */
#define NODE_SIZE	6

/*
 * Total size must be 128 bits.	 N.B. definition of uuid_t in uuid.h!
 */
typedef struct {
	u_int32_t	uu_timelow;	/* time "low" */
	u_int16_t	uu_timemid;	/* time "mid" */
	u_int16_t	uu_timehi;	/* time "hi" and version */
	u_int16_t	uu_clockseq;	/* "reserved" and clock sequence */
	u_int16_t	uu_node[NODE_SIZE / 2]; /* ethernet hardware address */
} uu_t;

/*
 * The Time Base Correction is the amount to add on to a UNIX-based
 * time value (i.e. seconds since 1 Jan. 1970) to convert it to the
 * time base for UUIDs (15 Oct. 1582).
 */
#define UUID_TBC	0x01B21DD2138140LL

static	short		uuid_eaddr[NODE_SIZE / 2];	/* ethernet address */
static	__int64_t	uuid_time;	/* last time basis used */
static	u_int16_t	uuid_clockseq;	/* boot-time randomizer */
DECLARE_MUTEX(uuid_lock);

/*
 * uuid_init - called from out of init_tbl[]
 */
void
uuid_init(void)
{
}

/*
 * uuid_getnodeuniq - obtain the node unique fields of a UUID.
 *
 * This is not in any way a standard or condoned UUID function;
 * it just something that's needed for user-level file handles.
 */
void
uuid_getnodeuniq(uuid_t *uuid, int fsid [2])
{
	char	*uu=(char*)uuid;

	/* on IRIX, this function assumes big-endian fields within
	 * the uuid, so we use INT_GET to get the same result on
	 * little-endian systems
	 */

	fsid[0] = (INT_GET(*(u_int16_t*)(uu+8), ARCH_CONVERT) << 16) +
		   INT_GET(*(u_int16_t*)(uu+4), ARCH_CONVERT);
	fsid[1] =  INT_GET(*(u_int32_t*)(uu  ), ARCH_CONVERT);
}

void
uuid_create_nil(uuid_t *uuid)
{
	bzero(uuid, sizeof *uuid);
}

int
uuid_is_nil(uuid_t *uuid)
{
	int	i;
	char	*cp = (char *)uuid;

	if (uuid == NULL)
		return B_TRUE;
	/* implied check of version number here... */
	for (i = 0; i < sizeof *uuid; i++)
		if (*cp++) return B_FALSE;	/* not nil */
	return B_TRUE;	/* is nil */
}

int
uuid_equal(uuid_t *uuid1, uuid_t *uuid2)
{
	return bcmp(uuid1, uuid2, sizeof(uuid_t)) ? B_FALSE : B_TRUE;
}

/*
 * Given a 128-bit uuid, return a 64-bit value by adding the top and bottom
 * 64-bit words.  NOTE: This function can not be changed EVER.	Although
 * brain-dead, some applications depend on this 64-bit value remaining
 * persistent.	Specifically, DMI vendors store the value as a persistent
 * filehandle.
 */
__uint64_t
uuid_hash64(uuid_t *uuid)
{
	__uint64_t	*sp = (__uint64_t *)uuid;

	return sp[0] + sp[1];
}	/* uuid_hash64 */

static void
get_eaddr(char *junk)
{
#ifdef __sparc__
	memcpy(uuid_eaddr, idprom->id_ethaddr, 6);
#else
	struct net_device *dev;

	dev = dev_get_by_name("eth0");
	if (!dev || !dev->addr_len) {
		get_random_bytes(uuid_eaddr, sizeof(uuid_eaddr));
	} else {
		memcpy(uuid_eaddr, dev->dev_addr,
			dev->addr_len<sizeof(uuid_eaddr)?
			dev->addr_len:sizeof(uuid_eaddr));
		dev_put(dev);
	}
#endif
}

/*
 * uuid_create - kernel version, does the actual work
 */
void
uuid_create(uuid_t *uuid)
{
	int		i;
	uu_t		*uu = (uu_t *)uuid;
	static int	uuid_have_eaddr = 0;	/* ethernet addr inited? */
	static int	uuid_is_init = 0;	/* time/clockseq inited? */

	down(&uuid_lock);
	if (!uuid_is_init) {
		timespec_t	ts;

		nanotime(&ts);
		/*
		 * The clock sequence must be initialized randomly.
		 */
		uuid_clockseq = ((unsigned long)jiffies & 0xfff) | 0x8000;
		/*
		 * Initialize the uuid time, it's in 100 nanosecond
		 * units since a time base in 1582.
		 */
		uuid_time = ts.tv_sec * 10000000LL +
			    ts.tv_nsec / 100LL +
			    UUID_TBC;
		uuid_is_init = 1;
	}
	if (!uuid_have_eaddr) {
		uuid_have_eaddr = 1;
		get_eaddr((char *)uuid_eaddr);
	}
	uuid_time++;
	uu->uu_timelow = (u_int32_t)(uuid_time & 0x00000000ffffffffLL);
	uu->uu_timemid = (u_int16_t)((uuid_time >> 32) & 0x0000ffff);
	uu->uu_timehi = (u_int16_t)((uuid_time >> 48) & 0x00000fff) | 0x1000;
	up(&uuid_lock);
	uu->uu_clockseq = uuid_clockseq;
	for (i = 0; i < (NODE_SIZE / 2); i++)
		uu->uu_node [i] = uuid_eaddr [i];
}

int
uuid_compare(uuid_t *uuid1, uuid_t *uuid2)
{
	int	i;
	char	*cp1 = (char *) uuid1;
	char	*cp2 = (char *) uuid2;

	if (uuid1 == NULL) {
		if (uuid2 == NULL) {
			return 0;	/* equal because both are nil */
		} else	{
			return -1;	/* uuid1 nil, so precedes uuid2 */
		}
	} else if (uuid2 == NULL) {
		return 1;
	}

	/* implied check of version number here... */
	for (i = 0; i < sizeof(uuid_t); i++) {
		if (*cp1 < *cp2)
			return -1;
		if (*cp1++ > *cp2++)
			return 1;
	}
	return 0;	/* they're equal */
}
