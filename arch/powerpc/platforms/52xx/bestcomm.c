/*
 * arch/ppc/syslib/bestcomm/bestcomm.c
 *
 * Driver for MPC52xx processor BestComm peripheral controller
 *
 * Author: Dale Farnsworth <dfarnsworth@mvista.com>
 *
 * 2003-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * HISTORY:
 *
 * 2005-08-14	Converted to platform driver by
 *		Andrey Volkov <avolkov@varma-el.com>, Varma Electronics Oy
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <asm/bug.h>
#include <asm/io.h>
#include <asm/mpc52xx.h>
#include <asm/of_platform.h>

#include "bestcomm.h"

#define DRIVER_NAME "mpc52xx-bestcomm"

struct sdma_io sdma;
struct device_node *sdma_node;
struct device_node *sram_node;

static spinlock_t sdma_lock = SPIN_LOCK_UNLOCKED;

#ifdef CONFIG_BESTCOMM_DEBUG
void sdma_dump(void)
{
	int i;
	printk("** SDMA registers: pa = %.8lx, va = %p\n",
	       sdma.base_reg_addr, sdma.io);
	printk("**  taskBar = %08x\n", sdma.io->taskBar);
	printk("**  currentPointer = %08x\n", sdma.io->currentPointer);
	printk("**  endPointer = %08x\n", sdma.io->endPointer);
	printk("**  variablePointer = %08x\n", sdma.io->variablePointer);

	printk("**  IntVect1 = %08x\n", sdma.io->IntVect1);
	printk("**  IntVect2 = %08x\n", sdma.io->IntVect2);
	printk("**  PtdCntrl = %08x\n", sdma.io->PtdCntrl);

	printk("**  IntPend = %08x\n", sdma.io->IntPend);
	printk("**  IntMask = %08x\n", sdma.io->IntMask);

	printk("**  TCR dump:");

	for (i=0;i<16;i++)  {
		if(i%8 == 0)
			printk("\n**   %02X:",i);
		printk(" %04X",sdma.io->tcr[i]);
	}
	printk("\n**  IPR dump:");
	for (i=0;i<32;i++)  {
		if(i%16 == 0)
			printk("\n**   %02X:",i);
		printk(" %02X",sdma.io->ipr[i]);
	}
	printk("\n**  cReqSelect = %08x\n", sdma.io->cReqSelect);
	printk("**  task_size0 = %08x\n", sdma.io->task_size0);
	printk("**  task_size1 = %08x\n", sdma.io->task_size1);
	printk("**  MDEDebug = %08x\n", sdma.io->MDEDebug);
	printk("**  ADSDebug = %08x\n", sdma.io->ADSDebug);
	printk("**  Value1 = %08x\n", sdma.io->Value1);
	printk("**  Value2 = %08x\n", sdma.io->Value2);
	printk("**  Control = %08x\n", sdma.io->Control);
	printk("**  Status = %08x\n", sdma.io->Status);
	printk("**  PTDDebug = %08x\n", sdma.io->PTDDebug);
}
#endif

#ifdef CONFIG_BESTCOMM_DEBUG
#define SDMA_DUMP_REGS()	sdma_dump()
#else
#define SDMA_DUMP_REGS()
#endif

/*
 * Use a very simple SRAM allocator.
 * There is no mechanism for freeing space.
 * In an attempt to minimize internal fragmentation, the SRAM is
 * divided into two areas.
 *
 * Area 1 is at the beginning of SRAM
 * and is used for allocations requiring alignments of 16 bytes or less.
 * Successive allocations return higher addresses.
 *
 * Area 2 is at the end of SRAM and is used for the remaining allocations.
 * Successive allocations return lower addresses.
 *
 * I've considered adding routines to support the freeing of SRAM allocations,
 * but the SRAM is so small (16K) that fragmentation can quickly cause the
 * SRAM to be unusable.  If you can come up with a slick way to free SRAM
 * memory without the fragmentation problem, please do so.
 */

static u8 *area1_end;
static u8 *area2_begin;

void *sdma_sram_alloc(int size, int alignment, u32 *dma_handle)
{
	u8 *a;

	spin_lock(&sdma_lock);

	/* alignment must be a power of 2 */
	BUG_ON(alignment & (alignment - 1));

	if (alignment < 16) {
		a = (u8 *)(((u32)area1_end + (alignment-1)) & ~(alignment-1));
		if (a + size <= area2_begin)
			area1_end = a + size;
		else
			a = 0;				/* out of memory */
	} else {
		a = (u8 *)(((u32)area2_begin - size) & ~(alignment - 1));
		if (a >= area1_end)
			area2_begin = a;
		else
			a = 0;				/* out of memory */
	}
	if(a && dma_handle)
		*dma_handle = sdma_sram_pa(a);
	spin_unlock(&sdma_lock);
	return (void *)a;
}

/* this will need to be updated if Freescale changes their task code FDT */
static u32 fdt_ops[] = {
	0xa0045670,	/* FDT[48] */
	0x80045670,	/* FDT[49] */
	0x21800000,	/* FDT[50] */
	0x21e00000,	/* FDT[51] */
	0x21500000,	/* FDT[52] */
	0x21400000,	/* FDT[53] */
	0x21500000,	/* FDT[54] */
	0x20400000,	/* FDT[55] */
	0x20500000,	/* FDT[56] */
	0x20800000,	/* FDT[57] */
	0x20a00000,	/* FDT[58] */
	0xc0170000,	/* FDT[59] */
	0xc0145670,	/* FDT[60] */
	0xc0345670,	/* FDT[61] */
	0xa0076540,	/* FDT[62] */
	0xa0000760,	/* FDT[63] */
};

static int new_task_number(void)
{
	struct sdma_tdt *tdt;
	int i;

	spin_lock(&sdma_lock);

	tdt = sdma.tdt;
	for (i=0; i<SDMA_MAX_TASKS; i++, tdt++)
		if (tdt->start == 0)
			break;
	if (i == SDMA_MAX_TASKS)
		i = -1;

	spin_unlock(&sdma_lock);

	return i;
}

int sdma_load_task(u32 *task_image)
{
	struct sdma_task_header *head = (struct sdma_task_header *)task_image;
	struct sdma_tdt *tdt;
	int tasknum;
	u32 *desc;
	u32 *var_src, *var_dst;
	u32 *inc_src;
	void *start;

	BUG_ON(head->magic != SDMA_TASK_MAGIC);

	tasknum = new_task_number();
	if (tasknum < 0)
		return -ENOMEM;

	desc = (u32 *)(head + 1);
	var_src = desc + head->desc_size;
	inc_src = var_src + head->var_size;

	tdt = &sdma.tdt[tasknum];

	start = sdma_sram_alloc(head->desc_size * sizeof(u32), 4, &tdt->start);
	if (!start)
		return -ENOMEM;
	tdt->stop = tdt->start + (head->desc_size - 1)*sizeof(u32);
	var_dst = sdma_sram_va(tdt->var);

	memcpy(start, desc, head->desc_size * sizeof(u32));
	memcpy(&var_dst[head->first_var], var_src, head->var_size * sizeof(u32));
	memcpy(&var_dst[SDMA_MAX_VAR], inc_src, head->inc_size * sizeof(u32));

	return tasknum;
}

void sdma_set_initiator(int task, int initiator)
{
	int i;
	int num_descs;
	u32 *desc;
	int next_drd_has_initiator;

	sdma_set_tcr_initiator(task, initiator);

	desc = sdma_task_desc(task);
	next_drd_has_initiator = 1;
	num_descs = sdma_task_num_descs(task);

	for (i=0; i<num_descs; i++, desc++) {
		if (!sdma_desc_is_drd(*desc))
			continue;
		if (next_drd_has_initiator)
			if (sdma_desc_initiator(*desc) != SDMA_INITIATOR_ALWAYS)
				sdma_set_desc_initiator(desc, initiator);
		next_drd_has_initiator = !sdma_drd_is_extended(*desc);
	}
}

struct sdma *sdma_alloc(int queue_size)
{
	struct sdma *s = kmalloc(sizeof(*s), GFP_KERNEL);
	void **cookie;

	if (!s)
		return NULL;

	memset(s, 0, sizeof(*s));

	if (queue_size) {
		cookie = kmalloc(sizeof(*cookie) * queue_size, GFP_KERNEL);
		if (!cookie) {
			kfree(s);
			return NULL;
		}
		s->cookie = cookie;
	}

	s->num_bd = queue_size;
	s->node = sdma_node;
	return s;
}
EXPORT_SYMBOL_GPL(sdma_alloc);

void sdma_free(struct sdma *s)
{
	if (s->cookie)
		kfree(s->cookie);
	kfree(s);
}

static int __init mpc52xx_sdma_init(void)
{
	int task;
	u32 *context;
	u32 *fdt;
	struct sdma_tdt *tdt;
	struct resource mem_io, mem_sram;
	u32 tdt_pa, var_pa, context_pa, fdt_pa;
	int ret = -ENODEV;

	/* Find SDMA registers */
	sdma_node = of_find_compatible_node(NULL, "dma-controller", "mpc5200-bestcomm");
	if (!sdma_node) {
		goto out;
	}

	if ((ret = of_address_to_resource(sdma_node, 0, &mem_io)) != 0) {
		printk(KERN_ERR "Could not get address of SDMA controller\n");
		goto out;
	}

	/* Find SRAM location */
	sram_node = of_find_compatible_node(NULL, "sram", "mpc5200-sram");
	if (!sram_node) {
		printk (KERN_ERR DRIVER_NAME ": could not locate SRAM\n");
		goto out;
	}

	if ((ret = of_address_to_resource(sram_node, 0, &mem_sram)) != 0) {
		printk(KERN_ERR "Could not get address of SRAM\n");
		goto out;
	}

	/* Map register regions */
	if (!request_mem_region(mem_io.start, mem_io.end - mem_io.start + 1,
	                        DRIVER_NAME)) {
		printk(KERN_ERR DRIVER_NAME " - resource unavailable\n");
		goto out;
	}
	sdma.base_reg_addr = mem_io.start;

	sdma.io = ioremap_nocache(mem_io.start, sizeof(struct mpc52xx_sdma));

	if (!sdma.io ) {
		printk(KERN_ERR DRIVER_NAME " - failed to map sdma regs\n");
		ret = -ENOMEM;
		goto map_io_error;
	}

	SDMA_DUMP_REGS();

	sdma.sram_size = mem_sram.end - mem_sram.start + 1;
	if (!request_mem_region(mem_sram.start, sdma.sram_size, DRIVER_NAME)) {
		printk(KERN_ERR DRIVER_NAME " - resource unavailable\n");
		goto req_sram_error;
	}

	sdma.base_sram_addr = mem_sram.start;
	sdma.sram = ioremap_nocache(mem_sram.start, sdma.sram_size);
	if (!sdma.sram ) {
		printk(KERN_ERR DRIVER_NAME " - failed to map sdma sram\n");
		ret = -ENOMEM;
		goto map_sram_error;
	}

	area1_end = sdma.sram;
	area2_begin = area1_end + sdma.sram_size;

	memset(area1_end, 0, sdma.sram_size);

	/* allocate space for task descriptors, contexts, and var tables */
	sdma.tdt = sdma_sram_alloc(sizeof(struct sdma_tdt) * SDMA_MAX_TASKS, 4, &tdt_pa);

	context = sdma_sram_alloc(SDMA_CONTEXT_SIZE * SDMA_MAX_TASKS,
							  SDMA_CONTEXT_ALIGN, &context_pa);
	sdma.var = sdma_sram_alloc( (SDMA_VAR_SIZE + SDMA_INC_SIZE) * SDMA_MAX_TASKS,
								SDMA_VAR_ALIGN, &var_pa);
	fdt = sdma_sram_alloc(SDMA_FDT_SIZE, SDMA_FDT_ALIGN, &fdt_pa);
	memcpy(&fdt[48], fdt_ops, sizeof(fdt_ops));

	out_be32(&sdma.io->taskBar, tdt_pa);

	tdt = sdma.tdt;
	for (task=0; task < SDMA_MAX_TASKS; task++) {
		out_be16(&sdma.io->tcr[task], 0);
		out_8(&sdma.io->ipr[task], 0);

		tdt->context = context_pa;
		tdt->var = var_pa;
		tdt->fdt = fdt_pa;
		var_pa += (SDMA_MAX_VAR + SDMA_MAX_INC)*sizeof(u32);
		context_pa += SDMA_MAX_CONTEXT*sizeof(u32);
		tdt++;
	}

	out_8(&sdma.io->ipr[SDMA_INITIATOR_ALWAYS], SDMA_IPR_ALWAYS);

	/* Disable COMM Bus Prefetch, apparently it's not reliable yet */
	out_be16(&sdma.io->PtdCntrl, in_be16(&sdma.io->PtdCntrl) | 1);

	printk(KERN_INFO "MPC52xx BestComm inited\n");

	return 0;

map_sram_error:
	release_mem_region(mem_sram.start, sdma.sram_size);
req_sram_error:
	iounmap(sdma.io);
map_io_error:
	release_mem_region(mem_io.start, mem_io.end - mem_io.start + 1);
out:
	return ret;
}

subsys_initcall(mpc52xx_sdma_init);


MODULE_DESCRIPTION("Freescale MPC52xx BestComm DMA");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(sdma_sram_alloc);
EXPORT_SYMBOL(sdma_load_task);
EXPORT_SYMBOL(sdma_set_initiator);
EXPORT_SYMBOL(sdma_free);
EXPORT_SYMBOL(sdma);


