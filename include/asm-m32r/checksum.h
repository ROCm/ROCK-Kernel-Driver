#ifndef _ASM_M32R_CHECKSUM_H
#define _ASM_M32R_CHECKSUM_H

/* $Id$ */

/*
 *  linux/include/asm-m32r/atomic.h
 *    orig : i386 2.4.10
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hiroyuki Kondo, Hirokazu Takata
 */

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
asmlinkage unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums, and handles user-space pointer exceptions correctly, when needed.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

asmlinkage unsigned int csum_partial_copy_generic(const char *src, char *dst,
		int len, int sum, int *src_err_ptr, int *dst_err_ptr);

/*
 *	Note: when you get a NULL pointer exception here this means someone
 *	passed in an incorrect kernel address to one of these functions.
 *
 *	If you use these functions directly please don't forget the
 *	verify_area().
 */

extern unsigned int csum_partial_copy(const char *src, char *dst,
		int len, int sum);
extern unsigned int csum_partial_copy_generic_from(const char *src,
		char *dst, int len, unsigned int sum, int *err_ptr);
extern unsigned int csum_partial_copy_generic_to (const char *src,
		char *dst, int len, unsigned int sum, int *err_ptr);

extern __inline__
unsigned int csum_partial_copy_nocheck ( const char *src, char *dst,
					int len, int sum)
{
#if 0
	return csum_partial_copy_generic ( src, dst, len, sum, NULL, NULL);
#else
	return  csum_partial_copy( src, dst, len, sum);
#endif
}

extern __inline__
unsigned int csum_partial_copy_from_user ( const char __user *src, char *dst,
						int len, int sum, int *err_ptr)
{
#if 0
	return csum_partial_copy_generic ( src, dst, len, sum, err_ptr, NULL);
#else
	return csum_partial_copy_generic_from ( src, dst, len, sum, err_ptr);
#endif
}

/*
 * These are the old (and unsafe) way of doing checksums, a warning message will be
 * printed if they are used and an exeption occurs.
 *
 * these functions should go away after some time.
 */

#define csum_partial_copy_fromuser csum_partial_copy
unsigned int csum_partial_copy( const char *src, char *dst, int len, int sum);

/*
 *	Fold a partial checksum
 */

static __inline__ unsigned int csum_fold(unsigned int sum)
{
	unsigned long tmpreg;
	__asm__(
		"	sll3	%1, %0, #16 \n"
		"	cmp	%0, %0 \n"
		"	addx	%0, %1 \n"
		"	ldi	%1, #0 \n"
		"	srli	%0, #16 \n"
		"	addx	%0, %1 \n"
		"	xor3	%0, %0, #0x0000ffff \n"
		: "=r" (sum), "=&r" (tmpreg)
		: "0"  (sum)
		: "cbit"
	);
	return sum;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static __inline__ unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl) {
	unsigned long sum, tmpreg0, tmpreg1;

	__asm__ __volatile__(
		"	ld	%0, @%1+ \n"
		"	addi	%2, #-4 \n"
		"#	bgez	%2, 2f \n"
		"	cmp	%0, %0 \n"
		"	ld	%3, @%1+ \n"
		"	ld	%4, @%1+ \n"
		"	addx	%0, %3 \n"
		"	ld	%3, @%1+ \n"
		"	addx	%0, %4 \n"
		"	addx	%0, %3 \n"
		"	.fillinsn\n"
		"1: \n"
		"	ld	%4, @%1+ \n"
		"	addi	%2, #-1 \n"
		"	addx	%0, %4 \n"
		"	bgtz	%2, 1b \n"
		"\n"
		"	ldi	%3, #0 \n"
		"	addx	%0, %3 \n"
		"	.fillinsn\n"
		"2: \n"
	/* Since the input registers which are loaded with iph and ipl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
	: "=&r" (sum), "=r" (iph), "=r" (ihl), "=&r" (tmpreg0), "=&r" (tmpreg1)
	: "1" (iph), "2" (ihl)
	: "cbit", "memory");

	return csum_fold(sum);
}

static __inline__ unsigned long csum_tcpudp_nofold(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum)
{
#if defined(__LITTLE_ENDIAN)
	unsigned long len_proto = (ntohs(len)<<16)+proto*256;
#else
	unsigned long len_proto = (proto<<16)+len;
#endif
	unsigned long tmpreg;

	__asm__(
		"	cmp	%0, %0 \n"
		"	addx	%0, %2 \n"
		"	addx	%0, %3 \n"
		"	addx	%0, %4 \n"
		"	ldi	%1, #0 \n"
		"	addx	%0, %1 \n"
		: "=r" (sum), "=&r" (tmpreg)
		: "r" (daddr), "r" (saddr), "r" (len_proto), "0" (sum)
		: "cbit"
	);

	return sum;
}

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static __inline__ unsigned short int csum_tcpudp_magic(unsigned long saddr,
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

static __inline__ unsigned short ip_compute_csum(unsigned char * buff, int len) {
	return csum_fold (csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int sum)
{
	unsigned long tmpreg0, tmpreg1, tmpreg2, tmpreg3;
	__asm__(
		"	ld	%1, @(%5) \n"
		"	ld	%2, @(4,%5) \n"
		"	ld	%3, @(8,%5) \n"
		"	ld	%4, @(12,%5) \n"
		"	add	%0, %1 \n"
		"	addx	%0, %2 \n"
		"	addx	%0, %3 \n"
		"	addx	%0, %4 \n"
		"	ld	%1, @(%6) \n"
		"	ld	%2, @(4,%6) \n"
		"	ld	%3, @(8,%6) \n"
		"	ld	%4, @(12,%6) \n"
		"	addx	%0, %1 \n"
		"	addx	%0, %2 \n"
		"	addx	%0, %3 \n"
		"	addx	%0, %4 \n"
		"	addx	%0, %7 \n"
		"	addx	%0, %8 \n"
		"	ldi	%1, #0 \n"
		"	addx	%0, %1 \n"
		: "=&r" (sum), "=&r" (tmpreg0), "=&r" (tmpreg1),
		  "=&r" (tmpreg2), "=&r" (tmpreg3)
		: "r" (saddr), "r" (daddr),
		  "r" (htonl((__u32) (len))), "r" (htonl(proto)), "0" (sum)
		: "cbit"
        );

	return csum_fold(sum);
}

/*
 *	Copy and checksum to user
 */
#define HAVE_CSUM_COPY_USER
static __inline__ unsigned int csum_and_copy_to_user (const char *src, char *dst,
				    int len, int sum, int *err_ptr)
{
	if (access_ok(VERIFY_WRITE, dst, len))
		return csum_partial_copy_generic_to(src, dst, len, sum, err_ptr);

	if (len)
		*err_ptr = -EFAULT;

	return -1; /* invalid checksum */
}

#endif /* _ASM_M32R_CHECKSUM_H */
