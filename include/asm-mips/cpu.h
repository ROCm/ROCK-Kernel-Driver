/* $Id: cpu.h,v 1.1 1996/06/23 09:38:33 dm Exp $
 * cpu.h: Values of the PRId register used to match up
 *        various MIPS cpu types.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#ifndef _MIPS_CPU_H
#define _MIPS_CPU_H

/*
 * Assigned values for the product ID register.  In order to detect a
 * certain CPU type exactly eventually additional registers may need to
 * be examined.
 */
#define PRID_IMP_R2000    0x0100
#define PRID_IMP_R3000    0x0200
#define PRID_IMP_R6000    0x0300
#define PRID_IMP_R4000    0x0400
#define PRID_IMP_R6000A   0x0600
#define PRID_IMP_R10000   0x0900
#define PRID_IMP_R4300    0x0b00
#define PRID_IMP_R8000    0x1000
#define PRID_IMP_R4600    0x2000
#define PRID_IMP_R4700    0x2100
#define PRID_IMP_R4640    0x2200
#define PRID_IMP_R4650    0x2200		/* Same as R4640 */
#define PRID_IMP_R5000    0x2300
#define PRID_IMP_SONIC    0x2400
#define PRID_IMP_MAGIC    0x2500
#define PRID_IMP_RM7000   0x2700
#define PRID_IMP_NEVADA   0x2800		/* RM5260 ??? */

#define PRID_IMP_UNKNOWN  0xff00

#define PRID_REV_R4400    0x0040
#define PRID_REV_R3000A   0x0030
#define PRID_REV_R3000    0x0020
#define PRID_REV_R2000A   0x0010

#endif /* !(_MIPS_CPU_H) */
