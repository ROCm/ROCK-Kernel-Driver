/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		MIPS specific IP/TCP/UDP checksumming routines
 *
 * Authors:	Ralf Baechle, <ralf@waldorf-gmbh.de>
 *		Lots of code moved from tcp.c and ip.c; see those files
 *		for more names.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/types.h>

#include <net/checksum.h>
#include <asm/byteorder.h>
#include <asm/string.h>
#include <asm/uaccess.h>

/*
 * Copy while checksumming, otherwise like csum_partial
 */
unsigned int csum_partial_copy_nocheck (const char *src, char *dst,
                                        int len, unsigned int sum)
{
	sum = csum_partial(src, len, sum);
	memcpy(dst, src, len);

	return sum;
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);

/*
 * Copy from userspace and compute checksum.  If we catch an exception
 * then zero the rest of the buffer.
 */
unsigned int csum_partial_copy_from_user (const char __user *src, char *dst,
                                          int len, unsigned int sum,
                                          int *err_ptr)
{
	int missing;

	missing = copy_from_user(dst, src, len);
	if (missing) {
		memset(dst + len - missing, 0, missing);
		*err_ptr = -EFAULT;
	}

	return csum_partial(dst, len-missing, sum);
}
EXPORT_SYMBOL(csum_partial_copy_from_user);
