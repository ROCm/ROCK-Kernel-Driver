/*
 * $Id: cfi_endian.h,v 1.9 2001/04/23 21:19:11 nico Exp $
 *
 * It seems that some helpful people decided to make life easier
 * for software engineers who aren't capable of dealing with the 
 * concept of byteswapping, and advise engineers to swap the bytes
 * by wiring the data lines up to flash chips from BE hosts backwards.
 *
 * So we have ugly stuff here to disable the byteswapping where necessary.
 * I'm not going to try to do this dynamically.
 *
 * At first I thought these guys were on crack, but then I discovered the
 * LART. 
 *
 */

#include <asm/byteorder.h>

#ifndef CONFIG_MTD_CFI_ADV_OPTIONS

#define CFI_HOST_ENDIAN

#else

#ifdef CONFIG_MTD_CFI_NOSWAP
#define CFI_HOST_ENDIAN
#endif

#ifdef CONFIG_MTD_CFI_LE_BYTE_SWAP
#define CFI_LITTLE_ENDIAN
#endif

#ifdef CONFIG_MTD_CFI_BE_BYTE_SWAP
#define CFI_BIG_ENDIAN
#endif

#ifdef CONFIG_MTD_CFI_LART_BIT_SWAP
#define CFI_LART_ENDIAN
#endif

#endif

#if defined(CFI_LITTLE_ENDIAN)
#define cpu_to_cfi8(x) (x)
#define cfi8_to_cpu(x) (x)
#define cpu_to_cfi16(x) cpu_to_le16(x)
#define cpu_to_cfi32(x) cpu_to_le32(x)
#define cfi16_to_cpu(x) le16_to_cpu(x)
#define cfi32_to_cpu(x) le32_to_cpu(x)
#elif defined (CFI_BIG_ENDIAN)
#define cpu_to_cfi8(x) (x)
#define cfi8_to_cpu(x) (x)
#define cpu_to_cfi16(x) cpu_to_be16(x)
#define cpu_to_cfi32(x) cpu_to_be32(x)
#define cfi16_to_cpu(x) be16_to_cpu(x)
#define cfi32_to_cpu(x) be32_to_cpu(x)
#elif defined (CFI_HOST_ENDIAN)
#define cpu_to_cfi8(x) (x)
#define cfi8_to_cpu(x) (x)
#define cpu_to_cfi16(x) (x)
#define cpu_to_cfi32(x) (x)
#define cfi16_to_cpu(x) (x)
#define cfi32_to_cpu(x) (x)
#elif defined (CFI_LART_ENDIAN)
/* 
   Fuck me backwards. The data line mapping on LART is as follows:

	U2	CPU	|	U3	CPU
	 0	20	|	0	12
	 1	22	|	1	14
	 2	19	|	2	11
	 3	17	|	3	9
	 4	24	|	4	0
	 5	26	|	5	2
	 6	31	|	6	7
	 7	29	|	7	5
	 8	21	|	8	13
	 9	23	|	9	15
	 10	18	|	10	10
	 11	16	|	11	8
	 12	25	|	12	1
	 13	27	|	13	3
	 14	30	|	14	6
	 15	28	|	15	4

   For historical reference: the reason why the LART has this strange
   mapping is that the designer of the board wanted address lines to
   be as short as possible. Why? Because in that way you don't need
   drivers in the address lines so the memory access time can be held
   short. -- Erik Mouw <J.A.K.Mouw@its.tudelft.nl>
*/
/* cpu_to_cfi16() and cfi16_to_cpu() are not needed because the LART
 * only has 32 bit wide Flash memory. -- Erik
 */
#define cpu_to_cfi16(x) (x)
#define cfi16_to_cpu(x) (x)
static inline __u32 cfi32_to_cpu(__u32 x)
{
	__u32 ret;

	ret =  (x & 0x08009000) >> 11;
	ret |= (x & 0x00002000) >> 10;
	ret |= (x & 0x04004000) >> 8;
	ret |= (x & 0x00000010) >> 4;
	ret |= (x & 0x91000820) >> 3;
	ret |= (x & 0x22080080) >> 2;
	ret |= (x & 0x40000400);
	ret |= (x & 0x00040040) << 1;
	ret |= (x & 0x00110000) << 4;
	ret |= (x & 0x00220100) << 5;
	ret |= (x & 0x00800208) << 6;
	ret |= (x & 0x00400004) << 9;
	ret |= (x & 0x00000001) << 12;
	ret |= (x & 0x00000002) << 13;

	return ret;
}
static inline __u32 cpu_to_cfi32(__u32 x)
{
	__u32 ret;

	ret =  (x & 0x00010012) << 11;
	ret |= (x & 0x00000008) << 10;
	ret |= (x & 0x00040040) << 8;
	ret |= (x & 0x00000001) << 4;
	ret |= (x & 0x12200104) << 3;
	ret |= (x & 0x08820020) << 2;
	ret |= (x & 0x40000400);
	ret |= (x & 0x00080080) >> 1;
	ret |= (x & 0x01100000) >> 4;
	ret |= (x & 0x04402000) >> 5;
	ret |= (x & 0x20008200) >> 6;
	ret |= (x & 0x80000800) >> 9;
	ret |= (x & 0x00001000) >> 12;
	ret |= (x & 0x00004000) >> 13;

	return ret;
}
#else
#error No CFI endianness defined
#endif
