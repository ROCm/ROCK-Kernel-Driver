#ifndef _X86_64_CHECKSUM_H
#define _X86_64_CHECKSUM_H


/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl) {
	unsigned int sum;

	__asm__ __volatile__(
"\n	    movl (%1), %0"
"\n	    subl $4, %2"
"\n	    jbe 2f"
"\n	    addl 4(%1), %0"
"\n	    adcl 8(%1), %0"
"\n	    adcl 12(%1), %0"
"\n1:	    adcl 16(%1), %0"
"\n	    lea 4(%1), %1"
"\n	    decl %2"
"\n	    jne	1b"
"\n	    adcl $0, %0"
"\n	    movl %0, %2"
"\n	    shrl $16, %0"
"\n	    addw %w2, %w0"
"\n	    adcl $0, %0"
"\n	    notl %0"
"\n2:"
	/* Since the input registers which are loaded with iph and ipl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
	: "=r" (sum), "=r" (iph), "=r" (ihl)
	: "1" (iph), "2" (ihl));
	return(sum);
}



/*
 *	Fold a partial checksum. Note this works on a 32bit unfolded checksum. Make sure
 *	to not mix with 64bit checksums!
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	__asm__(
"\n		addl %1,%0"
"\n		adcl $0xffff,%0"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum & 0xffff0000)
	);
	return (~sum) >> 16;
}




static inline unsigned long csum_tcpudp_nofold(unsigned saddr,
						   unsigned daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum) 
{
    __asm__(
"\n	addl %1, %0"
"\n	adcl %2, %0"
"\n	adcl %3, %0"
"\n	adcl $0, %0"
	: "=r" (sum)
	: "g" (daddr), "g"(saddr), "g"((ntohs(len)<<16)+proto*256), "0"(sum));
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
	return csum_fold(csum_tcpudp_nofold(saddr,daddr,len,proto,sum));
}


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
extern unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
unsigned int csum_partial_copy(const char *src, char *dst, int len, unsigned int sum);

/*
 * this is a new version of the above that records errors it finds in *errp,
 * but continues and zeros the rest of the buffer.
 */
unsigned int csum_partial_copy_from_user(const char *src, char *dst, int len, unsigned int sum, int *errp);

unsigned int csum_partial_copy_nocheck(const char *src, char *dst, int len, unsigned int sum);


/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

extern unsigned short ip_compute_csum(unsigned char * buff, int len);

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u32 len,
						     unsigned short proto,
						     unsigned int sum) 
{
	__asm__(
"\n		addl 0(%1), %0"
"\n		adcl 4(%1), %0"
"\n		adcl 8(%1), %0"
"\n		adcl 12(%1), %0"
"\n		adcl 0(%2), %0"
"\n		adcl 4(%2), %0"
"\n		adcl 8(%2), %0"
"\n		adcl 12(%2), %0"
"\n		adcl %3, %0"
"\n		adcl %4, %0"
"\n		adcl $0, %0"
		: "=&r" (sum)
		: "r" (saddr), "r" (daddr), 
		  "r"(htonl(len)), "r"(htonl(proto)), "0"(sum));

	return csum_fold(sum);
}

#endif
