/*
 *  drivers/mtd/nand/au1550nd.c
 *
 *  Copyright (C) 2004 Embedded Edge, LLC
 *
 * $Id: au1550nd.c,v 1.5 2004/05/17 07:19:35 ppopov Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>
#include <asm/io.h>
#include <asm/au1000.h>
#ifdef CONFIG_MIPS_PB1550
#include <asm/pb1550.h> 
#endif
#ifdef CONFIG_MIPS_DB1550
#include <asm/db1x00.h> 
#endif


/*
 * MTD structure for NAND controller
 */
static struct mtd_info *au1550_mtd = NULL;
static volatile u32 p_nand;
static int nand_width = 1; /* default, only x8 supported for now */

/* Internal buffers. Page buffer and oob buffer for one block*/
static u_char data_buf[512 + 16];
static u_char oob_buf[16 * 32];

/*
 * Define partitions for flash device
 */
const static struct mtd_partition partition_info[] = {
#ifdef CONFIG_MIPS_PB1550
#define NUM_PARTITIONS            2
	{ 
		.name = "Pb1550 NAND FS 0",
	  	.offset = 0,
	  	.size = 8*1024*1024 
	},
	{ 
		.name = "Pb1550 NAND FS 1",
		.offset =  MTDPART_OFS_APPEND,
 		.size =    MTDPART_SIZ_FULL
	}
#endif
#ifdef CONFIG_MIPS_DB1550
#define NUM_PARTITIONS            2
	{ 
		.name = "Db1550 NAND FS 0",
	  	.offset = 0,
	  	.size = 8*1024*1024 
	},
	{ 
		.name = "Db1550 NAND FS 1",
		.offset =  MTDPART_OFS_APPEND,
 		.size =    MTDPART_SIZ_FULL
	}
#endif
};

static inline void write_cmd_reg(u8 cmd)
{
	if (nand_width)
		*((volatile u8 *)(p_nand + MEM_STNAND_CMD)) = cmd;
	else
		*((volatile u16 *)(p_nand + MEM_STNAND_CMD)) = cmd;
	au_sync();
}

static inline void write_addr_reg(u8 addr)
{
	if (nand_width)
		*((volatile u8 *)(p_nand + MEM_STNAND_ADDR)) = addr;
	else
		*((volatile u16 *)(p_nand + MEM_STNAND_ADDR)) = addr;
	au_sync();
}

static inline void write_data_reg(u8 data)
{
	if (nand_width)
		*((volatile u8 *)(p_nand + MEM_STNAND_DATA)) = data;
	else
		*((volatile u16 *)(p_nand + MEM_STNAND_DATA)) = data;
	au_sync();
}

static inline u32 read_data_reg(void)
{
	u32 data;
	if (nand_width) {
		data = *((volatile u8 *)(p_nand + MEM_STNAND_DATA));
		au_sync();
	}
	else {
		data = *((volatile u16 *)(p_nand + MEM_STNAND_DATA));
		au_sync();
	}
	return data;
}

void au1550_hwcontrol(struct mtd_info *mtd, int cmd)
{
}

int au1550_device_ready(struct mtd_info *mtd)
{
	int ready;
	ready = (au_readl(MEM_STSTAT) & 0x1) ? 1 : 0;
	return ready;
}

static u_char au1550_nand_read_byte(struct mtd_info *mtd)
{
	u_char ret;
	ret = read_data_reg();
	return ret;
}

static void au1550_nand_write_byte(struct mtd_info *mtd, u_char byte)
{
	write_data_reg((u8)byte);
}

static void 
au1550_nand_write_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;

	for (i=0; i<len; i++)
		write_data_reg(buf[i]);
}

static void 
au1550_nand_read_buf(struct mtd_info *mtd, u_char *buf, int len)
{
	int i;

	for (i=0; i<len; i++)
		buf[i] = (u_char)read_data_reg();
}

static int 
au1550_nand_verify_buf(struct mtd_info *mtd, const u_char *buf, int len)
{
	int i;

	for (i=0; i<len; i++)
		if (buf[i] != (u_char)read_data_reg())
			return -EFAULT;

	return 0;
}

static void au1550_nand_select_chip(struct mtd_info *mtd, int chip)
{
	switch(chip) {
	case -1:
		/* deassert chip enable */
		au_writel(au_readl(MEM_STNDCTL) & ~0x20 , MEM_STNDCTL);
		break;
	case 0:
		/* assert (force assert) chip enable */
		au_writel(au_readl(MEM_STNDCTL) | 0x20 , MEM_STNDCTL);
		break;

	default:
		BUG();
	}
}

static void au1550_nand_command (struct mtd_info *mtd, unsigned command, 
		int column, int page_addr)
{
	register struct nand_chip *this = mtd->priv;

	/*
	 * Write out the command to the device.
	 */
	if (command == NAND_CMD_SEQIN) {
		int readcmd;

		if (column >= mtd->oobblock) {
			/* OOB area */
			column -= mtd->oobblock;
			readcmd = NAND_CMD_READOOB;
		} else if (column < 256) {
			/* First 256 bytes --> READ0 */
			readcmd = NAND_CMD_READ0;
		} else {
			column -= 256;
			readcmd = NAND_CMD_READ1;
		}
		write_cmd_reg(readcmd);
	}
	write_cmd_reg(command);

	if (column != -1 || page_addr != -1) {

		/* Serially input address */
		if (column != -1)
			write_addr_reg(column);
		if (page_addr != -1) {
			write_addr_reg((unsigned char) (page_addr & 0xff));
			write_addr_reg(((page_addr >> 8) & 0xff));
			/* One more address cycle for higher density devices */
			if (mtd->size & 0x0c000000) 
				write_addr_reg((unsigned char) ((page_addr >> 16) & 0x0f));
		}
	}
	
	switch (command) {
			
	case NAND_CMD_PAGEPROG:
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
	case NAND_CMD_SEQIN:
	case NAND_CMD_STATUS:
		break;

	case NAND_CMD_RESET:
		if (this->dev_ready)	
			break;
		udelay(this->chip_delay);
		write_cmd_reg(NAND_CMD_STATUS);
		while ( !(read_data_reg() & 0x40));
		return;

	/* This applies to read commands */	
	default:
		udelay (this->chip_delay);
	}
	
	/* wait until command is processed */
	while (!this->dev_ready(mtd));
}


/*
 * Main initialization routine
 */
int __init au1550_init (void)
{
	struct nand_chip *this;
	u16 boot_swapboot = 0; /* default value */
	u32 mem_time;

	/* Allocate memory for MTD device structure and private data */
	au1550_mtd = kmalloc (sizeof(struct mtd_info) + 
			sizeof (struct nand_chip), GFP_KERNEL);
	if (!au1550_mtd) {
		printk ("Unable to allocate NAND MTD dev structure.\n");
		return -ENOMEM;
	}

	/* Get pointer to private data */
	this = (struct nand_chip *) (&au1550_mtd[1]);

	/* Initialize structures */
	memset((char *) au1550_mtd, 0, sizeof(struct mtd_info));
	memset((char *) this, 0, sizeof(struct nand_chip));

	/* Link the private data with the MTD structure */
	au1550_mtd->priv = this;

	/* disable interrupts */
	au_writel(au_readl(MEM_STNDCTL) & ~(1<<8), MEM_STNDCTL);

	/* disable NAND boot */
	au_writel(au_readl(MEM_STNDCTL) & ~(1<<0), MEM_STNDCTL);

#ifdef CONFIG_MIPS_PB1550
	/* set gpio206 high */
	au_writel(au_readl(GPIO2_DIR) & ~(1<<6), GPIO2_DIR);

	boot_swapboot = (au_readl(MEM_STSTAT) & (0x7<<1)) | 
		((bcsr->status >> 6)  & 0x1);
	switch (boot_swapboot) {
		case 0:
		case 2:
		case 8:
		case 0xC:
		case 0xD:
			/* x16 NAND Flash */
			nand_width = 0;
			printk("Pb1550 NAND: 16-bit NAND not supported by MTD\n");
			break;
		case 1:
		case 9:
		case 3:
		case 0xE:
		case 0xF:
			/* x8 NAND Flash */
			nand_width = 1;
			break;
		default:
			printk("Pb1550 NAND: bad boot:swap\n");
			kfree(au1550_mtd);
			return 1;
	}

	/* Configure RCE1 - should be done by YAMON */
	au_writel(0x5 | (nand_width << 22), MEM_STCFG1);
	au_writel(NAND_TIMING, MEM_STTIME1);
	mem_time = au_readl(MEM_STTIME1);
	au_sync();

	/* setup and enable chip select */
	/* we really need to decode offsets only up till 0x20 */
	au_writel((1<<28) | (NAND_PHYS_ADDR>>4) | 
			(((NAND_PHYS_ADDR + 0x1000)-1) & (0x3fff<<18)>>18), 
			MEM_STADDR1);
	au_sync();
#endif

#ifdef CONFIG_MIPS_DB1550
	/* Configure RCE1 - should be done by YAMON */
	au_writel(0x00400005, MEM_STCFG1);
	au_writel(0x00007774, MEM_STTIME1);
	au_writel(0x12000FFF, MEM_STADDR1);
#endif

	p_nand = (volatile struct nand_regs *)ioremap(NAND_PHYS_ADDR, 0x1000);

	/* Set address of hardware control function */
	this->hwcontrol = au1550_hwcontrol;
	this->dev_ready = au1550_device_ready;
	/* 30 us command delay time */
	this->chip_delay = 30;		

	this->cmdfunc = au1550_nand_command;
	this->select_chip = au1550_nand_select_chip;
	this->write_byte = au1550_nand_write_byte;
	this->read_byte = au1550_nand_read_byte;
	this->write_buf = au1550_nand_write_buf;
	this->read_buf = au1550_nand_read_buf;
	this->verify_buf = au1550_nand_verify_buf;
	this->eccmode = NAND_ECC_SOFT;

	/* Set internal data buffer */
	this->data_buf = data_buf;
	this->oob_buf = oob_buf;

	/* Scan to find existence of the device */
	if (nand_scan (au1550_mtd, 1)) {
		kfree (au1550_mtd);
		return -ENXIO;
	}

	/* Register the partitions */
	add_mtd_partitions(au1550_mtd, partition_info, NUM_PARTITIONS);

	return 0;
}

module_init(au1550_init);

/*
 * Clean up routine
 */
#ifdef MODULE
static void __exit au1550_cleanup (void)
{
	struct nand_chip *this = (struct nand_chip *) &au1550_mtd[1];

	iounmap ((void *)p_nand);

	/* Unregister partitions */
	del_mtd_partitions(au1550_mtd);

	/* Unregister the device */
	del_mtd_device (au1550_mtd);

	/* Free the MTD device structure */
	kfree (au1550_mtd);
}
module_exit(au1550_cleanup);
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Embedded Edge, LLC");
MODULE_DESCRIPTION("Board-specific glue layer for NAND flash on Pb1550 board");
