#ifndef __UDF_ENDIAN_H
#define __UDF_ENDIAN_H

#ifndef __KERNEL__ 

#include <sys/types.h>

#if __BYTE_ORDER == 0

#error "__BYTE_ORDER must be defined"

#elif __BYTE_ORDER == __BIG_ENDIAN

#define le16_to_cpu(x) \
	((Uint16)((((Uint16)(x) & 0x00FFU) << 8) | \
		  (((Uint16)(x) & 0xFF00U) >> 8)))
 
#define le32_to_cpu(x) \
	((Uint32)((((Uint32)(x) & 0x000000FFU) << 24) | \
		  (((Uint32)(x) & 0x0000FF00U) <<  8) | \
		  (((Uint32)(x) & 0x00FF0000U) >>  8) | \
		  (((Uint32)(x) & 0xFF000000U) >> 24)))

#define le64_to_cpu(x) \
	((Uint64)((((Uint64)(x) & 0x00000000000000FFULL) << 56) | \
		  (((Uint64)(x) & 0x000000000000FF00ULL) << 40) | \
		  (((Uint64)(x) & 0x0000000000FF0000ULL) << 24) | \
		  (((Uint64)(x) & 0x00000000FF000000ULL) <<  8) | \
		  (((Uint64)(x) & 0x000000FF00000000ULL) >>  8) | \
		  (((Uint64)(x) & 0x0000FF0000000000ULL) >> 24) | \
		  (((Uint64)(x) & 0x00FF000000000000ULL) >> 40) | \
		  (((Uint64)(x) & 0xFF00000000000000ULL) >> 56)))		

#define cpu_to_le16(x) (le16_to_cpu(x))
#define cpu_to_le32(x) (le32_to_cpu(x))
#define cpu_to_le64(x) (le64_to_cpu(x))

#else /* __BYTE_ORDER == __LITTLE_ENDIAN */

#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)
#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

#endif /* __BYTE_ORDER == 0 */

#include <string.h>

#else /* __KERNEL__ */

#include <asm/byteorder.h>
#include <linux/string.h>

#endif /* ! __KERNEL__ */

static inline lb_addr lelb_to_cpu(lb_addr in)
{
	lb_addr out;
	out.logicalBlockNum = le32_to_cpu(in.logicalBlockNum);
	out.partitionReferenceNum = le16_to_cpu(in.partitionReferenceNum);
	return out;
}

static inline lb_addr cpu_to_lelb(lb_addr in)
{
	lb_addr out;
	out.logicalBlockNum = cpu_to_le32(in.logicalBlockNum);
	out.partitionReferenceNum = cpu_to_le16(in.partitionReferenceNum);
	return out;
}

static inline timestamp lets_to_cpu(timestamp in)
{
	timestamp out;
	memcpy(&out, &in, sizeof(timestamp));
	out.typeAndTimezone = le16_to_cpu(in.typeAndTimezone);
	out.year = le16_to_cpu(in.year);
	return out;
}

static inline long_ad lela_to_cpu(long_ad in)
{
	long_ad out;
	out.extLength = le32_to_cpu(in.extLength);
	out.extLocation = lelb_to_cpu(in.extLocation);
	return out;
}

static inline long_ad cpu_to_lela(long_ad in)
{
	long_ad out;
	out.extLength = cpu_to_le32(in.extLength);
	out.extLocation = cpu_to_lelb(in.extLocation);
	return out;
}

static inline extent_ad leea_to_cpu(extent_ad in)
{
	extent_ad out;
	out.extLength = le32_to_cpu(in.extLength);
	out.extLocation = le32_to_cpu(in.extLocation);
	return out;
}

static inline timestamp cpu_to_lets(timestamp in)
{
	timestamp out;
	memcpy(&out, &in, sizeof(timestamp));
	out.typeAndTimezone = cpu_to_le16(in.typeAndTimezone);
	out.year = cpu_to_le16(in.year);
	return out;
}

#endif /* __UDF_ENDIAN_H */
