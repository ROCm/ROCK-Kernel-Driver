/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by reiser4/README */

#if !defined (__FS_REISER4_LIB_H__)
#define __FS_REISER4_LIB_H__

/* These 2 functions of 64 bit numbers division were taken from
   include/sound/pcm.h */

/* Helper function for 64 bits numbers division. */
static inline void
divl(__u32 high, __u32 low, __u32 div, __u32 * q, __u32 * r)
{
	__u64 n = (__u64) high << 32 | low;
	__u64 d = (__u64) div << 31;
	__u32 q1 = 0;
	int c = 32;

	while (n > 0xffffffffU) {
		q1 <<= 1;
		if (n >= d) {
			n -= d;
			q1 |= 1;
		}
		d >>= 1;
		c--;
	}
	q1 <<= c;
	if (n) {
		low = n;
		*q = q1 | (low / div);
		if (r)
			*r = low % div;
	} else {
		if (r)
			*r = 0;
		*q = q1;
	}
	return;
}

/* Function for 64 bits numbers division. */
static inline __u64
div64_32(__u64 n, __u32 div, __u32 * rem)
{
	__u32 low, high;

	low = n & 0xffffffff;
	high = n >> 32;
	if (high) {
		__u32 high1 = high % div;
		__u32 low1 = low;
		high /= div;
		divl(high1, low1, div, &low, rem);
		return (__u64) high << 32 | low;
	} else {
		if (rem)
			*rem = low % div;
		return low / div;
	}

	return 0;
}

#endif /* __FS_REISER4_LIB_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
