/*
 *      MIPL Mobile IPv6 Utility functions
 *
 *      $Id: s.util.h 1.16 03/09/26 00:30:23+03:00 vnuorval@cs78179138.pp.htv.fi $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <linux/in6.h>
#include <asm/byteorder.h>

/**
 * mipv6_prefix_compare - Compare two IPv6 prefixes
 * @addr: IPv6 address
 * @prefix: IPv6 address
 * @nprefix: number of bits to compare
 *
 * Perform prefix comparison bitwise for the @nprefix first bits
 * Returns 1, if the prefixes are the same, 0 otherwise 
 **/
static inline int mipv6_prefix_compare(const struct in6_addr *addr,
				       const struct in6_addr *prefix, 
				       const unsigned int pfix_len)
{
	int i;
	unsigned int nprefix = pfix_len;

	if (nprefix > 128)
		return 0;

	for (i = 0; nprefix > 0; nprefix -= 32, i++) {
		if (nprefix >= 32) {
			if (addr->s6_addr32[i] != prefix->s6_addr32[i])
				return 0;
		} else {
			if (((addr->s6_addr32[i] ^ prefix->s6_addr32[i]) &
			     ((~0) << (32 - nprefix))) != 0)
				return 0;
			return 1;
		}
	}

	return 1;
}

/**
 * homeagent_anycast - Compute Home Agent anycast address
 * @ac_addr: append home agent anycast suffix to passed prefix
 * @prefix: prefix ha anycast address is generated from
 * @plen: length of prefix in bits
 *
 * Calculate corresponding Home Agent Anycast Address (RFC2526) in a
 * given subnet.
 */
static inline int 
mipv6_ha_anycast(struct in6_addr *ac_addr, struct in6_addr *prefix, int plen)
{
	if (plen <= 0 || plen > 120)  {
		/* error, interface id should be minimum 8 bits */
		return -1;
	}
	ipv6_addr_copy(ac_addr, prefix);

	if (plen < 32)
		ac_addr->s6_addr32[0] |= htonl((u32)(~0) >> plen);
	if (plen < 64)
		ac_addr->s6_addr32[1] |= htonl((u32)(~0) >> (plen > 32 ? plen % 32 : 0));
	if (plen < 92)
		ac_addr->s6_addr32[2] |= htonl((u32)(~0) >> (plen > 64 ? plen % 32 : 0));
	if (plen <= 120)
		ac_addr->s6_addr32[3] |= htonl((u32)(~0) >> (plen > 92 ? plen % 32 : 0));

	/* RFC2526: for interface identifiers in EUI-64
	 * format, the universal/local bit in the interface
	 * identifier MUST be set to 0. */
	if (plen == 64) {
		ac_addr->s6_addr32[2] &= (int)htonl(0xfdffffff);
	}
	/* Mobile IPv6 Home-Agents anycast id (0x7e) */
	ac_addr->s6_addr32[3] &= (int)htonl(0xfffffffe);

	return 0;
}

#endif /* _UTIL_H */
