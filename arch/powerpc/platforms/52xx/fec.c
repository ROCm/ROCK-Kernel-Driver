/*
 * arch/ppc/syslib/bestcomm/fec.c
 *
 * Driver for MPC52xx processor BestComm FEC controller
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
#include <linux/string.h>
#include <linux/types.h>
#include <asm/errno.h>
#include <asm/io.h>

#include <asm/mpc52xx.h>

#include "bestcomm.h"
#include "fec.h"

/*
 * Initialize FEC receive task.
 * Returns task number of FEC receive task.
 * Returns -1 on failure
 */
int sdma_fec_rx_init(struct sdma *s, phys_addr_t fifo, int maxbufsize)
{
	struct sdma_fec_rx_var *var;
	struct sdma_fec_rx_inc *inc;

	static int tasknum = -1;
	static struct sdma_bd *bd = 0;
	static u32 bd_pa;

	if (tasknum < 0) {
		tasknum = sdma_load_task(sdma_fec_rx_task);
		if (tasknum < 0)
			return tasknum;
	}

	if (!bd)
		bd = (struct sdma_bd *)sdma_sram_alloc(sizeof(*bd) * s->num_bd,
								SDMA_BD_ALIGN, &bd_pa);
	if (!bd)
		return -ENOMEM;

	sdma_disable_task(tasknum);

	s->tasknum = tasknum;
	s->bd = bd;
	s->flags = SDMA_FLAGS_NONE;
	s->index = 0;
	s->outdex = 0;
	memset(bd, 0, sizeof(*bd) * s->num_bd);

	var = (struct sdma_fec_rx_var *)sdma_task_var(tasknum);
	var->enable			= sdma_io_pa(&sdma.io->tcr[tasknum]);
	var->fifo			= fifo;
	var->bd_base		= bd_pa;
	var->bd_last		= bd_pa + (s->num_bd - 1)*sizeof(struct sdma_bd);
	var->bd_start		= bd_pa;
	var->buffer_size	= maxbufsize;

	/* These are constants, they should have been in the image file */
	inc = (struct sdma_fec_rx_inc *)sdma_task_inc(tasknum);
	inc->incr_bytes		= -(s16)sizeof(u32);
	inc->incr_dst		= sizeof(u32);
	inc->incr_dst_ma	= sizeof(u8);

	sdma_set_task_pragma(tasknum, SDMA_FEC_RX_BD_PRAGMA);
	sdma_set_task_auto_start(tasknum, tasknum);

	/* clear pending interrupt bits */
	out_be32(&sdma.io->IntPend, 1<<tasknum);

	out_8(&sdma.io->ipr[SDMA_INITIATOR_FEC_RX], SDMA_IPR_FEC_RX);

	return tasknum;
}

/*
 * Return 2nd to last DRD
 * This is an ugly hack, but at least it's only done once at initialization
 */
static u32 *self_modified_drd(int tasknum)
{
	u32 *desc;
	int num_descs;
	int drd_count;
	int i;

	num_descs = sdma_task_num_descs(tasknum);
	desc = sdma_task_desc(tasknum) + num_descs - 1;
	drd_count = 0;
	for (i=0; i<num_descs; i++, desc--)
		if (sdma_desc_is_drd(*desc) && ++drd_count == 3)
			break;
	return desc;
}

/*
 * Initialize FEC transmit task.
 * Returns task number of FEC transmit task.
 * Returns -1 on failure
 */
int sdma_fec_tx_init(struct sdma *s, phys_addr_t fifo)
{
	struct sdma_fec_tx_var *var;
	struct sdma_fec_tx_inc *inc;

	static int tasknum = -1;
	static struct sdma_bd *bd = 0;
	static u32 bd_pa;

	if (tasknum < 0) {
		tasknum = sdma_load_task(sdma_fec_tx_task);
		if (tasknum < 0)
			return tasknum;
	}

	if (!bd)
		bd = (struct sdma_bd *)sdma_sram_alloc(sizeof(*bd) * s->num_bd,
								SDMA_BD_ALIGN, &bd_pa);
	if (!bd)
		return -ENOMEM;

	sdma_disable_task(tasknum);

	s->tasknum = tasknum;
	s->bd = bd;
	s->flags = SDMA_FLAGS_ENABLE_TASK;
	s->index = 0;
	s->outdex = 0;
	memset(bd, 0, sizeof(*bd) * s->num_bd);

	var = (struct sdma_fec_tx_var *)sdma_task_var(tasknum);
	var->DRD		= sdma_sram_pa(self_modified_drd(tasknum));
	var->fifo		= fifo;
	var->enable		= sdma_io_pa(&sdma.io->tcr[tasknum]);
	var->bd_base	= bd_pa;
	var->bd_last	= bd_pa + (s->num_bd - 1)*sizeof(struct sdma_bd);
	var->bd_start	= bd_pa;

	/* These are constants, they should have been in the image file */
	inc = (struct sdma_fec_tx_inc *)sdma_task_inc(tasknum);
	inc->incr_bytes		= -(s16)sizeof(u32);
	inc->incr_src		= sizeof(u32);
	inc->incr_src_ma	= sizeof(u8);

	sdma_set_task_pragma(tasknum, SDMA_FEC_TX_BD_PRAGMA);
	sdma_set_task_auto_start(tasknum, tasknum);

	/* clear pending interrupt bits */
	out_be32(&sdma.io->IntPend, 1<<tasknum);

	out_8(&sdma.io->ipr[SDMA_INITIATOR_FEC_TX], SDMA_IPR_FEC_TX);

	return tasknum;
}

EXPORT_SYMBOL(sdma_fec_rx_init);
EXPORT_SYMBOL(sdma_fec_tx_init);
