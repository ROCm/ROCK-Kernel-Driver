/* $Id: md.h,v 1.1 1997/12/15 15:12:15 jj Exp $
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
