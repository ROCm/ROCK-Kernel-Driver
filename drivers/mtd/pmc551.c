/*
 * $Id: pmc551.c,v 1.11 2000/11/23 13:40:12 dwmw2 Exp $
 *
 * PMC551 PCI Mezzanine Ram Device
 *
 * Author:
 *       Mark Ferrell <mferrell@mvista.com>
 *       Copyright 1999,2000 Nortel Networks
 *
 * License:
 *	 As part of this driver was derrived from the slram.c driver it falls
 *	 under the same license, which is GNU General Public License v2
 *
 * Description:
 *	 This driver is intended to support the PMC551 PCI Ram device from
 *	 Ramix Inc.  The PMC551 is a PMC Mezzanine module for cPCI embeded
 *	 systems.  The device contains a single SROM that initally programs the
 *	 V370PDC chipset onboard the device, and various banks of DRAM/SDRAM
 *	 onboard.  This driver implements this PCI Ram device as an MTD (Memory
 *	 Technologies Device) so that it can be used to hold a filesystem, or
 *	 for added swap space in embeded systems.  Since the memory on this
 *	 board isn't as fast as main memory we do not try to hook it into main
 *	 memeory as that would simply reduce performance on the system.  Using
 *	 it as a block device allows us to use it as high speed swap or for a
 *	 high speed disk device of some sort.  Which becomes very usefull on
 *	 diskless systems in the embeded market I might add.
 *	 
 * Notes:
 *	 Due to what I assume is more buggy SROM, the 64M PMC551 I have
 *	 available claims that all 4 of it's DRAM banks have 64M of ram 
 *	 configured (making a grand total of 256M onboard).  This is slightly
 *	 annoying since the BAR0 size reflects the aperture size, not the dram
 *	 size, and the V370PDC supplies no other method for memory size
 *	 discovery.  This problem is mostly only relivant when compiled as a
 *	 module, as the unloading of the module with an aperture size  smaller
 *	 then the ram will cause the driver to detect the onboard memory size
 *	 to be equal to the aperture size when the module is reloaded.  Soooo,
 *	 to help, the module supports an msize option to allow the
 *	 specification of the onboard memory, and an asize option, to allow the
 *	 specification of the aperture size.  The aperture must be equal to or
 *	 less then the memory size, the driver will correct this if you screw
 *	 it up.  This problem is not relivant for compiled in drivers as
 *	 compiled in drivers only init once.
 *
 * Credits:
 *       Saeed Karamooz <saeed@ramix.com> of Ramix INC. for the initial
 *       example code of how to initialize this device and for help with
 *       questions I had concerning operation of the device.
 *
 *       Most of the MTD code for this driver was originally written for the
 *       slram.o module in the MTD drivers package written by David Hinds
 *       <dhinds@allegro.stanford.edu> which allows the mapping of system
 *       memory into an mtd device.  Since the PMC551 memory module is
 *       accessed in the same fashion as system memory, the slram.c code
 *       became a very nice fit to the needs of this driver.  All we added was
 *       PCI detection/initialization to the driver and automaticly figure out
 *       the size via the PCI detection.o, later changes by Corey Minyard
 *       settup the card to utilize a 1M sliding apature.
 *
 *	 Corey Minyard <minyard@nortelnetworks.com>
 *       * Modified driver to utilize a sliding apature instead of mapping all
 *       memory into kernel space which turned out to be very wastefull.
 *       * Located a bug in the SROM's initialization sequence that made the
 *       memory unusable, added a fix to code to touch up the DRAM some.
 *
 * Bugs/FIXME's:
 *       * MUST fix the init function to not spin on a register
 *       waiting for it to set .. this does not safely handle busted devices
 *       that never reset the register correctly which will cause the system to
 *       hang w/ a reboot beeing the only chance at recover.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <stdarg.h>
#include <linux/pci.h>

#ifndef CONFIG_PCI
#error Enable PCI in your kernel config
#endif

#include <linux/mtd/mtd.h>
#include <linux/mtd/pmc551.h>
#include <linux/mtd/compatmac.h>

#if LINUX_VERSION_CODE > 0x20300
#define PCI_BASE_ADDRESS(dev) (dev->resource[0].start)
#else
#define PCI_BASE_ADDRESS(dev) (dev->base_address[0])
#endif

static struct mtd_info *pmc551list = NULL;

static int pmc551_erase (struct mtd_info *mtd, struct erase_info *instr)
{
        struct mypriv *priv = mtd->priv;
        u32 start_addr_highbits;
        u32 end_addr_highbits;
        u32 start_addr_lowbits;
        u32 end_addr_lowbits;
        unsigned long end;

        end = instr->addr + instr->len;

        /* Is it too much memory?  The second check find if we wrap around
           past the end of a u32. */
        if ((end > mtd->size) || (end < instr->addr)) {
                return -EINVAL;
        }

        start_addr_highbits = instr->addr & PMC551_ADDR_HIGH_MASK;
        end_addr_highbits = end & PMC551_ADDR_HIGH_MASK;
        start_addr_lowbits = instr->addr & PMC551_ADDR_LOW_MASK;
        end_addr_lowbits = end & PMC551_ADDR_LOW_MASK;

        pci_write_config_dword ( priv->dev,
                                 PMC551_PCI_MEM_MAP0,
                                 (priv->mem_map0_base_val
                                  | start_addr_highbits));
        if (start_addr_highbits == end_addr_highbits) {
                /* The whole thing fits within one access, so just one shot
                   will do it. */
                memset(priv->start + start_addr_lowbits,
                       0xff,
                       instr->len);
        } else {
                /* We have to do multiple writes to get all the data
                   written. */
                memset(priv->start + start_addr_lowbits,
                       0xff,
                       priv->aperture_size - start_addr_lowbits);
                start_addr_highbits += priv->aperture_size;
                while (start_addr_highbits != end_addr_highbits) {
                        pci_write_config_dword ( priv->dev,
                                                 PMC551_PCI_MEM_MAP0,
                                                 (priv->mem_map0_base_val
                                                  | start_addr_highbits));
                        memset(priv->start,
                               0xff,
                               priv->aperture_size);
                        start_addr_highbits += priv->aperture_size;
                }
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
                memset(priv->start,
                       0xff,
                       end_addr_lowbits);
        }

	instr->state = MTD_ERASE_DONE;

        if (instr->callback) {
                (*(instr->callback))(instr);
	}

        return 0;
}


static void pmc551_unpoint (struct mtd_info *mtd, u_char *addr)
{}


static int pmc551_read (struct mtd_info *mtd,
                        loff_t from,
                        size_t len,
                        size_t *retlen,
                        u_char *buf)
{
        struct mypriv *priv = (struct mypriv *)mtd->priv;
        u32 start_addr_highbits;
        u32 end_addr_highbits;
        u32 start_addr_lowbits;
        u32 end_addr_lowbits;
        unsigned long end;
        u_char *copyto = buf;


        /* Is it past the end? */
        if (from > mtd->size) {
                return -EINVAL;
        }

        end = from + len;
        start_addr_highbits = from & PMC551_ADDR_HIGH_MASK;
        end_addr_highbits = end & PMC551_ADDR_HIGH_MASK;
        start_addr_lowbits = from & PMC551_ADDR_LOW_MASK;
        end_addr_lowbits = end & PMC551_ADDR_LOW_MASK;


        /* Only rewrite the first value if it doesn't match our current
           values.  Most operations are on the same page as the previous
           value, so this is a pretty good optimization. */
        if (priv->curr_mem_map0_val !=
                        (priv->mem_map0_base_val | start_addr_highbits)) {
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
        }

        if (start_addr_highbits == end_addr_highbits) {
                /* The whole thing fits within one access, so just one shot
                   will do it. */
                memcpy(copyto,
                       priv->start + start_addr_lowbits,
                       len);
                copyto += len;
        } else {
                /* We have to do multiple writes to get all the data
                   written. */
                memcpy(copyto,
                       priv->start + start_addr_lowbits,
                       priv->aperture_size - start_addr_lowbits);
                copyto += priv->aperture_size - start_addr_lowbits;
                start_addr_highbits += priv->aperture_size;
                while (start_addr_highbits != end_addr_highbits) {
                        pci_write_config_dword ( priv->dev,
                                                 PMC551_PCI_MEM_MAP0,
                                                 (priv->mem_map0_base_val
                                                  | start_addr_highbits));
                        memcpy(copyto,
                               priv->start,
                               priv->aperture_size);
                        copyto += priv->aperture_size;
                        start_addr_highbits += priv->aperture_size;
                        if (start_addr_highbits >= mtd->size) {
                                /* Make sure we have the right value here. */
                                priv->curr_mem_map0_val
                                = (priv->mem_map0_base_val
                                   | start_addr_highbits);
                                goto out;
                        }
                }
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
                memcpy(copyto,
                       priv->start,
                       end_addr_lowbits);
                copyto += end_addr_lowbits;
        }

out:
        *retlen = copyto - buf;
        return 0;
}

static int pmc551_write (struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
        struct mypriv *priv = (struct mypriv *)mtd->priv;
        u32 start_addr_highbits;
        u32 end_addr_highbits;
        u32 start_addr_lowbits;
        u32 end_addr_lowbits;
        unsigned long end;
        const u_char *copyfrom = buf;


        /* Is it past the end? */
        if (to > mtd->size) {
                return -EINVAL;
        }

        end = to + len;
        start_addr_highbits = to & PMC551_ADDR_HIGH_MASK;
        end_addr_highbits = end & PMC551_ADDR_HIGH_MASK;
        start_addr_lowbits = to & PMC551_ADDR_LOW_MASK;
        end_addr_lowbits = end & PMC551_ADDR_LOW_MASK;


        /* Only rewrite the first value if it doesn't match our current
           values.  Most operations are on the same page as the previous
           value, so this is a pretty good optimization. */
        if (priv->curr_mem_map0_val !=
                        (priv->mem_map0_base_val | start_addr_highbits)) {
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
        }

        if (start_addr_highbits == end_addr_highbits) {
                /* The whole thing fits within one access, so just one shot
                   will do it. */
                memcpy(priv->start + start_addr_lowbits,
                       copyfrom,
                       len);
                copyfrom += len;
        } else {
                /* We have to do multiple writes to get all the data
                   written. */
                memcpy(priv->start + start_addr_lowbits,
                       copyfrom,
                       priv->aperture_size - start_addr_lowbits);
                copyfrom += priv->aperture_size - start_addr_lowbits;
                start_addr_highbits += priv->aperture_size;
                while (start_addr_highbits != end_addr_highbits) {
                        pci_write_config_dword ( priv->dev,
                                                 PMC551_PCI_MEM_MAP0,
                                                 (priv->mem_map0_base_val
                                                  | start_addr_highbits));
                        memcpy(priv->start,
                               copyfrom,
                               priv->aperture_size);
                        copyfrom += priv->aperture_size;
                        start_addr_highbits += priv->aperture_size;
                        if (start_addr_highbits >= mtd->size) {
                                /* Make sure we have the right value here. */
                                priv->curr_mem_map0_val
                                = (priv->mem_map0_base_val
                                   | start_addr_highbits);
                                goto out;
                        }
                }
                priv->curr_mem_map0_val = (priv->mem_map0_base_val
                                           | start_addr_highbits);
                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);
                memcpy(priv->start,
                       copyfrom,
                       end_addr_lowbits);
                copyfrom += end_addr_lowbits;
        }

out:
        *retlen = copyfrom - buf;
        return 0;
}

/*
 * Fixup routines for the V370PDC
 * PCI device ID 0x020011b0
 *
 * This function basicly kick starts the DRAM oboard the card and gets it
 * ready to be used.  Before this is done the device reads VERY erratic, so
 * much that it can crash the Linux 2.2.x series kernels when a user cat's
 * /proc/pci .. though that is mainly a kernel bug in handling the PCI DEVSEL
 * register.  FIXME: stop spinning on registers .. must implement a timeout
 * mechanism
 * returns the size of the memory region found.
 */
static u32 fixup_pmc551 (struct pci_dev *dev)
{
#ifdef CONFIG_MTD_PMC551_BUGFIX
        u32 dram_data;
#endif
        u32 size, dcmd, cfg, dtmp;
        u16 cmd, tmp, i;
	u8 bcmd, counter;

        /* Sanity Check */
        if(!dev) {
                return -ENODEV;
        }

	/*
	 * Attempt to reset the card
	 * FIXME: Stop Spinning registers
	 */
	counter=0;
	/* unlock registers */
	pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, 0xA5 );
	/* read in old data */
	pci_read_config_byte(dev, PMC551_SYS_CTRL_REG, &bcmd );
	/* bang the reset line up and down for a few */
	for(i=0;i<10;i++) {
		counter=0;
		bcmd &= ~0x80;
		while(counter++ < 100) {
			pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, bcmd);
		}
		counter=0;
		bcmd |= 0x80;
		while(counter++ < 100) {
			pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, bcmd);
		}
	}
	bcmd |= (0x40|0x20);
	pci_write_config_byte(dev, PMC551_SYS_CTRL_REG, bcmd);

        /* 
	 * Take care and turn off the memory on the device while we
	 * tweak the configurations
	 */
        pci_read_config_word(dev, PCI_COMMAND, &cmd);
        tmp = cmd & ~(PCI_COMMAND_IO|PCI_COMMAND_MEMORY);
        pci_write_config_word(dev, PCI_COMMAND, tmp);

	/*
	 * Disable existing aperture before probing memory size
	 */
	pci_read_config_dword(dev, PMC551_PCI_MEM_MAP0, &dcmd);
        dtmp=(dcmd|PMC551_PCI_MEM_MAP_ENABLE|PMC551_PCI_MEM_MAP_REG_EN);
	pci_write_config_dword(dev, PMC551_PCI_MEM_MAP0, dtmp);
	/*
	 * Grab old BAR0 config so that we can figure out memory size
	 * This is another bit of kludge going on.  The reason for the
	 * redundancy is I am hoping to retain the original configuration
	 * previously assigned to the card by the BIOS or some previous 
	 * fixup routine in the kernel.  So we read the old config into cfg,
	 * then write all 1's to the memory space, read back the result into
	 * "size", and then write back all the old config.
	 */
	pci_read_config_dword( dev, PCI_BASE_ADDRESS_0, &cfg );
#ifndef CONFIG_MTD_PMC551_BUGFIX
	pci_write_config_dword( dev, PCI_BASE_ADDRESS_0, ~0 );
	pci_read_config_dword( dev, PCI_BASE_ADDRESS_0, &size );
	pci_write_config_dword( dev, PCI_BASE_ADDRESS_0, cfg );
	size=~(size&PCI_BASE_ADDRESS_MEM_MASK)+1;
#else
        /*
         * Get the size of the memory by reading all the DRAM size values
         * and adding them up.
         *
         * KLUDGE ALERT: the boards we are using have invalid column and
         * row mux values.  We fix them here, but this will break other
         * memory configurations.
         */
        pci_read_config_dword(dev, PMC551_DRAM_BLK0, &dram_data);
        size = PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK0, dram_data);

        pci_read_config_dword(dev, PMC551_DRAM_BLK1, &dram_data);
        size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK1, dram_data);

        pci_read_config_dword(dev, PMC551_DRAM_BLK2, &dram_data);
        size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK2, dram_data);

        pci_read_config_dword(dev, PMC551_DRAM_BLK3, &dram_data);
        size += PMC551_DRAM_BLK_GET_SIZE(dram_data);
        dram_data = PMC551_DRAM_BLK_SET_COL_MUX(dram_data, 0x5);
        dram_data = PMC551_DRAM_BLK_SET_ROW_MUX(dram_data, 0x9);
        pci_write_config_dword(dev, PMC551_DRAM_BLK3, dram_data);

        /*
         * Oops .. something went wrong
         */
        if( (size &= PCI_BASE_ADDRESS_MEM_MASK) == 0) {
                return -ENODEV;
        }
#endif /* CONFIG_MTD_PMC551_BUGFIX */

	if ((cfg&PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_MEMORY) {
                return -ENODEV;
	}

        /*
         * Precharge Dram
         */
        pci_write_config_word( dev, PMC551_SDRAM_MA, 0x0400 );
        pci_write_config_word( dev, PMC551_SDRAM_CMD, 0x00bf );

        /*
         * Wait untill command has gone through
         * FIXME: register spinning issue
         */
        do {	pci_read_config_word( dev, PMC551_SDRAM_CMD, &cmd );
		if(counter++ > 100)break;
        } while ( (PCI_COMMAND_IO) & cmd );

        /*
	 * Turn on auto refresh 
	 * The loop is taken directly from Ramix's example code.  I assume that
	 * this must be held high for some duration of time, but I can find no
	 * documentation refrencing the reasons why.
	 * 
         */
        for ( i = 1; i<=8 ; i++) {
                pci_write_config_word (dev, PMC551_SDRAM_CMD, 0x0df);

                /*
                 * Make certain command has gone through
                 * FIXME: register spinning issue
                 */
		counter=0;
                do {	pci_read_config_word(dev, PMC551_SDRAM_CMD, &cmd);
			if(counter++ > 100)break;
                } while ( (PCI_COMMAND_IO) & cmd );
        }

        pci_write_config_word ( dev, PMC551_SDRAM_MA, 0x0020);
        pci_write_config_word ( dev, PMC551_SDRAM_CMD, 0x0ff);

        /*
         * Wait until command completes
         * FIXME: register spinning issue
         */
	counter=0;
        do {	pci_read_config_word ( dev, PMC551_SDRAM_CMD, &cmd);
		if(counter++ > 100)break;
        } while ( (PCI_COMMAND_IO) & cmd );

        pci_read_config_dword ( dev, PMC551_DRAM_CFG, &dcmd);
        dcmd |= 0x02000000;
        pci_write_config_dword ( dev, PMC551_DRAM_CFG, dcmd);

        /*
         * Check to make certain fast back-to-back, if not
         * then set it so
         */
        pci_read_config_word( dev, PCI_STATUS, &cmd);
        if((cmd&PCI_COMMAND_FAST_BACK) == 0) {
                cmd |= PCI_COMMAND_FAST_BACK;
                pci_write_config_word( dev, PCI_STATUS, cmd);
        }

        /*
         * Check to make certain the DEVSEL is set correctly, this device
         * has a tendancy to assert DEVSEL and TRDY when a write is performed
         * to the memory when memory is read-only
         */
        if((cmd&PCI_STATUS_DEVSEL_MASK) != 0x0) {
                cmd &= ~PCI_STATUS_DEVSEL_MASK;
                pci_write_config_word( dev, PCI_STATUS, cmd );
        }
        /*
         * Set to be prefetchable and put everything back based on old cfg.
	 * it's possible that the reset of the V370PDC nuked the original
	 * settup
         */
        cfg |= PCI_BASE_ADDRESS_MEM_PREFETCH;
	pci_write_config_dword( dev, PCI_BASE_ADDRESS_0, cfg );

        /*
         * Turn PCI memory and I/O bus access back on
         */
        pci_write_config_word( dev, PCI_COMMAND,
                               PCI_COMMAND_MEMORY | PCI_COMMAND_IO );
#ifdef CONFIG_MTD_PMC551_DEBUG
        /*
         * Some screen fun
         */
        printk(KERN_DEBUG "pmc551: %d%c (0x%x) of %sprefetchable memory at 0x%lx\n",
	       (size<1024)?size:(size<1048576)?size/1024:size/1024/1024,
               (size<1024)?'B':(size<1048576)?'K':'M',
	       size, ((dcmd&(0x1<<3)) == 0)?"non-":"",
               PCI_BASE_ADDRESS(dev)&PCI_BASE_ADDRESS_MEM_MASK );

        /*
         * Check to see the state of the memory
         */
        pci_read_config_dword( dev, PMC551_DRAM_BLK0, &dcmd );
        printk(KERN_DEBUG "pmc551: DRAM_BLK0 Flags: %s,%s\n"
			  "pmc551: DRAM_BLK0 Size: %d at %d\n"
			  "pmc551: DRAM_BLK0 Row MUX: %d, Col MUX: %d\n",
               (((0x1<<1)&dcmd) == 0)?"RW":"RO",
               (((0x1<<0)&dcmd) == 0)?"Off":"On",
	       PMC551_DRAM_BLK_GET_SIZE(dcmd),
	       ((dcmd>>20)&0x7FF), ((dcmd>>13)&0x7), ((dcmd>>9)&0xF) );

        pci_read_config_dword( dev, PMC551_DRAM_BLK1, &dcmd );
        printk(KERN_DEBUG "pmc551: DRAM_BLK1 Flags: %s,%s\n"
			  "pmc551: DRAM_BLK1 Size: %d at %d\n"
			  "pmc551: DRAM_BLK1 Row MUX: %d, Col MUX: %d\n",
               (((0x1<<1)&dcmd) == 0)?"RW":"RO",
               (((0x1<<0)&dcmd) == 0)?"Off":"On",
	       PMC551_DRAM_BLK_GET_SIZE(dcmd),
	       ((dcmd>>20)&0x7FF), ((dcmd>>13)&0x7), ((dcmd>>9)&0xF) );

        pci_read_config_dword( dev, PMC551_DRAM_BLK2, &dcmd );
        printk(KERN_DEBUG "pmc551: DRAM_BLK2 Flags: %s,%s\n"
			  "pmc551: DRAM_BLK2 Size: %d at %d\n"
			  "pmc551: DRAM_BLK2 Row MUX: %d, Col MUX: %d\n",
               (((0x1<<1)&dcmd) == 0)?"RW":"RO",
               (((0x1<<0)&dcmd) == 0)?"Off":"On",
	       PMC551_DRAM_BLK_GET_SIZE(dcmd),
	       ((dcmd>>20)&0x7FF), ((dcmd>>13)&0x7), ((dcmd>>9)&0xF) );

        pci_read_config_dword( dev, PMC551_DRAM_BLK3, &dcmd );
        printk(KERN_DEBUG "pmc551: DRAM_BLK3 Flags: %s,%s\n"
			  "pmc551: DRAM_BLK3 Size: %d at %d\n"
			  "pmc551: DRAM_BLK3 Row MUX: %d, Col MUX: %d\n",
               (((0x1<<1)&dcmd) == 0)?"RW":"RO",
               (((0x1<<0)&dcmd) == 0)?"Off":"On",
	       PMC551_DRAM_BLK_GET_SIZE(dcmd),
	       ((dcmd>>20)&0x7FF), ((dcmd>>13)&0x7), ((dcmd>>9)&0xF) );

        pci_read_config_word( dev, PCI_COMMAND, &cmd );
        printk( KERN_DEBUG "pmc551: Memory Access %s\n",
                (((0x1<<1)&cmd) == 0)?"off":"on" );
        printk( KERN_DEBUG "pmc551: I/O Access %s\n",
                (((0x1<<0)&cmd) == 0)?"off":"on" );

        pci_read_config_word( dev, PCI_STATUS, &cmd );
        printk( KERN_DEBUG "pmc551: Devsel %s\n",
                ((PCI_STATUS_DEVSEL_MASK&cmd)==0x000)?"Fast":
                ((PCI_STATUS_DEVSEL_MASK&cmd)==0x200)?"Medium":
                ((PCI_STATUS_DEVSEL_MASK&cmd)==0x400)?"Slow":"Invalid" );

        printk( KERN_DEBUG "pmc551: %sFast Back-to-Back\n",
                ((PCI_COMMAND_FAST_BACK&cmd) == 0)?"Not ":"" );

	pci_read_config_byte(dev, PMC551_SYS_CTRL_REG, &bcmd );
	printk( KERN_DEBUG "pmc551: EEPROM is under %s control\n"
			   "pmc551: System Control Register is %slocked to PCI access\n"
			   "pmc551: System Control Register is %slocked to EEPROM access\n", 
		(bcmd&0x1)?"software":"hardware",
		(bcmd&0x20)?"":"un", (bcmd&0x40)?"":"un");
#endif
        return size;
}

/*
 * Kernel version specific module stuffages
 */
#if LINUX_VERSION_CODE < 0x20211
#ifdef MODULE
#define init_pmc551 init_module
#define cleanup_pmc551 cleanup_module
#endif
#define __exit
#endif

#if defined(MODULE)
MODULE_AUTHOR("Mark Ferrell <mferrell@mvista.com>");
MODULE_DESCRIPTION(PMC551_VERSION);
MODULE_PARM(msize, "i");
MODULE_PARM_DESC(msize, "memory size, 6=32M, 7=64M, 8=128M, ect.. [32M-1024M]");
MODULE_PARM(asize, "i");
MODULE_PARM_DESC(asize, "aperture size, must be <= memsize [1M-1024M]");
#endif
/*
 * Stuff these outside the ifdef so as to not bust compiled in driver support
 */
static int msize=0;
#if defined(CONFIG_MTD_PMC551_APERTURE_SIZE)
static int asize=CONFIG_MTD_PMC551_APERTURE_SIZE
#else
static int asize=0;
#endif

/*
 * PMC551 Card Initialization
 */
int __init init_pmc551(void)
{
        struct pci_dev *PCI_Device = NULL;
        struct mypriv *priv;
        int count, found=0;
        struct mtd_info *mtd;
        u32 length = 0;

	if(msize) {
		if (msize < 6 || msize > 11 ) {
			printk(KERN_NOTICE "pmc551: Invalid memory size\n");
			return -ENODEV;
		}
		msize = (512*1024)<<msize;
	}

	if(asize) {
		if (asize < 1 || asize > 11 ) {
			printk(KERN_NOTICE "pmc551: Invalid aperture size\n");
			return -ENODEV;
		}
		asize = (512*1024)<<asize;
	}

        printk(KERN_INFO PMC551_VERSION);

        if(!pci_present()) {
                printk(KERN_NOTICE "pmc551: PCI not enabled.\n");
                return -ENODEV;
        }

        /*
         * PCU-bus chipset probe.
         */
        for( count = 0; count < MAX_MTD_DEVICES; count++ ) {

                if ( (PCI_Device = pci_find_device( PCI_VENDOR_ID_V3_SEMI,
                                                    PCI_DEVICE_ID_V3_SEMI_V370PDC, PCI_Device ) ) == NULL) {
                        break;
                }

                printk(KERN_NOTICE "pmc551: Found PCI V370PDC IRQ:%d\n",
                       PCI_Device->irq);

                /*
                 * The PMC551 device acts VERY wierd if you don't init it
                 * first.  i.e. it will not correctly report devsel.  If for
                 * some reason the sdram is in a wrote-protected state the
                 * device will DEVSEL when it is written to causing problems
                 * with the oldproc.c driver in
                 * some kernels (2.2.*)
                 */
                if((length = fixup_pmc551(PCI_Device)) <= 0) {
                        printk(KERN_NOTICE "pmc551: Cannot init SDRAM\n");
                        break;
                }
		if(msize) {
			length = msize;
			printk(KERN_NOTICE "pmc551: Using specified memory size 0x%x\n", length);
		}

                mtd = kmalloc(sizeof(struct mtd_info), GFP_KERNEL);
                if (!mtd) {
                        printk(KERN_NOTICE "pmc551: Cannot allocate new MTD device.\n");
                        break;
                }

                memset(mtd, 0, sizeof(struct mtd_info));

                priv = kmalloc (sizeof(struct mypriv), GFP_KERNEL);
                if (!priv) {
                        printk(KERN_NOTICE "pmc551: Cannot allocate new MTD device.\n");
                        kfree(mtd);
                        break;
                }
                memset(priv, 0, sizeof(*priv));
                mtd->priv = priv;

                priv->dev = PCI_Device;
		if(asize) {
			if(asize > length) {
				asize=length;
				printk(KERN_NOTICE "pmc551: reducing aperture size to fit memory [0x%x]\n",asize);
			} else {
				printk(KERN_NOTICE "pmc551: Using specified aperture size 0x%x\n", asize);
			}
			priv->aperture_size = asize;
		} else {
                	priv->aperture_size = length;
		}
                priv->start = ioremap((PCI_BASE_ADDRESS(PCI_Device)
                                       & PCI_BASE_ADDRESS_MEM_MASK),
                                      priv->aperture_size);
		
		/*
		 * Due to the dynamic nature of the code, we need to figure
		 * this out in order to stuff the register to set the proper
		 * aperture size.  If you know of an easier way to do this then
		 * PLEASE help yourself.
		 *
		 * Not with bloody floating point, you don't. Consider yourself
		 * duly LARTed. dwmw2.
		 */
		{
			u32 size;
			u16 bits;
			size = priv->aperture_size>>20;
			for(bits=0;!(size&0x01)&&size>0;bits++,size=size>>1);
			//size=((u32)((log10(priv->aperture_size)/.30103)-19)<<4);
                	priv->mem_map0_base_val = (PMC551_PCI_MEM_MAP_REG_EN
						| PMC551_PCI_MEM_MAP_ENABLE
						| size);
#ifdef CONFIG_MTD_PMC551_DEBUG
			printk(KERN_NOTICE "pmc551: aperture set to %d[%d]\n", 
					size, size>>4);
#endif
		}
                priv->curr_mem_map0_val = priv->mem_map0_base_val;

                pci_write_config_dword ( priv->dev,
                                         PMC551_PCI_MEM_MAP0,
                                         priv->curr_mem_map0_val);

                mtd->size 		= length;
                mtd->flags 		= (MTD_CLEAR_BITS
                                | MTD_SET_BITS
                                | MTD_WRITEB_WRITEABLE
                                | MTD_VOLATILE);
                mtd->erase 		= pmc551_erase;
                mtd->point 		= NULL;
                mtd->unpoint 		= pmc551_unpoint;
                mtd->read 		= pmc551_read;
                mtd->write 		= pmc551_write;
                mtd->module 		= THIS_MODULE;
                mtd->type 		= MTD_RAM;
                mtd->name 		= "PMC551 RAM board";
                mtd->erasesize 		= 0x10000;

                if (add_mtd_device(mtd)) {
                        printk(KERN_NOTICE "pmc551: Failed to register new device\n");
                        kfree(mtd->priv);
                        kfree(mtd);
                        break;
                }
                printk(KERN_NOTICE "Registered pmc551 memory device.\n");
                printk(KERN_NOTICE "Mapped %dM of memory from 0x%p to 0x%p\n",
                       priv->aperture_size/1024/1024,
                       priv->start,
                       priv->start + priv->aperture_size);
                printk(KERN_NOTICE "Total memory is %d%c\n",
	       		(length<1024)?length:
				(length<1048576)?length/1024:length/1024/1024,
               		(length<1024)?'B':(length<1048576)?'K':'M');
		priv->nextpmc551 = pmc551list;
		pmc551list = mtd;
		found++;
        }

        if( !pmc551list ) {
                printk(KERN_NOTICE "pmc551: not detected,\n");
                return -ENODEV;
        } else {
		printk(KERN_NOTICE "pmc551: %d pmc551 devices loaded\n", found);
                return 0;
	}
}

/*
 * PMC551 Card Cleanup
 */
static void __exit cleanup_pmc551(void)
{
        int found=0;
        struct mtd_info *mtd;
	struct mypriv *priv;

	while((mtd=pmc551list)) {
		priv = (struct mypriv *)mtd->priv;
		pmc551list = priv->nextpmc551;
		
		if(priv->start)
			iounmap(((struct mypriv *)mtd->priv)->start);
		
		kfree (mtd->priv);
		del_mtd_device(mtd);
		kfree(mtd);
		found++;
	}

	printk(KERN_NOTICE "pmc551: %d pmc551 devices unloaded\n", found);
}

#if LINUX_VERSION_CODE >= 0x20211
module_init(init_pmc551);
module_exit(cleanup_pmc551);
#endif
