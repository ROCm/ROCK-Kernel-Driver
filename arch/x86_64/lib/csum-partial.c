/*
 * arch/x86_64/lib/csum-partial.c
 *
 * This file contains network checksum routines that are better done
 * in an architecture-specific manner due to speed.
 */
 
#include <linux/compiler.h>
#include <linux/module.h>

/* Better way for this sought */
static inline unsigned short from64to16(unsigned long x)
{
	/* add up 32-bit words for 33 bits */
	x = (x & 0xffffffff) + (x >> 32);
	/* add up 16-bit and 17-bit words for 17+c bits */
	x = (x & 0xffff) + (x >> 16);
	/* add up 16-bit and 2-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

/*
 * Do a 64-bit checksum on an arbitrary memory area.
 * Returns a 32bit checksum.
 *
 * This isn't a great routine, but it's not _horrible_ either. 
 * We rely on the compiler to unroll.
 */
static inline unsigned do_csum(const unsigned char * buff, int len)
{
	int odd, count;
	unsigned long result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long) buff;
	if (unlikely(odd)) {
		result = *buff << 8;
		len--;
		buff++;
	}
	count = len >> 1;		/* nr of 16-bit words.. */
	if (count) {
		if (2 & (unsigned long) buff) {
			result += *(unsigned short *) buff;
			count--;
			len -= 2;
			buff += 2;
		}
		count >>= 1;		/* nr of 32-bit words.. */
		if (count) {
			if (4 & (unsigned long) buff) {
				result += *(unsigned int *) buff;
				count--;
				len -= 4;
				buff += 4;
			}
			count >>= 1;	/* nr of 64-bit words.. */
			if (count) {
				unsigned long zero = 0; 
				do {
					asm("  addq %1,%0\n"
					    "  adcq %2,%0\n" 
					    : "=r" (result)
					    : "m"  (*buff), "r" (zero),  "0" (result));
					count--;
					buff += 8;
				} while (count);
				result = (result & 0xffffffff) + (result >> 32);
			}
			if (len & 4) {
				result += *(unsigned int *) buff;
				buff += 4;
			}
		}
		if (len & 2) {
			result += *(unsigned short *) buff;
			buff += 2;
		}
	}
	if (len & 1)
		result += *buff;
	result = from64to16(result);
	if (unlikely(odd))
		return ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
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
 * it's best to have buff aligned on a 64-bit boundary
 */
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum)
{
	unsigned result = do_csum(buff, len);

	/* add in old sum, and carry.. */
	asm("addl %1,%0\n\t"
	    "adcl $0,%0" : "=r" (result) : "r" (sum), "0" (result)); 
	return result;
}

//EXPORT_SYMBOL(csum_partial);

/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
unsigned short ip_compute_csum(unsigned char * buff, int len)
{
	return ~csum_partial(buff,len,0); 
}

EXPORT_SYMBOL(ip_compute_csum);
