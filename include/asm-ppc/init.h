/*
 * BK Id: SCCS/s.init.h 1.8 07/06/01 09:19:30 trini
 */
#ifdef __KERNEL__
#ifndef _PPC_INIT_H
#define _PPC_INIT_H

#include <linux/init.h>

#define __pmac __attribute__ ((__section__ (".text.pmac")))
#define __pmacdata __attribute__ ((__section__ (".data.pmac")))
#define __pmacfunc(__argpmac) \
	__argpmac __pmac; \
	__argpmac
	
#define __prep __attribute__ ((__section__ (".text.prep")))
#define __prepdata __attribute__ ((__section__ (".data.prep")))
#define __prepfunc(__argprep) \
	__argprep __prep; \
	__argprep

#define __chrp __attribute__ ((__section__ (".text.chrp")))
#define __chrpdata __attribute__ ((__section__ (".data.chrp")))
#define __chrpfunc(__argchrp) \
	__argchrp __chrp; \
	__argchrp

/* this is actually just common chrp/pmac code, not OF code -- Cort */
#define __openfirmware __attribute__ ((__section__ (".text.openfirmware")))
#define __openfirmwaredata __attribute__ ((__section__ (".data.openfirmware")))
#define __openfirmwarefunc(__argopenfirmware) \
	__argopenfirmware __openfirmware; \
	__argopenfirmware
	
#endif /* _PPC_INIT_H */
#endif /* __KERNEL__ */
