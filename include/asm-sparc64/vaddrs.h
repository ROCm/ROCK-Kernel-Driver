/* $Id: vaddrs.h,v 1.10 1998/05/14 13:36:01 jj Exp $ */
#ifndef _SPARC64_VADDRS_H
#define _SPARC64_VADDRS_H

/* asm-sparc64/vaddrs.h:  Here will be define the virtual addresses at
 *                      which important I/O addresses will be mapped.
 *                      For instance the timer register virtual address
 *                      is defined here.
 *
 * Copyright (C) 1995,1998 David S. Miller (davem@caip.rutgers.edu)
 */

/* Everything here must be in the first kernel PGD. */
#define  DVMA_VADDR     0x0000000100000000ULL  /* Base area of the DVMA on suns */
#define  DVMA_LEN       0x0000000040000000ULL  /* Size of the DVMA address space */
#define  DVMA_END       0x0000000140000000ULL
#define  MODULES_VADDR	0x0000000001000000ULL  /* Where to map modules */
#define  MODULES_LEN	0x000000007f000000ULL
#define  MODULES_END	0x0000000080000000ULL

#endif /* !(_SPARC_VADDRS_H) */

