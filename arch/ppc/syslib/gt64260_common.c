/*
 * arch/ppc/syslib/gt64260_common.c
 *
 * Common routines for the Marvell/Galileo GT64260 (Discovery) host bridge,
 * interrupt controller, memory controller, serial controller, enet controller,
 * etc.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * The GT64260 port is the result of hard work from many people from
 * many companies.  In particular, employees of Marvell/Galileo, Mission
 * Critical Linux, Xyterra, and MontaVista Software were heavily involved.
 */

/*
 * At last count, the 64260-B-0 has 65 errata and 24 restrictions.  The odds of
 * you getting it to work well, under stress, for a long period of time are
 * low.  If nothing else, you will likely run into an interrupt controller
 * bug.
 *
 * The newer 64260A-B-0 is much improved but has its own problems.
 * Dave Wilhardt <dwilhardt@zyterra.com> has discovered that you must set
 * up your PCI snoop regions to be prefetchable with 4-word bursts AND set the
 * "Memory Write and Invalidate bit" (bit 4) in the cmd reg of each PCI device
 * before coherency works between PCI and other devices.  Many thanks to Dave.
 *
 * So far this code has been tested on Marvell/Galileo EV-64260-BP and
 * an EV-64260A-BP uni-processor boards with 750 and 7400 processors.
 * It has not yet been tested with a 7410 or 7450, or on an smp system.
 *
 * Note: I have not had any luck moving the base register address of the bridge
 * 	 with the gt64260_set_base() routine.  I move it in the bootloader
 * 	 before starting the kernel.  I haven't really looked into it so it
 * 	 may be an easy fix. -- MAG
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/gt64260.h>
#include <asm/delay.h>


u32	gt64260_base; /* Virtual base address of 64260's regs */
u32	gt64260_revision; /* Revision of the chip */
u8	gt64260_pci_exclude_bridge = TRUE;


/*
 *****************************************************************************
 *
 *	Bridge Initialization Routines
 *
 *****************************************************************************
 */
static void gt64260_check_errata(struct pci_controller *hose_a,
				 struct pci_controller *hose_b);

/*
 * Typical '_find_bridges()' routine for boards with a GT64260 bridge.
 */
int __init
gt64260_find_bridges(u32 phys_base_addr, gt64260_bridge_info_t *info,
	int ((*map_irq)(struct pci_dev *, unsigned char, unsigned char)))
{
	struct pci_controller		*hose_a, *hose_b;
	u32				io_base_a, io_base_b;
	int				rc;


	gt64260_base = (u32)ioremap(phys_base_addr,GT64260_INTERNAL_SPACE_SIZE);

	hose_a = pcibios_alloc_controller();
	if (!hose_a)
		return -1;

	hose_b = pcibios_alloc_controller();
	if (!hose_b)
		return -1;

	info->hose_a = hose_a;
	info->hose_b = hose_b;

	/* Ends up mapping PCI Config addr/data pairs twice */
	setup_indirect_pci(hose_a,
			   phys_base_addr + GT64260_PCI_0_CONFIG_ADDR,
			   phys_base_addr + GT64260_PCI_0_CONFIG_DATA);

	setup_indirect_pci(hose_b,
			   phys_base_addr + GT64260_PCI_1_CONFIG_ADDR,
			   phys_base_addr + GT64260_PCI_1_CONFIG_DATA);

	if ((rc = gt64260_bridge_init(info)) != 0) {
		iounmap((void *)hose_a->cfg_addr);
		iounmap((void *)hose_a->cfg_data);
		iounmap((void *)hose_b->cfg_addr);
		iounmap((void *)hose_b->cfg_data);
		iounmap((void *)gt64260_base);
		return rc;
	}

	/* ioremap PCI I/O regions */
	io_base_b = (u32)ioremap(info->pci_1_io_start_proc,info->pci_1_io_size);
	io_base_a = (u32)ioremap(info->pci_0_io_start_proc,info->pci_0_io_size);
	isa_io_base = io_base_a;

	hose_a->first_busno = 0;
	hose_a->last_busno  = 0xff;

	pci_init_resource(&hose_a->io_resource,
			  0,	/* really: io_base_a - isa_io_base */
			  info->pci_0_io_size - 1,
			  IORESOURCE_IO,
			  "host bridge PCI bus 0");
	hose_a->io_space.start = info->pci_0_io_start_pci;
	hose_a->io_space.end   = info->pci_0_io_start_pci +
					info->pci_0_io_size - 1;
	hose_a->io_base_virt = (void *)isa_io_base;

	pci_init_resource(&hose_a->mem_resources[0],
			  info->pci_0_mem_start_proc,
			  info->pci_0_mem_start_proc + info->pci_0_mem_size - 1,
			  IORESOURCE_MEM,
			  "host bridge PCI bus 0");
	hose_a->mem_space.start = info->pci_0_mem_start_pci_lo;
	hose_a->mem_space.end   = info->pci_0_mem_start_pci_lo +
					info->pci_0_mem_size - 1;
	hose_a->pci_mem_offset = (info->pci_0_mem_start_proc -
					info->pci_0_mem_start_pci_lo);

	hose_a->last_busno = pciauto_bus_scan(hose_a, hose_a->first_busno);


	hose_b->first_busno = hose_a->last_busno + 1;
	hose_b->bus_offset  = hose_b->first_busno;
	hose_b->last_busno  = 0xff;

	pci_init_resource(&hose_b->io_resource,
			  io_base_b - isa_io_base,
			  io_base_b - isa_io_base + info->pci_1_io_size - 1,
			  IORESOURCE_IO,
			  "host bridge PCI bus 1");
	hose_b->io_space.start = info->pci_1_io_start_pci;
	hose_b->io_space.end   = info->pci_1_io_start_pci +
					info->pci_1_io_size - 1;
	hose_b->io_base_virt = (void *)isa_io_base;

	pci_init_resource(&hose_b->mem_resources[0],
			  info->pci_1_mem_start_proc,
			  info->pci_1_mem_start_proc + info->pci_1_mem_size - 1,
			  IORESOURCE_MEM,
			  "host bridge PCI bus 1");
	hose_b->mem_space.start = info->pci_1_mem_start_pci_lo;
	hose_b->mem_space.end   = info->pci_1_mem_start_pci_lo +
					info->pci_1_mem_size - 1;
	hose_b->pci_mem_offset = (info->pci_1_mem_start_proc -
					info->pci_1_mem_start_pci_lo);

	hose_b->last_busno = pciauto_bus_scan(hose_b, hose_b->first_busno);


	ppc_md.pci_exclude_device = gt64260_pci_exclude_device;
	ppc_md.pci_swizzle        = common_swizzle;
	ppc_md.pci_map_irq        = map_irq;

	return 0;
} /* gt64260_find_bridges() */

/*
 * gt64260_bridge_init()
 *
 * Perform bridge initialization for a "typical" setup for a PPC system.
 */
int __init
gt64260_bridge_init(gt64260_bridge_info_t *info)
{
	int	window;
	u16	u16_val;
	u32	u32_val;
	int	rc = 0;
	u8	save_exclude;

	/*
	 * Count on firmware to set/clear other bits in this register.
	 *
	 * Set CPU CONFIG Reg bit:
	 *	bit 13 - Pipeline
	 *	bit 16 - RdOOO
	 *
	 * Clear CPU Config Reg bit:
	 * 	bit 12 - endianess
	 *	bit 27 - RemapWrDis
	 */
	u32_val = gt_read(GT64260_CPU_CONFIG);
	u32_val |= ((1<<13) | (1<<16));
	u32_val &= ~((1<<8) | (1<<12) | (1<<27));
	gt_write(GT64260_CPU_CONFIG, u32_val);

	/* PCI 0/1 Timeout and Retry limits */
	u32_val = gt_read(GT64260_PCI_0_TO_RETRY);
	u32_val |= 0x0000ffff;
	gt_write(GT64260_PCI_0_TO_RETRY, u32_val);

	u32_val = gt_read(GT64260_PCI_1_TO_RETRY);
	u32_val |= 0x0000ffff;
	gt_write(GT64260_PCI_1_TO_RETRY, u32_val);

	save_exclude = gt64260_pci_exclude_bridge;
	gt64260_pci_exclude_bridge = FALSE;

	/* Set class code to indicate host bridge */
	early_read_config_dword(info->hose_a,
			        info->hose_a->first_busno,
			        PCI_DEVFN(0,0),
			        PCI_CLASS_REVISION,
			        &u32_val);
	u32_val &= 0x000000ff;
	gt64260_revision = u32_val;	/* a 64260 or 64260A? */
	u32_val |= (PCI_CLASS_BRIDGE_HOST << 16);
	early_write_config_dword(info->hose_a,
				 info->hose_a->first_busno,
				 PCI_DEVFN(0,0),
				 PCI_CLASS_REVISION,
				 u32_val);

	early_read_config_dword(info->hose_b,
			        info->hose_b->first_busno,
			        PCI_DEVFN(0,0),
			        PCI_CLASS_REVISION,
			        &u32_val);
	u32_val &= 0x000000ff;
	u32_val |= (PCI_CLASS_BRIDGE_HOST << 16);
	early_write_config_dword(info->hose_b,
				 info->hose_b->first_busno,
				 PCI_DEVFN(0,0),
				 PCI_CLASS_REVISION,
				 u32_val);

	/* Enable 64260 to be PCI master & respond to PCI MEM cycles */
	early_read_config_word(info->hose_a,
			       info->hose_a->first_busno,
			       PCI_DEVFN(0,0),
			       PCI_COMMAND,
			       &u16_val);
	u16_val |= (PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
	early_write_config_word(info->hose_a,
				info->hose_a->first_busno,
				PCI_DEVFN(0,0),
				PCI_COMMAND,
				u16_val);

	early_read_config_word(info->hose_b,
			       info->hose_b->first_busno,
			       PCI_DEVFN(0,0),
			       PCI_COMMAND,
			       &u16_val);
	u16_val |= (PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
	early_write_config_word(info->hose_b,
				info->hose_b->first_busno,
				PCI_DEVFN(0,0),
				PCI_COMMAND,
				u16_val);

	gt64260_pci_exclude_bridge = save_exclude;

	/*
	 * Disable all CPU windows on the bridge except for SCS 0 which
	 * is covering all of system memory.:
	 */
	gt64260_cpu_disable_all_windows();

	/*
	 * Set CPU snoop window to indicate all of system memory
	 * is covered with wirte-back cache.
	 */
	gt64260_cpu_snoop_set_window(0,
				     0x00000000,
				     info->mem_size,
				     GT64260_CPU_SNOOP_WB);

	/*
	 * Set up CPU->PCI mappings (so CPU can get at PCI dev regs/mem).
	 * Will only use one of the four CPU->PCI MEM windows on each bus.
	 */
	gt64260_cpu_set_pci_io_window(0,
				      info->pci_0_io_start_proc,
				      info->pci_0_io_start_pci,
				      info->pci_0_io_size,
				      info->pci_0_io_swap);

	gt64260_cpu_set_pci_mem_window(0,
				       0,
				       info->pci_0_mem_start_proc,
				       info->pci_0_mem_start_pci_hi,
				       info->pci_0_mem_start_pci_lo,
				       info->pci_0_mem_size,
				       info->pci_0_mem_swap);

	gt64260_cpu_set_pci_io_window(1,
				      info->pci_1_io_start_proc,
				      info->pci_1_io_start_pci,
				      info->pci_1_io_size,
				      info->pci_1_io_swap);

	gt64260_cpu_set_pci_mem_window(1,
				       0,
				       info->pci_1_mem_start_proc,
				       info->pci_1_mem_start_pci_hi,
				       info->pci_1_mem_start_pci_lo,
				       info->pci_1_mem_size,
				       info->pci_1_mem_swap);

	/*
	 * Set up PCI MEM->system memory mapping (bridge slave PCI window).
	 *
	 * Set BAR enables to allow only the SCS0 slave window to respond
	 * to PCI read/write cycles.
	 */
	gt64260_pci_bar_enable(0, GT64260_PCI_SLAVE_BAR_REG_ENABLES_SCS_0);
	gt64260_pci_bar_enable(1, GT64260_PCI_SLAVE_BAR_REG_ENABLES_SCS_0);

	/*
	 * For virt_to_bus & bus_to_virt to work correctly, this mapping
	 * must be the same on both PCI buses.
	 */
	gt64260_pci_slave_scs_set_window(info->hose_a,
					 0,
					 0x00000000,
					 0x00000000,
					 info->mem_size);

	gt64260_pci_slave_scs_set_window(info->hose_b,
					 0,
					 0x00000000,
					 0x00000000,
					 info->mem_size);
	pci_dram_offset = 0; /* System mem at same addr on PCI & cpu bus */

	/* Disable all the access control windows */
	for (window=0; window<GT64260_PCI_ACC_CNTL_WINDOWS; window++) {
		gt64260_pci_acc_cntl_set_window(0, window, 0, 0, 0, 0);
		gt64260_pci_acc_cntl_set_window(1, window, 0, 0, 0, 0);
	}

	/* Disable all the PCI snoop regions */
	for (window=0; window<GT64260_PCI_SNOOP_WINDOWS; window++) {
		gt64260_pci_snoop_set_window(0, window, 0, 0, 0, 0);
		gt64260_pci_snoop_set_window(1, window, 0, 0, 0, 0);
	}

	gt64260_pci_acc_cntl_set_window(0,
					0,
					0x00000000,
					0x00000000,
					info->mem_size,
					(GT64260_PCI_ACC_CNTL_PREFETCHEN |
					 GT64260_PCI_ACC_CNTL_MBURST_4_WORDS |
					 GT64260_PCI_ACC_CNTL_SWAP_BYTE));

	gt64260_pci_acc_cntl_set_window(1,
					0,
					0x00000000,
					0x00000000,
					info->mem_size,
					(GT64260_PCI_ACC_CNTL_PREFETCHEN |
					 GT64260_PCI_ACC_CNTL_MBURST_4_WORDS |
					 GT64260_PCI_ACC_CNTL_SWAP_BYTE));

	gt64260_pci_snoop_set_window(0,
				     0,
				     0x00000000,
				     0x00000000,
				     info->mem_size,
				     GT64260_PCI_SNOOP_WB);
				
	gt64260_pci_snoop_set_window(1,
				     0,
				     0x00000000,
				     0x00000000,
				     info->mem_size,
				     GT64260_PCI_SNOOP_WB);

	gt64260_check_errata(info->hose_a, info->hose_b);


	/* Set latency timer (to 64) & cacheline size; clear BIST */
	gt64260_pci_exclude_bridge = FALSE;
	u32_val = ((0x04 << 8) | (L1_CACHE_LINE_SIZE / 4));

	early_write_config_dword(info->hose_a,
				 info->hose_a->first_busno,
				 PCI_DEVFN(0,0),
				 PCI_CACHE_LINE_SIZE,
				 u32_val);
	early_write_config_dword(info->hose_b,
				 info->hose_b->first_busno,
				 PCI_DEVFN(0,0),
				 PCI_CACHE_LINE_SIZE,
				 u32_val);
	gt64260_pci_exclude_bridge = TRUE;

	return rc;
} /* gt64260_bridge_init() */

/*
 * gt64260_check_errata()
 *
 * Apply applicable errata and restrictions from 0.5 of the
 * Errata and Restrictions document from Marvell/Galileo.
 */
static void __init
gt64260_check_errata(struct pci_controller *hose_a,
		     struct pci_controller *hose_b)
{
	u32	val;
	u8	save_exclude;

	/* Currently 2 versions, 64260 and 64260A */
	if (gt64260_revision == GT64260) {
		save_exclude = gt64260_pci_exclude_bridge;
		gt64260_pci_exclude_bridge = FALSE;

		/* FEr#5, FEr#12 */
		early_read_config_dword(hose_a,
					hose_a->first_busno,
					PCI_DEVFN(0,0),
					PCI_COMMAND,
					&val);
		val &= ~(PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY);
		early_write_config_dword(hose_a,
					 hose_a->first_busno,
					 PCI_DEVFN(0,0),
					 PCI_COMMAND,
					 val);

		early_read_config_dword(hose_b,
					hose_b->first_busno,
					PCI_DEVFN(0,0),
					PCI_COMMAND,
					&val);
		val &= ~(PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY);
		early_write_config_dword(hose_b,
					 hose_b->first_busno,
					 PCI_DEVFN(0,0),
					 PCI_COMMAND,
					 val);
		gt64260_pci_exclude_bridge = save_exclude;

		/* FEr#12, FEr#13 */
		gt_clr_bits(GT64260_PCI_0_CMD, ((1<<4) | (1<<5) | (1<<9)));
		gt_clr_bits(GT64260_PCI_1_CMD, ((1<<4) | (1<<5) | (1<<9)));

		/* FEr#54 */
		gt_clr_bits(GT64260_CPU_SNOOP_BASE_0, 0xfffcf000);
		gt_clr_bits(GT64260_CPU_SNOOP_BASE_1, 0xfffcf000);
		gt_clr_bits(GT64260_CPU_SNOOP_BASE_2, 0xfffcf000);
		gt_clr_bits(GT64260_CPU_SNOOP_BASE_3, 0xfffcf000);

		/* R#18 */
		gt_set_bits(GT64260_SDRAM_CONFIG, (1<<26));

	} else if (gt64260_revision == GT64260A) {
		/* R#18 */
		gt_set_bits(GT64260_SDRAM_CONFIG, (1<<26));

		/* No longer errata so turn on */
		gt_set_bits(GT64260_PCI_0_CMD, ((1<<4) | (1<<5) | (1<<9)));
		gt_set_bits(GT64260_PCI_1_CMD, ((1<<4) | (1<<5) | (1<<9)));
	}
} /* gt64260_check_errata() */


/*
 *****************************************************************************
 *
 *	General Window Setting Routines
 *
 *****************************************************************************
 */

static int
gt64260_set_32bit_window(u32 base_addr,
			 u32 size,
			 u32 other_bits,
			 u32 bot_reg,
			 u32 top_reg)
{
	u32	val;

	if (size > 0) {
		/* Set up the window on the CPU side */
		gt_write(bot_reg, (base_addr >> 20) | other_bits);
		gt_write(top_reg, (base_addr + size - 1) >> 20);
	} else { /* Disable window */
		gt_write(top_reg, 0x00000000);
		gt_write(bot_reg, 0x00000fff | other_bits);
	}

	val = gt_read(bot_reg); /* Flush FIFO */
	return 0;
} /* gt64260_set_32bit_window() */

static int
gt64260_set_64bit_window(u32 base_addr_hi,
			 u32 base_addr_lo,
			 u32 size,
			 u32 other_bits,
			 u32 bot_reg_hi,
			 u32 bot_reg_lo,
			 u32 top_reg)
{
	int	rc;

	if ((rc = gt64260_set_32bit_window(base_addr_lo,
					   size,
					   other_bits,
					   bot_reg_lo,
					   top_reg)) == 0) {

		gt_write(bot_reg_hi, base_addr_hi);
		base_addr_hi = gt_read(bot_reg_hi); /* Flush FIFO */
	}

	return rc;
} /* gt64260_set_64bit_window() */


/*
 *****************************************************************************
 *
 *	CPU Configuration Routines
 *
 *****************************************************************************
 */

int
gt64260_cpu_scs_set_window(u32 window,
			   u32 base_addr,
			   u32 size)
{
	static u32
	cpu_scs_windows[GT64260_CPU_SCS_DECODE_WINDOWS][2] = {
		{ GT64260_CPU_SCS_DECODE_0_BOT, GT64260_CPU_SCS_DECODE_0_TOP },
		{ GT64260_CPU_SCS_DECODE_1_BOT, GT64260_CPU_SCS_DECODE_1_TOP },
		{ GT64260_CPU_SCS_DECODE_2_BOT, GT64260_CPU_SCS_DECODE_2_TOP },
		{ GT64260_CPU_SCS_DECODE_3_BOT, GT64260_CPU_SCS_DECODE_3_TOP },
	}; /* cpu_scs_windows[][] */
	int	rc = -1;

	if (window < GT64260_CPU_SCS_DECODE_WINDOWS) {
		rc = gt64260_set_32bit_window(base_addr,
					      size,
					      0,
					      cpu_scs_windows[window][0],
					      cpu_scs_windows[window][1]);
	}

	return rc;
} /* gt64260_cpu_scs_set_window() */

int
gt64260_cpu_cs_set_window(u32 window,
			  u32 base_addr,
			  u32 size)
{
	static u32
	cpu_cs_windows[GT64260_CPU_CS_DECODE_WINDOWS][2] = {
		{ GT64260_CPU_CS_DECODE_0_BOT, GT64260_CPU_CS_DECODE_0_TOP },
		{ GT64260_CPU_CS_DECODE_1_BOT, GT64260_CPU_CS_DECODE_1_TOP },
		{ GT64260_CPU_CS_DECODE_2_BOT, GT64260_CPU_CS_DECODE_2_TOP },
		{ GT64260_CPU_CS_DECODE_3_BOT, GT64260_CPU_CS_DECODE_3_TOP },
	}; /* cpu_cs_windows[][] */
	int	rc = -1;

	if (window < GT64260_CPU_CS_DECODE_WINDOWS) {
		rc = gt64260_set_32bit_window(base_addr,
					      size,
					      0,
					      cpu_cs_windows[window][0],
					      cpu_cs_windows[window][1]);
	}

	return rc;
} /* gt64260_cpu_cs_set_window() */

int
gt64260_cpu_boot_set_window(u32 base_addr,
			    u32 size)
{
	int	rc;

	rc = gt64260_set_32bit_window(base_addr,
				      size,
				      0,
				      GT64260_CPU_BOOT_CS_DECODE_0_BOT,
				      GT64260_CPU_BOOT_CS_DECODE_0_TOP);

	return rc;
} /* gt64260_cpu_boot_set_window() */

/*
 * gt64260_cpu_set_pci_io_window()
 *
 * Set up a CPU window into PCI I/O or MEM space.
 * Always do Read/Modify/Write to window regs.
 */
static int
gt64260_cpu_pci_set_window(u32 cpu_base_addr,
			   u32 pci_base_addr,
			   u32 size,
			   u32 other_bits,
			   u32 bot_reg,
			   u32 top_reg,
			   u32 remap_reg)
{
	u32	val;
	int	rc;

	if ((rc = gt64260_set_32bit_window(cpu_base_addr,
					   size,
					   other_bits,
					   bot_reg,
					   top_reg)) == 0) {

		/* Set up CPU->PCI remapping (on lower 32 bits) */
		gt_write(remap_reg, pci_base_addr >> 20);
		val = gt_read(bot_reg); /* Flush FIFO */
	}

	return rc;
} /* gt64260_cpu_pci_set_window() */


/*
 * gt64260_cpu_set_pci_io_window()
 *
 * Set up a CPU window into PCI I/O space.
 * Always do Read/Modify/Write to window regs.
 */
int
gt64260_cpu_set_pci_io_window(u32 pci_bus,
			      u32 cpu_base_addr,
			      u32 pci_base_addr,
			      u32 size,
			      u32 swap)
{
	/* 2 PCI buses with 1 I/O window each (from CPU point of view) */
	static u32
	cpu_pci_io_windows[GT64260_PCI_BUSES][3] = {
		{ GT64260_CPU_PCI_0_IO_DECODE_BOT,
		  GT64260_CPU_PCI_0_IO_DECODE_TOP,
		  GT64260_CPU_PCI_0_IO_REMAP },

		{ GT64260_CPU_PCI_1_IO_DECODE_BOT,
		  GT64260_CPU_PCI_1_IO_DECODE_TOP,
		  GT64260_CPU_PCI_1_IO_REMAP },
	}; /* cpu_pci_io_windows[][] */
	int	rc = -1;

	if (pci_bus < GT64260_PCI_BUSES) {
		rc =  gt64260_cpu_pci_set_window(cpu_base_addr,
					  pci_base_addr,
					  size,
					  swap,
					  cpu_pci_io_windows[pci_bus][0],
					  cpu_pci_io_windows[pci_bus][1],
					  cpu_pci_io_windows[pci_bus][2]);
	}

	return rc;
} /* gt64260_cpu_set_pci_io_window() */

/*
 * gt64260_cpu_set_pci_mem_window()
 *
 * Set up a CPU window into PCI MEM space (4 PCI MEM windows per PCI bus).
 * Always do Read/Modify/Write to window regs.
 */
int
gt64260_cpu_set_pci_mem_window(u32 pci_bus,
			       u32 window,
			       u32 cpu_base_addr,
			       u32 pci_base_addr_hi,
			       u32 pci_base_addr_lo,
			       u32 size,
			       u32 swap_64bit)
{
	/* 2 PCI buses with 4 memory windows each (from CPU point of view) */
	static u32
	cpu_pci_mem_windows[GT64260_PCI_BUSES][GT64260_PCI_MEM_WINDOWS_PER_BUS][4] = {
		{ /* PCI 0 */
			{ GT64260_CPU_PCI_0_MEM_0_DECODE_BOT,
			  GT64260_CPU_PCI_0_MEM_0_DECODE_TOP,
			  GT64260_CPU_PCI_0_MEM_0_REMAP_HI,
			  GT64260_CPU_PCI_0_MEM_0_REMAP_LO },

			{ GT64260_CPU_PCI_0_MEM_1_DECODE_BOT,
			  GT64260_CPU_PCI_0_MEM_1_DECODE_TOP,
			  GT64260_CPU_PCI_0_MEM_1_REMAP_HI,
			  GT64260_CPU_PCI_0_MEM_1_REMAP_LO },

			{ GT64260_CPU_PCI_0_MEM_2_DECODE_BOT,
			  GT64260_CPU_PCI_0_MEM_2_DECODE_TOP,
			  GT64260_CPU_PCI_0_MEM_2_REMAP_HI,
			  GT64260_CPU_PCI_0_MEM_2_REMAP_LO },

			{ GT64260_CPU_PCI_0_MEM_3_DECODE_BOT,
			  GT64260_CPU_PCI_0_MEM_3_DECODE_TOP,
			  GT64260_CPU_PCI_0_MEM_3_REMAP_HI,
			  GT64260_CPU_PCI_0_MEM_3_REMAP_LO }
		},

		{ /* PCI 1 */
			{ GT64260_CPU_PCI_1_MEM_0_DECODE_BOT,
			  GT64260_CPU_PCI_1_MEM_0_DECODE_TOP,
			  GT64260_CPU_PCI_1_MEM_0_REMAP_HI,
			  GT64260_CPU_PCI_1_MEM_0_REMAP_LO },

			{ GT64260_CPU_PCI_1_MEM_1_DECODE_BOT,
			  GT64260_CPU_PCI_1_MEM_1_DECODE_TOP,
			  GT64260_CPU_PCI_1_MEM_1_REMAP_HI,
			  GT64260_CPU_PCI_1_MEM_1_REMAP_LO },

			{ GT64260_CPU_PCI_1_MEM_2_DECODE_BOT,
			  GT64260_CPU_PCI_1_MEM_2_DECODE_TOP,
			  GT64260_CPU_PCI_1_MEM_2_REMAP_HI,
			  GT64260_CPU_PCI_1_MEM_2_REMAP_LO },

			{ GT64260_CPU_PCI_1_MEM_3_DECODE_BOT,
			  GT64260_CPU_PCI_1_MEM_3_DECODE_TOP,
			  GT64260_CPU_PCI_1_MEM_3_REMAP_HI,
			  GT64260_CPU_PCI_1_MEM_3_REMAP_LO },
		}
	}; /* cpu_pci_mem_windows[][][] */
	u32		remap_reg, remap;
	int		rc = -1;

	if ((pci_bus < GT64260_PCI_BUSES) &&
	    (window < GT64260_PCI_MEM_WINDOWS_PER_BUS)) {

		if (gt64260_cpu_pci_set_window(
			cpu_base_addr,
			pci_base_addr_lo,
			size,
			swap_64bit,
			cpu_pci_mem_windows[pci_bus][window][0],
			cpu_pci_mem_windows[pci_bus][window][1],
			cpu_pci_mem_windows[pci_bus][window][3]) == 0) {

			remap_reg = cpu_pci_mem_windows[pci_bus][window][2];
			gt_write(remap_reg, pci_base_addr_hi);

			remap = gt_read(remap_reg); /* Flush FIFO */

			rc = 0;
		}
	}

	return rc;
} /* gt64260_cpu_set_pci_mem_window() */

int
gt64260_cpu_prot_set_window(u32 window,
			    u32 base_addr,
			    u32 size,
			    u32 access_bits)
{
	static u32
	cpu_prot_windows[GT64260_CPU_PROT_WINDOWS][2] = {
		{ GT64260_CPU_PROT_BASE_0, GT64260_CPU_PROT_TOP_0 },
		{ GT64260_CPU_PROT_BASE_1, GT64260_CPU_PROT_TOP_1 },
		{ GT64260_CPU_PROT_BASE_2, GT64260_CPU_PROT_TOP_2 },
		{ GT64260_CPU_PROT_BASE_3, GT64260_CPU_PROT_TOP_3 },
		{ GT64260_CPU_PROT_BASE_4, GT64260_CPU_PROT_TOP_4 },
		{ GT64260_CPU_PROT_BASE_5, GT64260_CPU_PROT_TOP_5 },
		{ GT64260_CPU_PROT_BASE_6, GT64260_CPU_PROT_TOP_6 },
		{ GT64260_CPU_PROT_BASE_7, GT64260_CPU_PROT_TOP_7 },
	}; /* cpu_prot_windows[][] */
	int	rc = -1;

	if (window < GT64260_CPU_PROT_WINDOWS) {
		rc = gt64260_set_32bit_window(base_addr,
					      size,
					      access_bits,
					      cpu_prot_windows[window][0],
					      cpu_prot_windows[window][1]);
	}

	return rc;
} /* gt64260_cpu_prot_set_window() */

int
gt64260_cpu_snoop_set_window(u32 window,
			     u32 base_addr,
			     u32 size,
			     u32  snoop_type)
{
	static u32
	cpu_snoop_windows[GT64260_CPU_SNOOP_WINDOWS][2] = {
		{ GT64260_CPU_SNOOP_BASE_0, GT64260_CPU_SNOOP_TOP_0 },
		{ GT64260_CPU_SNOOP_BASE_1, GT64260_CPU_SNOOP_TOP_1 },
		{ GT64260_CPU_SNOOP_BASE_2, GT64260_CPU_SNOOP_TOP_2 },
		{ GT64260_CPU_SNOOP_BASE_3, GT64260_CPU_SNOOP_TOP_3 },
	}; /* cpu_snoop_windows[][] */
	int	rc = -1;

	if ((window < GT64260_CPU_SNOOP_WINDOWS) &&
	    (snoop_type <= GT64260_CPU_SNOOP_WB)) {

		rc = gt64260_set_32bit_window(base_addr,
					      size,
					      snoop_type,
					      cpu_snoop_windows[window][0],
					      cpu_snoop_windows[window][1]);
	}

	return rc;
} /* gt64260_cpu_snoop_set_window() */

void
gt64260_cpu_disable_all_windows(void)
{
	int	pci_bus, window;

	/* Don't disable SCS windows b/c we need to access system memory */

	for (window=0; window<GT64260_CPU_CS_DECODE_WINDOWS; window++) {
		gt64260_cpu_cs_set_window(window, 0, 0);
	}

	gt64260_cpu_boot_set_window(0, 0);

	for (pci_bus=0; pci_bus<GT64260_PCI_BUSES; pci_bus++) {
		gt64260_cpu_set_pci_io_window(pci_bus, 0, 0, 0, 0);

		for (window=0;window<GT64260_PCI_MEM_WINDOWS_PER_BUS;window++) {
			gt64260_cpu_set_pci_mem_window(pci_bus,
						       window,
						       0, 0, 0, 0, 0);
		}
	}

	for (window=0; window<GT64260_CPU_PROT_WINDOWS; window++) {
		gt64260_cpu_prot_set_window(window, 0, 0, 0);
	}

	for (window=0; window<GT64260_CPU_SNOOP_WINDOWS; window++) {
		gt64260_cpu_snoop_set_window(window, 0, 0, 0);
	}

	return;
} /* gt64260_cpu_disable_all_windows() */


/*
 *****************************************************************************
 *
 *	PCI Slave Window Configuration Routines
 *
 *****************************************************************************
 */

int
gt64260_pci_bar_enable(u32 pci_bus,
		       u32 enable_bits)
{
	u32	reg, val;
	int	rc = -1;

	if (pci_bus < GT64260_PCI_BUSES) {
		reg = (pci_bus == 0) ? GT64260_PCI_0_SLAVE_BAR_REG_ENABLES :
				       GT64260_PCI_1_SLAVE_BAR_REG_ENABLES;


		/* Note: '0' enables, '1' disables */
		val = gt_read(reg);
		val |= 0xffffffff;	/* Disable everything by default */
		val &= ~enable_bits;
		gt_write(reg, val);

		gt_read(reg); /* Flush FIFO */

		rc = 0;
	}

	return rc;
} /* gt64260_pci_bar_enable() */

static int
gt64260_pci_slave_set_window(struct pci_controller *hose,
			     u32 pci_base_addr,
			     u32 cpu_base_addr,
			     u32 bar_size,
			     u32 pci_cfg_fcn,
			     u32 pci_cfg_hdr_offset,
			     u32 bar_size_reg,
			     u32 remap_reg)
{
	u32	val;
	int	devfn;
	u8	save_exclude;

	pci_base_addr &= 0xfffff000;
	cpu_base_addr &= 0xfffff000;
	bar_size &= 0xfffff000;
	devfn = PCI_DEVFN(0, pci_cfg_fcn);

	gt_write(bar_size_reg, (bar_size - 1) & 0xfffff000);
	gt_write(remap_reg, cpu_base_addr);
	gt_read(remap_reg); /* Flush FIFO */

	save_exclude = gt64260_pci_exclude_bridge;
	gt64260_pci_exclude_bridge = FALSE;
	early_read_config_dword(hose,
				hose->first_busno,
				devfn,
				pci_cfg_hdr_offset,
				&val);
	val &= 0x0000000f;
	early_write_config_dword(hose,
				 hose->first_busno,
				 devfn,
				 pci_cfg_hdr_offset,
				 pci_base_addr | val);
	gt64260_pci_exclude_bridge = save_exclude;

	return 0;
} /* gt64260_pci_slave_set_window() */

int
gt64260_pci_slave_scs_set_window(struct pci_controller *hose,
				 u32 window,
				 u32 pci_base_addr,
				 u32 cpu_base_addr,
				 u32 size)
{
	static u32
	pci_scs_windows[GT64260_PCI_BUSES][GT64260_PCI_SCS_WINDOWS][4] = {
		{ /* PCI 0 */
			{ 0, 0x10,
			  GT64260_PCI_0_SLAVE_SCS_0_SIZE,
			  GT64260_PCI_0_SLAVE_SCS_0_REMAP },
			{ 0, 0x14,
			  GT64260_PCI_0_SLAVE_SCS_1_SIZE,
			  GT64260_PCI_0_SLAVE_SCS_1_REMAP },
			{ 0, 0x18,
			  GT64260_PCI_0_SLAVE_SCS_2_SIZE,
			  GT64260_PCI_0_SLAVE_SCS_2_REMAP },
			{ 0, 0x1c,
			  GT64260_PCI_0_SLAVE_SCS_3_SIZE,
			  GT64260_PCI_0_SLAVE_SCS_3_REMAP },
		},
		{ /* PCI 1 */
			{ 0, 0x10,
			  GT64260_PCI_1_SLAVE_SCS_0_SIZE,
			  GT64260_PCI_1_SLAVE_SCS_0_REMAP },
			{ 0, 0x14,
			  GT64260_PCI_1_SLAVE_SCS_1_SIZE,
			  GT64260_PCI_1_SLAVE_SCS_1_REMAP },
			{ 0, 0x18,
			  GT64260_PCI_1_SLAVE_SCS_2_SIZE,
			  GT64260_PCI_1_SLAVE_SCS_2_REMAP },
			{ 0, 0x1c,
			  GT64260_PCI_1_SLAVE_SCS_3_SIZE,
			  GT64260_PCI_1_SLAVE_SCS_3_REMAP },
		}
	}; /* pci_scs_windows[][][] */
	int	pci_bus;
	int	rc = -1;

	if (window < GT64260_PCI_SCS_WINDOWS) {
		pci_bus = (hose->first_busno == 0) ? 0 : 1;

		rc = gt64260_pci_slave_set_window(
				hose,
				pci_base_addr,
				cpu_base_addr,
				size,
				pci_scs_windows[pci_bus][window][0],
				pci_scs_windows[pci_bus][window][1],
				pci_scs_windows[pci_bus][window][2],
				pci_scs_windows[pci_bus][window][3]);
	}

	return rc;
} /* gt64260_pci_slave_scs_set_window() */

int
gt64260_pci_slave_cs_set_window(struct pci_controller *hose,
				u32 window,
				u32 pci_base_addr,
				u32 cpu_base_addr,
				u32 size)
{
	static u32
	pci_cs_windows[GT64260_PCI_BUSES][GT64260_PCI_CS_WINDOWS][4] = {
		{ /* PCI 0 */
			{ 1, 0x10,
			  GT64260_PCI_0_SLAVE_CS_0_SIZE,
			  GT64260_PCI_0_SLAVE_CS_0_REMAP },
			{ 1, 0x14,
			  GT64260_PCI_0_SLAVE_CS_1_SIZE,
			  GT64260_PCI_0_SLAVE_CS_1_REMAP },
			{ 1, 0x18,
			  GT64260_PCI_0_SLAVE_CS_2_SIZE,
			  GT64260_PCI_0_SLAVE_CS_2_REMAP },
			{ 1, 0x1c,
			  GT64260_PCI_0_SLAVE_CS_3_SIZE,
			  GT64260_PCI_0_SLAVE_CS_3_REMAP },
		},
		{ /* PCI 1 */
			{ 1, 0x10,
			  GT64260_PCI_1_SLAVE_CS_0_SIZE,
			  GT64260_PCI_1_SLAVE_CS_0_REMAP },
			{ 1, 0x14,
			  GT64260_PCI_1_SLAVE_CS_1_SIZE,
			  GT64260_PCI_1_SLAVE_CS_1_REMAP },
			{ 1, 0x18,
			  GT64260_PCI_1_SLAVE_CS_2_SIZE,
			  GT64260_PCI_1_SLAVE_CS_2_REMAP },
			{ 1, 0x1c,
			  GT64260_PCI_1_SLAVE_CS_3_SIZE,
			  GT64260_PCI_1_SLAVE_CS_3_REMAP },
		}
	}; /* pci_cs_windows[][][] */
	int	pci_bus;
	int	rc = -1;

	if (window < GT64260_PCI_CS_WINDOWS) {
		pci_bus = (hose->first_busno == 0) ? 0 : 1;

		rc = gt64260_pci_slave_set_window(
				hose,
				pci_base_addr,
				cpu_base_addr,
				size,
				pci_cs_windows[pci_bus][window][0],
				pci_cs_windows[pci_bus][window][1],
				pci_cs_windows[pci_bus][window][2],
				pci_cs_windows[pci_bus][window][3]);
	}

	return rc;
} /* gt64260_pci_slave_cs_set_window() */

int
gt64260_pci_slave_boot_set_window(struct pci_controller *hose,
				  u32 pci_base_addr,
				  u32 cpu_base_addr,
				  u32 size)
{
	int	rc;

	rc = gt64260_pci_slave_set_window(hose,
					  pci_base_addr,
					  cpu_base_addr,
					  size,
					  1,
					  0x20,
					  GT64260_PCI_1_SLAVE_BOOT_SIZE,
					  GT64260_PCI_1_SLAVE_BOOT_REMAP);

	return rc;
} /* gt64260_pci_slave_boot_set_window() */

int
gt64260_pci_slave_p2p_mem_set_window(struct pci_controller *hose,
				     u32 window,
				     u32 pci_base_addr,
				     u32 other_bus_base_addr,
				     u32 size)
{
	static u32
	pci_p2p_mem_windows[GT64260_PCI_BUSES][GT64260_PCI_P2P_MEM_WINDOWS][4]={
		{ /* PCI 0 */
			{ 2, 0x10,
			  GT64260_PCI_0_SLAVE_P2P_MEM_0_SIZE,
			  GT64260_PCI_0_SLAVE_P2P_MEM_0_REMAP_LO },
			{ 2, 0x14,
			  GT64260_PCI_0_SLAVE_P2P_MEM_1_SIZE,
			  GT64260_PCI_0_SLAVE_P2P_MEM_1_REMAP_LO },
		},
		{ /* PCI 1 */
			{ 2, 0x10,
			  GT64260_PCI_1_SLAVE_P2P_MEM_0_SIZE,
			  GT64260_PCI_1_SLAVE_P2P_MEM_0_REMAP_LO },
			{ 2, 0x14,
			  GT64260_PCI_1_SLAVE_P2P_MEM_1_SIZE,
			  GT64260_PCI_1_SLAVE_P2P_MEM_1_REMAP_LO },
		}
	}; /* pci_p2p_mem_windows[][][] */
	int	pci_bus;
	int	rc = -1;

	if (window < GT64260_PCI_P2P_MEM_WINDOWS) {
		pci_bus = (hose->first_busno == 0) ? 0 : 1;

		rc = gt64260_pci_slave_set_window(
				hose,
				pci_base_addr,
				other_bus_base_addr,
				size,
				pci_p2p_mem_windows[pci_bus][window][0],
				pci_p2p_mem_windows[pci_bus][window][1],
				pci_p2p_mem_windows[pci_bus][window][2],
				pci_p2p_mem_windows[pci_bus][window][3]);
	}

	return rc;
} /* gt64260_pci_slave_p2p_mem_set_window() */

int
gt64260_pci_slave_p2p_io_set_window(struct pci_controller *hose,
				    u32 pci_base_addr,
				    u32 other_bus_base_addr,
				    u32 size)
{
	int	rc;

	rc = gt64260_pci_slave_set_window(hose,
					  pci_base_addr,
					  other_bus_base_addr,
					  size,
					  2,
					  0x18,
					  GT64260_PCI_1_SLAVE_P2P_IO_SIZE,
					  GT64260_PCI_1_SLAVE_P2P_IO_REMAP);

	return rc;
} /* gt64260_pci_slave_p2p_io_set_window() */

int
gt64260_pci_slave_dac_scs_set_window(struct pci_controller *hose,
				     u32 window,
				     u32 pci_base_addr_hi,
				     u32 pci_base_addr_lo,
				     u32 cpu_base_addr,
				     u32 size)
{
	static u32
	pci_dac_scs_windows[GT64260_PCI_BUSES][GT64260_PCI_DAC_SCS_WINDOWS][5]={
		{ /* PCI 0 */
			{ 4, 0x10, 0x14,
			  GT64260_PCI_0_SLAVE_DAC_SCS_0_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_SCS_0_REMAP },
			{ 4, 0x18, 0x1c,
			  GT64260_PCI_0_SLAVE_DAC_SCS_1_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_SCS_1_REMAP },
			{ 5, 0x10, 0x14,
			  GT64260_PCI_0_SLAVE_DAC_SCS_2_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_SCS_2_REMAP },
			{ 5, 0x18, 0x1c,
			  GT64260_PCI_0_SLAVE_DAC_SCS_3_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_SCS_3_REMAP },
		},
		{ /* PCI 1 */
			{ 4, 0x10, 0x14,
			  GT64260_PCI_1_SLAVE_DAC_SCS_0_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_SCS_0_REMAP },
			{ 4, 0x18, 0x1c,
			  GT64260_PCI_1_SLAVE_DAC_SCS_1_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_SCS_1_REMAP },
			{ 5, 0x10, 0x14,
			  GT64260_PCI_1_SLAVE_DAC_SCS_2_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_SCS_2_REMAP },
			{ 5, 0x18, 0x1c,
			  GT64260_PCI_1_SLAVE_DAC_SCS_3_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_SCS_3_REMAP },
		}
	}; /* pci_dac_scs_windows[][][] */
	int	pci_bus;
	int	rc = -1;

	if (window < GT64260_PCI_DAC_SCS_WINDOWS) {
		pci_bus = (hose->first_busno == 0) ? 0 : 1;

		rc = gt64260_pci_slave_set_window(
				hose,
				pci_base_addr_lo,
				cpu_base_addr,
				size,
				pci_dac_scs_windows[pci_bus][window][0],
				pci_dac_scs_windows[pci_bus][window][1],
				pci_dac_scs_windows[pci_bus][window][3],
				pci_dac_scs_windows[pci_bus][window][4]);

		early_write_config_dword(
			hose,
			hose->first_busno,
			PCI_DEVFN(0, pci_dac_scs_windows[pci_bus][window][0]),
			pci_dac_scs_windows[pci_bus][window][2],
			pci_base_addr_hi);
	}

	return rc;
} /* gt64260_pci_slave_dac_scs_set_window() */

int
gt64260_pci_slave_dac_cs_set_window(struct pci_controller *hose,
				    u32 window,
				    u32 pci_base_addr_hi,
				    u32 pci_base_addr_lo,
				    u32 cpu_base_addr,
				    u32 size)
{
	static u32
	pci_dac_cs_windows[GT64260_PCI_BUSES][GT64260_PCI_DAC_CS_WINDOWS][5] = {
		{ /* PCI 0 */
			{ 6, 0x10, 0x14,
			  GT64260_PCI_0_SLAVE_DAC_CS_0_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_CS_0_REMAP },
			{ 6, 0x18, 0x1c,
			  GT64260_PCI_0_SLAVE_DAC_CS_1_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_CS_1_REMAP },
			{ 6, 0x20, 0x24,
			  GT64260_PCI_0_SLAVE_DAC_CS_2_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_CS_2_REMAP },
			{ 7, 0x10, 0x14,
			  GT64260_PCI_0_SLAVE_DAC_CS_3_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_CS_3_REMAP },
		},
		{ /* PCI 1 */
			{ 6, 0x10, 0x14,
			  GT64260_PCI_1_SLAVE_DAC_CS_0_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_CS_0_REMAP },
			{ 6, 0x18, 0x1c,
			  GT64260_PCI_1_SLAVE_DAC_CS_1_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_CS_1_REMAP },
			{ 6, 0x20, 0x24,
			  GT64260_PCI_1_SLAVE_DAC_CS_2_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_CS_2_REMAP },
			{ 7, 0x10, 0x14,
			  GT64260_PCI_1_SLAVE_DAC_CS_3_SIZE,
			  GT64260_PCI_1_SLAVE_DAC_CS_3_REMAP },
		}
	}; /* pci_dac_cs_windows[][][] */
	int	pci_bus;
	int	rc = -1;

	if (window < GT64260_PCI_CS_WINDOWS) {
		pci_bus = (hose->first_busno == 0) ? 0 : 1;

		rc = gt64260_pci_slave_set_window(
				hose,
				pci_base_addr_lo,
				cpu_base_addr,
				size,
				pci_dac_cs_windows[pci_bus][window][0],
				pci_dac_cs_windows[pci_bus][window][1],
				pci_dac_cs_windows[pci_bus][window][3],
				pci_dac_cs_windows[pci_bus][window][4]);

		early_write_config_dword(
			hose,
			hose->first_busno,
			PCI_DEVFN(0, pci_dac_cs_windows[pci_bus][window][0]),
			pci_dac_cs_windows[pci_bus][window][2],
			pci_base_addr_hi);
	}

	return rc;
} /* gt64260_pci_slave_dac_cs_set_window() */

int
gt64260_pci_slave_dac_boot_set_window(struct pci_controller *hose,
				      u32 pci_base_addr_hi,
				      u32 pci_base_addr_lo,
				      u32 cpu_base_addr,
				      u32 size)
{
	int	rc;

	rc = gt64260_pci_slave_set_window(hose,
					  pci_base_addr_lo,
					  cpu_base_addr,
					  size,
					  7,
					  0x18,
					  GT64260_PCI_1_SLAVE_BOOT_SIZE,
					  GT64260_PCI_1_SLAVE_BOOT_REMAP);

	early_write_config_dword(hose,
				 hose->first_busno,
				 PCI_DEVFN(0, 7),
				 0x1c,
				 pci_base_addr_hi);

	return rc;
} /* gt64260_pci_slave_dac_boot_set_window() */

int
gt64260_pci_slave_dac_p2p_mem_set_window(struct pci_controller *hose,
				         u32 window,
				         u32 pci_base_addr_hi,
				         u32 pci_base_addr_lo,
				         u32 other_bus_base_addr,
				         u32 size)
{
	static u32
	pci_dac_p2p_mem_windows[GT64260_PCI_BUSES][GT64260_PCI_DAC_P2P_MEM_WINDOWS][5] = {
		{ /* PCI 0 */
			{ 4, 0x20, 0x24,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_0_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_0_REMAP_LO },
			{ 5, 0x20, 0x24,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_1_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_1_REMAP_LO },
		},
		{ /* PCI 1 */
			{ 4, 0xa0, 0xa4,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_0_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_0_REMAP_LO },
			{ 5, 0xa0, 0xa4,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_1_SIZE,
			  GT64260_PCI_0_SLAVE_DAC_P2P_MEM_1_REMAP_LO },
		}
	}; /* pci_dac_p2p_windows[][][] */
	int	pci_bus;
	int	rc = -1;

	if (window < GT64260_PCI_P2P_MEM_WINDOWS) {
		pci_bus = (hose->first_busno == 0) ? 0 : 1;

		rc = gt64260_pci_slave_set_window(
				hose,
				pci_base_addr_lo,
				other_bus_base_addr,
				size,
				pci_dac_p2p_mem_windows[pci_bus][window][0],
				pci_dac_p2p_mem_windows[pci_bus][window][1],
				pci_dac_p2p_mem_windows[pci_bus][window][3],
				pci_dac_p2p_mem_windows[pci_bus][window][4]);

		early_write_config_dword(
		    hose,
		    hose->first_busno,
		    PCI_DEVFN(0, pci_dac_p2p_mem_windows[pci_bus][window][0]),
		    pci_dac_p2p_mem_windows[pci_bus][window][2],
		    pci_base_addr_hi);
	}

	return rc;
} /* gt64260_pci_slave_dac_p2p_mem_set_window() */


/*
 *****************************************************************************
 *
 *	PCI Control Configuration Routines
 *
 *****************************************************************************
 */


int
gt64260_pci_acc_cntl_set_window(u32 pci_bus,
			        u32 window,
			        u32 base_addr_hi,
			        u32 base_addr_lo,
			        u32 size,
			        u32 features)
{
	static u32
	pci_acc_cntl_windows[GT64260_PCI_BUSES][GT64260_PCI_ACC_CNTL_WINDOWS][3] = {
		{ /* PCI 0 */
			{ GT64260_PCI_0_ACC_CNTL_0_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_0_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_0_TOP },

			{ GT64260_PCI_0_ACC_CNTL_1_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_1_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_1_TOP },

			{ GT64260_PCI_0_ACC_CNTL_2_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_2_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_2_TOP },

			{ GT64260_PCI_0_ACC_CNTL_3_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_3_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_3_TOP },

			{ GT64260_PCI_0_ACC_CNTL_4_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_4_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_4_TOP },

			{ GT64260_PCI_0_ACC_CNTL_5_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_5_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_5_TOP },

			{ GT64260_PCI_0_ACC_CNTL_6_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_6_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_6_TOP },

			{ GT64260_PCI_0_ACC_CNTL_7_BASE_HI,
			  GT64260_PCI_0_ACC_CNTL_7_BASE_LO,
			  GT64260_PCI_0_ACC_CNTL_7_TOP },
		},
		{ /* PCI 1 */
			{ GT64260_PCI_1_ACC_CNTL_0_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_0_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_0_TOP },

			{ GT64260_PCI_1_ACC_CNTL_1_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_1_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_1_TOP },

			{ GT64260_PCI_1_ACC_CNTL_2_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_2_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_2_TOP },

			{ GT64260_PCI_1_ACC_CNTL_3_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_3_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_3_TOP },

			{ GT64260_PCI_1_ACC_CNTL_4_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_4_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_4_TOP },

			{ GT64260_PCI_1_ACC_CNTL_5_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_5_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_5_TOP },

			{ GT64260_PCI_1_ACC_CNTL_6_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_6_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_6_TOP },

			{ GT64260_PCI_1_ACC_CNTL_7_BASE_HI,
			  GT64260_PCI_1_ACC_CNTL_7_BASE_LO,
			  GT64260_PCI_1_ACC_CNTL_7_TOP },
		}
	}; /* pci_acc_cntl_windows[][][] */
	int	rc = -1;

	if ((pci_bus < GT64260_PCI_BUSES) &&
	    (window < GT64260_PCI_ACC_CNTL_WINDOWS)) {

		rc = gt64260_set_64bit_window(
			      base_addr_hi,
			      base_addr_lo,
			      size,
			      features,
			      pci_acc_cntl_windows[pci_bus][window][0],
			      pci_acc_cntl_windows[pci_bus][window][1],
			      pci_acc_cntl_windows[pci_bus][window][2]);
	}

	return rc;
} /* gt64260_pci_acc_cntl_set_window() */

int
gt64260_pci_snoop_set_window(u32 pci_bus,
			     u32 window,
			     u32 base_addr_hi,
			     u32 base_addr_lo,
			     u32 size,
			     u32 snoop_type)
{
	static u32
	pci_snoop_windows[GT64260_PCI_BUSES][GT64260_PCI_SNOOP_WINDOWS][3] = {
		{ /* PCI 0 */
			{ GT64260_PCI_0_SNOOP_0_BASE_HI,
			  GT64260_PCI_0_SNOOP_0_BASE_LO,
			  GT64260_PCI_0_SNOOP_0_TOP },

			{ GT64260_PCI_0_SNOOP_1_BASE_HI,
			  GT64260_PCI_0_SNOOP_1_BASE_LO,
			  GT64260_PCI_0_SNOOP_1_TOP },

			{ GT64260_PCI_0_SNOOP_2_BASE_HI,
			  GT64260_PCI_0_SNOOP_2_BASE_LO,
			  GT64260_PCI_0_SNOOP_2_TOP },

			{ GT64260_PCI_0_SNOOP_3_BASE_HI,
			  GT64260_PCI_0_SNOOP_3_BASE_LO,
			  GT64260_PCI_0_SNOOP_3_TOP },
		},
		{ /* PCI 1 */
			{ GT64260_PCI_1_SNOOP_0_BASE_HI,
			  GT64260_PCI_1_SNOOP_0_BASE_LO,
			  GT64260_PCI_1_SNOOP_0_TOP },

			{ GT64260_PCI_1_SNOOP_1_BASE_HI,
			  GT64260_PCI_1_SNOOP_1_BASE_LO,
			  GT64260_PCI_1_SNOOP_1_TOP },

			{ GT64260_PCI_1_SNOOP_2_BASE_HI,
			  GT64260_PCI_1_SNOOP_2_BASE_LO,
			  GT64260_PCI_1_SNOOP_2_TOP },

			{ GT64260_PCI_1_SNOOP_3_BASE_HI,
			  GT64260_PCI_1_SNOOP_3_BASE_LO,
			  GT64260_PCI_1_SNOOP_3_TOP },
		},
	}; /* pci_snoop_windows[][][] */
	int	rc = -1;

	if ((pci_bus < GT64260_PCI_BUSES) &&
	    (window < GT64260_PCI_SNOOP_WINDOWS)) {

		rc = gt64260_set_64bit_window(
			      base_addr_hi,
			      base_addr_lo,
			      size,
			      snoop_type,
			      pci_snoop_windows[pci_bus][window][0],
			      pci_snoop_windows[pci_bus][window][1],
			      pci_snoop_windows[pci_bus][window][2]);
	}

	return rc;
} /* gt64260_pci_snoop_set_window() */

/*
 *****************************************************************************
 *
 *	64260's Register Base Address Routines
 *
 *****************************************************************************
 */

/*
 * gt64260_remap_bridge_regs()
 *
 * Move the bridge's register to the specified base address.
 * Assume that there are no other windows overlapping this area and that
 * all but the highest 3 nibbles are 0.
 */
int
gt64260_set_base(u32 new_base)
{
	u32	val;
	int	limit = 100000;
	int	rc = 0;

	val = gt_read(GT64260_INTERNAL_SPACE_DECODE);
	val = (new_base >> 20) | (val & 0xffff0000);
	gt_write(GT64260_INTERNAL_SPACE_DECODE, val);

	iounmap((void *)gt64260_base);
	gt64260_base = (u32)ioremap((new_base & 0xfff00000),
				    GT64260_INTERNAL_SPACE_SIZE);

	do { /* Wait for bridge to move its regs */
		val = gt_read(GT64260_INTERNAL_SPACE_DECODE);
	} while ((val != 0xffffffff) && (limit-- > 0));

	if (limit <= 0) {
		rc = -1;
	}

	return rc;
} /* gt64260_remap_bridge_regs() */

/*
 * gt64260_get_base()
 *
 * Return the current virtual base address of the 64260's registers.
 */
int
gt64260_get_base(u32 *base)
{
	*base = gt64260_base;
	return 0;
} /* gt64260_remap_bridge_regs() */

/*
 *****************************************************************************
 *
 *	Exclude PCI config space access to bridge itself
 *
 *****************************************************************************
 */

/*
 * gt64260_exclude_pci_device()
 *
 * This routine causes the PCI subsystem to skip the PCI device in slot 0
 * (which is the 64260 itself) unless explicitly allowed.
 */
int
gt64260_pci_exclude_device(u8 bus, u8 devfn)
{
	struct pci_controller	*hose;

	hose = pci_bus_to_hose(bus);

	/* Skip slot 0 and 1 on both hoses */
	if ((gt64260_pci_exclude_bridge == TRUE) &&
	    (PCI_SLOT(devfn) == 0) &&
	    (hose->first_busno == bus)) {

		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	else {
		return PCIBIOS_SUCCESSFUL;
	}
} /* gt64260_pci_exclude_device() */

#if defined(CONFIG_SERIAL_TEXT_DEBUG)

/*
 * gt64260_putc()
 *
 * Dump a character out the MPSC port for gt64260_mpsc_progress
 * this assumes the baud rate has already been set up and the
 * MPSC initialized by the bootloader or firmware.
 */

static inline void
gt_putc(char c){
	mb();
	gt_write(GT64260_MPSC_0_CHR_1, c);
	mb();
	gt_write(GT64260_MPSC_0_CHR_2, 0x200);
	mb();

	udelay(10000);
}

void
puthex(unsigned long val){

        int i;

        for (i = 7;  i >= 0;  i--) {
		gt_putc("0123456789ABCDEF"[(val>>28) & 0x0f]);
		val <<= 4;
	}
	gt_putc('\r');
	gt_putc('\n');

}


void
gt64260_mpsc_progress(char *s, unsigned short hex){
	/* spit stuff out the 64260 mpsc */

	volatile char	c;
	while ((c = *s++) != 0){
		gt_putc(c);
		if ( c == '\n' ) gt_putc('\r');
	}
	gt_putc('\n');
	gt_putc('\r');

	return;
}

#endif /* CONFIG_DEBUG_TEXT */
