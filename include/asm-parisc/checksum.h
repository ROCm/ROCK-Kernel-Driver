#ifndef _PARISC_CHECKSUM_H
#define _PARISC_CHECKSUM_H

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
extern unsigned int csum_partial(const unsigned char *, int, unsigned int);

/*
 * the same as csum_partial, but copies from src while it
 * checksums
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */
extern unsigned int csum_partial_copy(const char *, char *, int, unsigned int);

/*
 * the same as csum_partial, but copies from user space
 *
 * this is obsolete and will go away.
 */
#define csum_partial_copy_fromuser csum_partial_copy

/*
 * this is a new version of the above that records errors it finds in *errp,
 * but continues and zeros the rest of the buffer.
 */
unsigned int csum_partial_copy_from_user(const char *src, char *dst, int len, unsigned int sum, int *errp);

/*
 *	Note: when you get a NULL pointer exception here this means someone
 *	passed in an incorrect kernel address to one of these functions. 
 *	
 *	If you use these functions directly please don't forget the 
 *	verify_area().
 */
extern __inline__
unsigned int csum_partial_copy_nocheck (const char *src, char *dst,
					int len, int sum)
{
	return csum_partial_copy (src, dst, len, sum);
}

/*
 *	Optimized for IP headers, which always checksum on 4 octet boundaries.
 *
 *	Written by Randolph Chung <tausq@debian.org>
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl) {
	unsigned int sum;


	__asm__ __volatile__ ("
	ldws,ma		4(%1), %0
	addi		-4, %2, %2
	comib,>=	0, %2, 2f
	
	ldws,ma		4(%1), %%r19
	add		%0, %%r19, %0
	ldws,ma		4(%1), %%r19
	addc		%0, %%r19, %0
	ldws,ma		4(%1), %%r19
	addc		%0, %%r19, %0
1:	ldws,ma		4(%1), %%r19
	addib,<>	-1, %2, 1b
	addc		%0, %%r19, %0
	addc		%0, %%r0, %0

	zdepi		-1, 31, 16, %%r19
	and		%0, %%r19, %%r20
	extru		%0, 15, 16, %%r21
	add		%%r20, %%r21, %0
	and		%0, %%r19, %%r20
	extru		%0, 15, 16, %%r21
	add		%%r20, %%r21, %0
	subi		-1, %0, %0
2:
	"
	: "=r" (sum), "=r" (iph), "=r" (ihl)
	: "1" (iph), "2" (ihl)
	: "r19", "r20", "r21" );

	return(sum);
}

/*
 *	Fold a partial checksum
 */
static inline unsigned int csum_fold(unsigned int sum)
{
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}
 
static inline unsigned long csum_tcpudp_nofold(unsigned long saddr,
					       unsigned long daddr,
					       unsigned short len,
					       unsigned short proto,
					       unsigned int sum) 
{
	__asm__("
		add  %1, %0, %0
		addc %2, %0, %0
		addc %3, %0, %0
		addc %%r0, %0, %0 "
		: "=r" (sum)
		: "r" (daddr), "r"(saddr), "r"((proto<<16)+len), "0"(sum));
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
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline unsigned short ip_compute_csum(unsigned char * buf, int len) {
	 return csum_fold (csum_partial(buf, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int sum) 
{
	BUG();
	return csum_fold(sum);
}

/* 
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static __inline__ unsigned int csum_and_copy_to_user (const char *src, char *dst,
				    int len, int sum, int *err_ptr)
{
	/* code stolen from include/asm-mips64 */
	sum = csum_partial(src, len, sum);
	 
	if (copy_to_user(dst, src, len)) {
		*err_ptr = -EFAULT;
		return -1;
	}

	return sum;
}

#endif

