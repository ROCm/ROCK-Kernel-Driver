/*
 *  linux/drivers/ide/q40ide.c -- Q40 I/O port IDE Driver
 *
 *     (c) Richard Zidlicky
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>

#include <linux/ide.h>

    /*
     *  Bases of the IDE interfaces
     */

#define Q40IDE_NUM_HWIFS	2

#define PCIDE_BASE1	0x1f0
#define PCIDE_BASE2	0x170
#define PCIDE_BASE3	0x1e8
#define PCIDE_BASE4	0x168
#define PCIDE_BASE5	0x1e0
#define PCIDE_BASE6	0x160

static const q40ide_ioreg_t pcide_bases[Q40IDE_NUM_HWIFS] = {
    PCIDE_BASE1, PCIDE_BASE2, /* PCIDE_BASE3, PCIDE_BASE4  , PCIDE_BASE5,
    PCIDE_BASE6 */
};


    /*
     *  Offsets from one of the above bases
     */

#undef HD_DATA
#define HD_DATA  0x1f0

#define PCIDE_REG(x)	((q40ide_ioreg_t)(HD_##x-PCIDE_BASE1))

static const int pcide_offsets[IDE_NR_PORTS] = {
    PCIDE_REG(DATA), PCIDE_REG(ERROR), PCIDE_REG(NSECTOR), PCIDE_REG(SECTOR),
    PCIDE_REG(LCYL), PCIDE_REG(HCYL), PCIDE_REG(CURRENT), PCIDE_REG(STATUS),
    PCIDE_REG(CMD)
};

static int q40ide_default_irq(q40ide_ioreg_t base)
{
           switch (base) { 
	            case 0x1f0: return 14;
		    case 0x170: return 15;
		    case 0x1e8: return 11;
		    default:
			return 0;
	   }
}



    /*
     *  Probe for Q40 IDE interfaces
     */

void q40ide_init(void)
{
    int i;

    if (!MACH_IS_Q40)
      return ;

    for (i = 0; i < Q40IDE_NUM_HWIFS; i++) {
	hw_regs_t hw;

	ide_setup_ports(&hw,(ide_ioreg_t) pcide_bases[i], (int *)pcide_offsets, 
			pcide_bases[i]+0x206, 
			0, NULL, q40ide_default_irq(pcide_bases[i]));
	ide_register_hw(&hw, NULL);
    }
}

