/* $Id: head.h,v 1.30 1997/08/08 08:34:33 jj Exp $ */
#ifndef _SPARC64_HEAD_H
#define _SPARC64_HEAD_H

#include <asm/pstate.h>

#define KERNBASE	0x400000

#define	PTREGS_OFF	(STACK_BIAS + REGWIN_SZ)

#define CHEETAH_ID	0x003e0014
#define CHEETAH_PLUS_ID	0x003e0015

#endif /* !(_SPARC64_HEAD_H) */
