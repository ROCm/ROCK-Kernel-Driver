/* Copyright 2002 Andi Kleen, SuSE Labs.
 * Subject to the GNU Public License v.2
 * 
 * Wrappers of assembly checksum functions for x86-64.
 */

#include <asm/checksum.h>
#include <linux/module.h>

/* Better way for this sought */
static inline unsigned from64to32(unsigned long x)
{
	/* add up 32-bit words for 33 bits */
        x = (x & 0xffffffff) + (x >> 32);
        /* add up 16-bit and 17-bit words for 17+c bits */
        x = (x & 0xffff) + (x >> 16);
        /* add up 16-bit and 2-bit for 16+c bit */
        x = (x & 0xffff) + (x >> 16);
        return x;
}

/** 
 * csum_partial_copy_from_user - Copy and checksum from user space. 
 * @src: source address (user space) 
 * @dst: destination address
 * @len: number of bytes to be copied.
 * @isum: initial sum that is added into the result (32bit unfolded)
 * @errp: set to -EFAULT for an bad source address.
 * 
 * Returns an 32bit unfolded checksum of the buffer.
 * src and dst are best aligned to 64bits. 
 */ 
unsigned int 
csum_partial_copy_from_user(const char *src, char *dst, 
			    int len, unsigned int isum, int *errp)
{ 
	*errp = 0;
	if (likely(access_ok(VERIFY_READ,src, len))) { 
		unsigned long sum; 
		sum = csum_partial_copy_generic(src,dst,len,isum,errp,NULL);
		if (likely(*errp == 0)) 
			return from64to32(sum); 
	} 
	*errp = -EFAULT;
	memset(dst,0,len); 
	return 0;		
} 

EXPORT_SYMBOL(csum_partial_copy_from_user);

/** 
 * csum_partial_copy_to_user - Copy and checksum to user space. 
 * @src: source address
 * @dst: destination address (user space)
 * @len: number of bytes to be copied.
 * @isum: initial sum that is added into the result (32bit unfolded)
 * @errp: set to -EFAULT for an bad destination address.
 * 
 * Returns an 32bit unfolded checksum of the buffer.
 * src and dst are best aligned to 64bits.
 */ 
unsigned int 
csum_partial_copy_to_user(const char *src, char *dst, 
			  int len, unsigned int isum, int *errp)
{ 
	if (unlikely(!access_ok(VERIFY_WRITE, dst, len))) {
		*errp = -EFAULT;
		return 0; 
	}
	*errp = 0;
	return from64to32(csum_partial_copy_generic(src,dst,len,isum,NULL,errp)); 
} 

EXPORT_SYMBOL(csum_partial_copy_to_user);

/** 
 * csum_partial_copy_nocheck - Copy and checksum.
 * @src: source address
 * @dst: destination address
 * @len: number of bytes to be copied.
 * @isum: initial sum that is added into the result (32bit unfolded)
 * 
 * Returns an 32bit unfolded checksum of the buffer.
 */ 
unsigned int 
csum_partial_copy_nocheck(const char *src, char *dst, int len, unsigned int sum)
{ 
	return from64to32(csum_partial_copy_generic(src,dst,len,sum,NULL,NULL));
} 

//EXPORT_SYMBOL(csum_partial_copy_nocheck);

unsigned short csum_ipv6_magic(struct in6_addr *saddr, struct in6_addr *daddr,
			       __u32 len, unsigned short proto, unsigned int sum) 
{
	__u64 rest, sum64;
     
	rest = (__u64)htonl(len) + (__u64)htons(proto) + (__u64)sum;
	asm("  addq (%[saddr]),%[sum]\n"
	    "  adcq 8(%[saddr]),%[sum]\n"
	    "  adcq (%[daddr]),%[sum]\n" 
	    "  adcq 8(%[daddr]),%[sum]\n"
	    "  adcq $0,%[sum]\n"
	    : [sum] "=r" (sum64) 
	    : "[sum]" (rest),[saddr] "r" (saddr), [daddr] "r" (daddr));
	return csum_fold(from64to32(sum64)); 
}

EXPORT_SYMBOL(csum_ipv6_magic);
