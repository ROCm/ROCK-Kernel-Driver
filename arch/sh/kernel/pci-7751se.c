/*
 * linux/arch/sh/kernel/pci-7751se.c
 *
 * Author:  Ian DaSilva (idasilva@mvista.com)
 *
 * Highly leveraged from pci-bigsur.c, written by Dustin McIntire.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * PCI initialization for the Hitachi SH7751 Solution Engine board (MS7751SE01)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include <asm/io.h>
#include <asm/pci-sh7751.h>

#define PCIMCR_MRSET_OFF	0xBFFFFFFF
#define PCIMCR_RFSH_OFF		0xFFFFFFFB

/*
 * Only long word accesses of the PCIC's internal local registers and the
 * configuration registers from the CPU is supported.
 */
#define PCIC_WRITE(x,v) writel((v), PCI_REG(x))
#define PCIC_READ(x) readl(PCI_REG(x))

/*
 * Description:  This function sets up and initializes the pcic, sets
 * up the BARS, maps the DRAM into the address space etc, etc.
 */
int __init pcibios_init_platform(void)
{
   unsigned long data;
   unsigned long bcr1, wcr1, wcr2, wcr3, mcr;
   unsigned short bcr2;

   //
   // Initialize the slave bus controller on the pcic.  The values used
   // here should not be hardcoded, but they should be taken from the bsc
   // on the processor, to make this function as generic as possible.
   // (i.e. Another sbc may usr different SDRAM timing settings -- in order
   // for the pcic to work, its settings need to be exactly the same.)
   //
   bcr1 = (*(volatile unsigned long*)(SH7751_BCR1));
   bcr2 = (*(volatile unsigned short*)(SH7751_BCR2));
   wcr1 = (*(volatile unsigned long*)(SH7751_WCR1));
   wcr2 = (*(volatile unsigned long*)(SH7751_WCR2));
   wcr3 = (*(volatile unsigned long*)(SH7751_WCR3));
   mcr = (*(volatile unsigned long*)(SH7751_MCR));

   bcr1 = bcr1 | 0x00080000;  /* Enable Bit 19, BREQEN */
   (*(volatile unsigned long*)(SH7751_BCR1)) = bcr1;   

   bcr1 = bcr1 | 0x40080000;  /* Enable Bit 19 BREQEN, set PCIC to slave */
   PCIC_WRITE(SH7751_PCIBCR1, bcr1);	 /* PCIC BCR1 */
   PCIC_WRITE(SH7751_PCIBCR2, bcr2);     /* PCIC BCR2 */
   PCIC_WRITE(SH7751_PCIWCR1, wcr1);     /* PCIC WCR1 */
   PCIC_WRITE(SH7751_PCIWCR2, wcr2);     /* PCIC WCR2 */
   PCIC_WRITE(SH7751_PCIWCR3, wcr3);     /* PCIC WCR3 */
   mcr = (mcr & PCIMCR_MRSET_OFF) & PCIMCR_RFSH_OFF;
   PCIC_WRITE(SH7751_PCIMCR, mcr);      /* PCIC MCR */

   PCIC_WRITE(SH7751_PCIINTM, 0x0000c3ff);
   PCIC_WRITE(SH7751_PCIAINTM, 0x0000980f);
   PCIC_WRITE(SH7751_PCICONF1, 0xF39000C7);  /* FB9000C7 */
   PCIC_WRITE(SH7751_PCICONF2, 0x00000000);  /* PCI Class code & Revision ID */
   PCIC_WRITE(SH7751_PCICONF4, 0xab000001);  /* PCI I/O */
   PCIC_WRITE(SH7751_PCICONF5, 0x0c000000);  /* PCI MEM0 old val: 0xb0000000 */
   PCIC_WRITE(SH7751_PCICONF6, 0xd0000000);  /* PCI MEM1 */
   PCIC_WRITE(SH7751_PCICONF11, 0x35051054); /* PCI Sub system ID & Sub system vendor ID */
   PCIC_WRITE(SH7751_PCILSR0, 0x03f00000);   /* PCI MEM0 */
   PCIC_WRITE(SH7751_PCILSR1, 0x00000000);   /* PCI MEM1 */
   PCIC_WRITE(SH7751_PCILAR0, 0x0c000000);   /* MEM0 */
   PCIC_WRITE(SH7751_PCILAR1, 0x00000000);   /* MEM1 */

   PCIC_WRITE(SH7751_PCICR, 0xa5000001);

   printk("SH7751 PCI: Finished initialization of the PCI controller\n");

   return 1;
}

int __init pcibios_map_platform_irq(u8 slot, u8 pin)
{
        switch (slot) {
        case 0: return 13;
        case 1: return 13; 	/* AMD Ethernet controller */
        case 2: return -1;
        case 3: return -1;
        case 4: return -1;
        default:
                printk("PCI: Bad IRQ mapping request for slot %d\n", slot);
                return -1;
        }
}
