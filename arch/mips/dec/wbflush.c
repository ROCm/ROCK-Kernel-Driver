/*
 * Setup the right wbflush routine for the different DECstations.
 *
 * Created with information from:
 *      DECstation 3100 Desktop Workstation Functional Specification
 *      DECstation 5000/200 KN02 System Module Functional Specification
 *      mipsel-linux-objdump --disassemble vmunix | grep "wbflush" :-)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Harald Koerfgen
 */

#include <asm/bootinfo.h>
#include <linux/init.h>

static void wbflush_kn01(void);
static void wbflush_kn210(void);
static void wbflush_kn02ba(void);
static void wbflush_kn03(void);

void (*__wbflush) (void);

void __init wbflush_setup(void)
{
	switch (mips_machtype) {
	case MACH_DS23100:
	    __wbflush = wbflush_kn01;
	    break;
	case MACH_DS5100:	/*  DS5100 MIPSMATE */
	    __wbflush = wbflush_kn210;
	    break;
	case MACH_DS5000_200:	/* DS5000 3max */
	    __wbflush = wbflush_kn01;
	    break;
	case MACH_DS5000_1XX:	/* DS5000/100 3min */
	    __wbflush = wbflush_kn02ba;
	    break;
	case MACH_DS5000_2X0:	/* DS5000/240 3max+ */
	    __wbflush = wbflush_kn03;
	    break;
	case MACH_DS5000_XX:	/* Personal DS5000/2x */
	    __wbflush = wbflush_kn02ba;
	    break;
	}
}

/*
 * For the DS3100 and DS5000/200 the writeback buffer functions
 * as part of Coprocessor 0.
 */
static void wbflush_kn01(void)
{
    asm(".set\tpush\n\t"
	".set\tnoreorder\n\t"
	"1:\tbc0f\t1b\n\t"
	"nop\n\t"
	".set\tpop");
}

/*
 * For the DS5100 the writeback buffer seems to be a part of Coprocessor 3.
 * But CP3 has to enabled first.
 */
static void wbflush_kn210(void)
{
    asm(".set\tpush\n\t"
	".set\tnoreorder\n\t"
	"mfc0\t$2,$12\n\t"
	"lui\t$3,0x8000\n\t"
	"or\t$3,$2,$3\n\t"
	"mtc0\t$3,$12\n\t"
	"nop\n"
	"1:\tbc3f\t1b\n\t"
	"nop\n\t"
	"mtc0\t$2,$12\n\t"
	"nop\n\t"
	".set\tpop"
  : : :"$2", "$3");
}

/*
 * Looks like some magic with the System Interrupt Mask Register
 * in the famous IOASIC for kmins and maxines.
 */
static void wbflush_kn02ba(void)
{
    asm(".set\tpush\n\t"
	".set\tnoreorder\n\t"
	"lui\t$2,0xbc04\n\t"
	"lw\t$3,0x120($2)\n\t"
	"lw\t$3,0x120($2)\n\t"
	".set\tpop"
  : : :"$2", "$3");
}

/*
 * The DS500/2x0 doesnt need to write back the WB.
 */
static void wbflush_kn03(void)
{
}

#ifdef EXPORT_SYMTAB
#include <linux/module.h>

EXPORT_SYMBOL(__wbflush);
#endif
