/*
 *  linux/fs/ufs/swab.h
 *
 * Copyright (C) 1997, 1998 Francois-Rene Rideau <fare@tunes.org>
 * Copyright (C) 1998 Jakub Jelinek <jj@ultra.linux.cz>
 */

#ifndef _UFS_SWAB_H
#define _UFS_SWAB_H

/*
 * Notes:
 *    HERE WE ASSUME EITHER BIG OR LITTLE ENDIAN UFSes
 *    in case there are ufs implementations that have strange bytesexes,
 *    you'll need to modify code here as well as in ufs_super.c and ufs_fs.h
 *    to support them.
 *
 *    WE ALSO ASSUME A REMOTELY SANE ARCHITECTURE BYTESEX.
 *    We are not ready to confront insane bytesexual perversions where
 *    conversion to/from little/big-endian is not an involution.
 *    That is, we require that XeYZ_to_cpu(x) == cpu_to_XeYZ(x)
 *
 *    NOTE that swab macros depend on a variable (or macro) swab being in
 *    scope and properly initialized (usually from sb->u.ufs_sb.s_swab).
 *    Its meaning depends on whether the architecture is sane-endian or not.
 *    For sane architectures, it's a flag taking values UFS_NATIVE_ENDIAN (0)
 *    or UFS_SWABBED_ENDIAN (1), indicating whether to swab or not.
 *    For pervert architectures, it's either UFS_LITTLE_ENDIAN or
 *    UFS_BIG_ENDIAN whose meaning you'll have to guess.
 *
 *    It is important to keep these conventions in synch with ufs_fs.h
 *    and super.c. Failure to do so (initializing swab to 0 both for
 *    NATIVE_ENDIAN and LITTLE_ENDIAN) led to nasty crashes on big endian
 *    machines reading little endian UFSes. Search for "swab =" in super.c.
 *
 *    I also suspect the whole UFS code to trust the on-disk structures
 *    much too much, which might lead to losing badly when mounting
 *    inconsistent partitions as UFS filesystems. fsck required (but of
 *    course, no fsck.ufs has yet to be ported from BSD to Linux as of 199808).
 */

#include <linux/ufs_fs.h>
#include <asm/byteorder.h>

/*
 * These are only valid inside ufs routines,
 * after swab has been initialized to sb->u.ufs_sb.s_swab
 */
#define SWAB16(x) ufs_swab16(swab,x)
#define SWAB32(x) ufs_swab32(swab,x)
#define SWAB64(x) ufs_swab64(swab,x)

/*
 * We often use swabing, when we want to increment/decrement some value,
 * so these macros might become handy and increase readability. (Daniel)
 */
#define INC_SWAB16(x)	((x)=ufs_swab16_add(swab,x,1))
#define INC_SWAB32(x)	((x)=ufs_swab32_add(swab,x,1))
#define INC_SWAB64(x)	((x)=ufs_swab64_add(swab,x,1))
#define DEC_SWAB16(x)	((x)=ufs_swab16_add(swab,x,-1))
#define DEC_SWAB32(x)	((x)=ufs_swab32_add(swab,x,-1))
#define DEC_SWAB64(x)	((x)=ufs_swab64_add(swab,x,-1))
#define ADD_SWAB16(x,y)	((x)=ufs_swab16_add(swab,x,y))
#define ADD_SWAB32(x,y)	((x)=ufs_swab32_add(swab,x,y))
#define ADD_SWAB64(x,y)	((x)=ufs_swab64_add(swab,x,y))
#define SUB_SWAB16(x,y)	((x)=ufs_swab16_add(swab,x,-(y)))
#define SUB_SWAB32(x,y)	((x)=ufs_swab32_add(swab,x,-(y)))
#define SUB_SWAB64(x,y)	((x)=ufs_swab64_add(swab,x,-(y)))

#if defined(__LITTLE_ENDIAN) || defined(__BIG_ENDIAN) /* sane bytesex */
extern __inline__ __const__ __u16 ufs_swab16(unsigned swab, __u16 x) {
	if (swab)
		return swab16(x);
	else
		return x;
}
extern __inline__ __const__ __u32 ufs_swab32(unsigned swab, __u32 x) {
	if (swab)
		return swab32(x);
	else
		return x;
}
extern __inline__ __const__ __u64 ufs_swab64(unsigned swab, __u64 x) {
	if (swab)
		return swab64(x);
	else
		return x;
}
extern __inline__ __const__ __u16 ufs_swab16_add(unsigned swab, __u16 x, __u16 y) {
	if (swab)
		return swab16(swab16(x)+y);
	else
		return x + y;
}
extern __inline__ __const__ __u32 ufs_swab32_add(unsigned swab, __u32 x, __u32 y) {
	if (swab)
		return swab32(swab32(x)+y);
	else
		return x + y;
}
extern __inline__ __const__ __u64 ufs_swab64_add(unsigned swab, __u64 x, __u64 y) {
	if (swab)
		return swab64(swab64(x)+y);
	else
		return x + y;
}
#else /* bytesexual perversion -- BEWARE! Read note at top of file! */
extern __inline__ __const__ __u16 ufs_swab16(unsigned swab, __u16 x) {
	if (swab == UFS_LITTLE_ENDIAN)
		return le16_to_cpu(x);
	else
		return be16_to_cpu(x);
}
extern __inline__ __const__ __u32 ufs_swab32(unsigned swab, __u32 x) {
	if (swab == UFS_LITTLE_ENDIAN)
		return le32_to_cpu(x);
	else
		return be32_to_cpu(x);
}
extern __inline__ __const__ __u64 ufs_swab64(unsigned swab, __u64 x) {
	if (swab == UFS_LITTLE_ENDIAN)
		return le64_to_cpu(x);
	else
		return be64_to_cpu(x);
}
extern __inline__ __const__ __u16 ufs_swab16_add(unsigned swab, __u16 x, __u16 y) {
	return ufs_swab16(swab, ufs_swab16(swab, x) + y);
}
extern __inline__ __const__ __u32 ufs_swab32_add(unsigned swab, __u32 x, __u32 y) {
	return ufs_swab32(swab, ufs_swab32(swab, x) + y);
}
extern __inline__ __const__ __u64 ufs_swab64_add(unsigned swab, __u64 x, __u64 y) {
	return ufs_swab64(swab, ufs_swab64(swab, x) + y);
}
#endif /* byte sexuality */

#endif /* _UFS_SWAB_H */
