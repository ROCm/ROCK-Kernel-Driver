/*
 * BK Id: SCCS/s.md.h 1.5 05/17/01 18:14:25 cort
 */
/*
 * md.h: High speed xor_block operation for RAID4/5 
 *
 */
 
#ifdef __KERNEL__
#ifndef __ASM_MD_H
#define __ASM_MD_H

/* #define HAVE_ARCH_XORBLOCK */

#define MD_XORBLOCK_ALIGNMENT	sizeof(long)

#endif /* __ASM_MD_H */
#endif /* __KERNEL__ */
