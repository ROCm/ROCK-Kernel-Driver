/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2001 Thiemo Seufer.
 */
#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

#include <asm/uaccess.h>

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
unsigned int csum_partial(const unsigned char *buff, int len, unsigned int sum);

/*
 * this is a new version of the above that records errors it finds in *errp,
 * but continues and zeros the rest of the buffer.
 */
#define csum_partial_copy_nocheck csum_partial_copy

/*
 * this is a new version of the above that records errors it finds in *errp,
 * but continues and zeros the rest of the buffer.
 */
unsigned int csum_partial_copy_from_user(const char *src, char *dst, int len,
                                         unsigned int sum, int *errp);

/*
 * Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
extern inline unsigned int csum_and_copy_to_user (const char *src, char *dst,
						  int len, int sum,
						  int *err_ptr)
{
	sum = csum_partial(src, len, sum);

	if (copy_to_user(dst, src, len)) {
		*err_ptr = -EFAULT;
		return -1;
	}

	return sum;
}

/*
 * the same as csum_partial, but copies from user space (but on MIPS
 * we have just one address space, so this is identical to the above)
 *
 * this is obsolete and will go away.
 */
#define csum_partial_copy_fromuser csum_partial_copy
unsigned int csum_partial_copy(const char *src, char *dst, int len,
			       unsigned int sum);

/*
 *	Fold a partial checksum without adding pseudo headers
 */
static inline unsigned short int csum_fold(unsigned int sum)
{
	__asm__(
	".set\tnoat\t\t\t# csum_fold\n\t"
	"sll\t$1,%0,16\n\t"
	"addu\t%0,$1\n\t"
	"sltu\t$1,%0,$1\n\t"
	"srl\t%0,%0,16\n\t"
	"addu\t%0,$1\n\t"
	"xori\t%0,0xffff\n\t"
	".set\tat"
	: "=r" (sum)
	: "0" (sum)
	: "$1");

 	return sum;
}
 
/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char *iph,
					  unsigned int ihl)
{
	unsigned int sum;
	unsigned long dummy;

	/*
	 * This is for 32-bit processors ...  but works just fine for 64-bit
	 * processors for now ...  XXX
	 */
	__asm__ __volatile__(
	".set\tnoreorder\t\t\t# ip_fast_csum\n\t"
	".set\tnoat\n\t"
	"lw\t%0, (%1)\n\t"
	"subu\t%2, 4\n\t"
	"dsll\t%2, 2\n\t"
	"lw\t%3, 4(%1)\n\t"
	"daddu\t%2, %1\n\t"
	"addu\t%0, %3\n\t"
	"sltu\t$1, %0, %3\n\t"
	"lw\t%3, 8(%1)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %3\n\t"
	"sltu\t$1, %0, %3\n\t"
	"lw\t%3, 12(%1)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %3\n\t"
	"sltu\t$1, %0, %3\n\t"
	"addu\t%0, $1\n"

	"1:\tlw\t%3, 16(%1)\n\t"
	"daddiu\t%1, 4\n"
	"addu\t%0, %3\n\t"
	"sltu\t$1, %0, %3\n\t"
	"bne\t%2, %1, 1b\n\t"
	" addu\t%0, $1\n"

	"2:\t.set\tat\n\t"
	".set\treorder"
	: "=&r" (sum), "=&r" (iph), "=&r" (ihl), "=&r" (dummy)
	: "1" (iph), "2" (ihl)
	: "$1");

	return csum_fold(sum);
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned long csum_tcpudp_nofold(unsigned long saddr,
					       unsigned long daddr,
					       unsigned short len,
					       unsigned short proto,
					       unsigned int sum)
{
	__asm__(
	".set\tnoat\t\t\t# csum_tcpudp_nofold\n\t"
	"daddu\t%0, %2\n\t"
	"daddu\t%0, %3\n\t"
	"daddu\t%0, %4\n\t"
	"dsll32\t$1, %0, 0\n\t"
	"daddu\t%0, $1\n\t"
	"dsrl32\t%0, %0, 0\n\t"
	".set\tat"
	: "=r" (sum)
	: "0" (daddr), "r"(saddr),
#ifdef __MIPSEL__
	  "r" ((ntohs(len)<<16)+proto*256),
#else
	  "r" (((proto)<<16)+len),
#endif
	  "r" (sum)
	: "$1");

	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static inline unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						 struct in6_addr *daddr,
						 __u32 len,
						 unsigned short proto,
						 unsigned int sum) 
{
	__asm__(
	".set\tnoreorder\t\t\t# csum_ipv6_magic\n\t"
	".set\tnoat\n\t"
	"addu\t%0, %5\t\t\t# proto (long in network byte order)\n\t"
	"sltu\t$1, %0, %5\n\t"
	"addu\t%0, $1\n\t"

	"addu\t%0, %6\t\t\t# csum\n\t"
	"sltu\t$1, %0, %6\n\t"
	"lw\t%1, 0(%2)\t\t\t# four words source address\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 4(%2)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 8(%2)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 12(%2)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 0(%3)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 4(%3)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 8(%3)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"

	"lw\t%1, 12(%3)\n\t"
	"addu\t%0, $1\n\t"
	"addu\t%0, %1\n\t"
	"sltu\t$1, %0, $1\n\t"
	".set\tnoat\n\t"
	".set\tnoreorder"
	: "=r" (sum), "=r" (proto)
	: "r" (saddr), "r" (daddr),
	  "0" (htonl(len)), "1" (htonl(proto)), "r" (sum)
	: "$1");

	return csum_fold(sum);
}

#endif /* _ASM_CHECKSUM_H */
