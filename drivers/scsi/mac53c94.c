/*
 * SCSI low-level driver for the 53c94 SCSI bus adaptor found
 * on Power Macintosh computers, controlling the external SCSI chain.
 * We assume the 53c94 is connected to a DBDMA (descriptor-based DMA)
 * controller.
 *
 * Paul Mackerras, August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <asm/dbdma.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/pci-bridge.h>

#include "scsi.h"
#include "hosts.h"
#include "mac53c94.h"

enum fsc_phase {
	idle,
	selecting,
	dataing,
	completing,
	busfreeing,
};

struct fsc_state {
	volatile struct	mac53c94_regs *regs;
	int	intr;
	volatile struct	dbdma_regs *dma;
	int	dmaintr;
	int	clk_freq;
	struct	Scsi_Host *host;
	struct	fsc_state *next;
	Scsi_Cmnd *request_q;
	Scsi_Cmnd *request_qtail;
	Scsi_Cmnd *current_req;		/* req we're currently working on */
	enum fsc_phase phase;		/* what we're currently trying to do */
	struct dbdma_cmd *dma_cmds;	/* space for dbdma commands, aligned */
	void	*dma_cmd_space;
	struct	pci_dev *pdev;
	dma_addr_t dma_addr;
};

static struct fsc_state *all_53c94s;

static void mac53c94_init(struct fsc_state *);
static void mac53c94_start(struct fsc_state *);
static void mac53c94_interrupt(int, void *, struct pt_regs *);
static irqreturn_t do_mac53c94_interrupt(int, void *, struct pt_regs *);
static void cmd_done(struct fsc_state *, int result);
static void set_dma_cmds(struct fsc_state *, Scsi_Cmnd *);
static int data_goes_out(Scsi_Cmnd *);

int
mac53c94_detect(Scsi_Host_Template *tp)
{
	struct device_node *node;
	int nfscs;
	struct fsc_state *state, **prev_statep;
	struct Scsi_Host *host;
	void *dma_cmd_space;
	unsigned char *clkprop;
	int proplen;
	struct pci_dev *pdev;
	u8 pbus, devfn;

	nfscs = 0;
	prev_statep = &all_53c94s;
	for (node = find_devices("53c94"); node != 0; node = node->next) {
		if (node->n_addrs != 2 || node->n_intrs != 2) {
			printk(KERN_ERR "mac53c94: expected 2 addrs and intrs"
			       " (got %d/%d) for node %s\n",
			       node->n_addrs, node->n_intrs, node->full_name);
			continue;
		}

		pdev = NULL;
		if (node->parent != NULL
		    && !pci_device_from_OF_node(node->parent, &pbus, &devfn))
			pdev = pci_find_slot(pbus, devfn);
		if (pdev == NULL) {
			printk(KERN_ERR "mac53c94: can't find PCI device "
			       "for %s\n", node->full_name);
			continue;
		}

		host = scsi_register(tp, sizeof(struct fsc_state));
		if (host == NULL)
			break;
		host->unique_id = nfscs;

		state = (struct fsc_state *) host->hostdata;
		if (state == 0) {
			/* "can't happen" */
			printk(KERN_ERR "mac53c94: no state for %s?!\n",
			       node->full_name);
			scsi_unregister(host);
			break;
		}
		state->host = host;
		state->pdev = pdev;

		state->regs = (volatile struct mac53c94_regs *)
			ioremap(node->addrs[0].address, 0x1000);
		state->intr = node->intrs[0].line;
		state->dma = (volatile struct dbdma_regs *)
			ioremap(node->addrs[1].address, 0x1000);
		state->dmaintr = node->intrs[1].line;
		if (state->regs == NULL || state->dma == NULL) {
			printk(KERN_ERR "mac53c94: ioremap failed for %s\n",
			       node->full_name);
			if (state->dma != NULL)
				iounmap(state->dma);
			if (state->regs != NULL)
				iounmap(state->regs);
			scsi_unregister(host);
			break;
		}

		clkprop = get_property(node, "clock-frequency", &proplen);
		if (clkprop == NULL || proplen != sizeof(int)) {
			printk(KERN_ERR "%s: can't get clock frequency, "
			       "assuming 25MHz\n", node->full_name);
			state->clk_freq = 25000000;
		} else
			state->clk_freq = *(int *)clkprop;

		/* Space for dma command list: +1 for stop command,
		   +1 to allow for aligning. */
		dma_cmd_space = kmalloc((host->sg_tablesize + 2) *
					sizeof(struct dbdma_cmd), GFP_KERNEL);
		if (dma_cmd_space == 0) {
			printk(KERN_ERR "mac53c94: couldn't allocate dma "
			       "command space for %s\n", node->full_name);
			goto err_cleanup;
		}
		state->dma_cmds = (struct dbdma_cmd *)
			DBDMA_ALIGN(dma_cmd_space);
		memset(state->dma_cmds, 0, (host->sg_tablesize + 1)
		       * sizeof(struct dbdma_cmd));
		state->dma_cmd_space = dma_cmd_space;

		*prev_statep = state;
		prev_statep = &state->next;

		if (request_irq(state->intr, do_mac53c94_interrupt, 0,
				"53C94", state)) {
			printk(KERN_ERR "mac53C94: can't get irq %d for %s\n",
			       state->intr, node->full_name);
		err_cleanup:
			iounmap(state->dma);
			iounmap(state->regs);
			scsi_unregister(host);
			break;
		}

		mac53c94_init(state);

		++nfscs;
	}
	return nfscs;
}

int
mac53c94_release(struct Scsi_Host *host)
{
	struct fsc_state *fp = (struct fsc_state *) host->hostdata;

	if (fp == 0)
		return 0;
	if (fp->regs)
		iounmap((void *) fp->regs);
	if (fp->dma)
		iounmap((void *) fp->dma);
	kfree(fp->dma_cmd_space);
	free_irq(fp->intr, fp);
	return 0;
}

int
mac53c94_queue(Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	struct fsc_state *state;

#if 0
	if (data_goes_out(cmd)) {
		int i;
		printk(KERN_DEBUG "mac53c94_queue %p: command is", cmd);
		for (i = 0; i < cmd->cmd_len; ++i)
			printk(" %.2x", cmd->cmnd[i]);
		printk("\n" KERN_DEBUG "use_sg=%d request_bufflen=%d request_buffer=%p\n",
		       cmd->use_sg, cmd->request_bufflen, cmd->request_buffer);
	}
#endif

	cmd->scsi_done = done;
	cmd->host_scribble = NULL;

	state = (struct fsc_state *) cmd->device->host->hostdata;

	if (state->request_q == NULL)
		state->request_q = cmd;
	else
		state->request_qtail->host_scribble = (void *) cmd;
	state->request_qtail = cmd;

	if (state->phase == idle)
		mac53c94_start(state);

	return 0;
}

int
mac53c94_abort(Scsi_Cmnd *cmd)
{
	return SCSI_ABORT_SNOOZE;
}

int
mac53c94_host_reset(Scsi_Cmnd *cmd)
{
	struct fsc_state *state = (struct fsc_state *) cmd->device->host->hostdata;
	volatile struct mac53c94_regs *regs = state->regs;
	volatile struct dbdma_regs *dma = state->dma;

	st_le32(&dma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
	regs->command = CMD_SCSI_RESET;	/* assert RST */
	eieio();
	udelay(100);			/* leave it on for a while (>= 25us) */
	regs->command = CMD_RESET;
	eieio();
	udelay(20);
	mac53c94_init(state);
	regs->command = CMD_NOP;
	eieio();
	return SUCCESS;
}

static void
mac53c94_init(struct fsc_state *state)
{
	volatile struct mac53c94_regs *regs = state->regs;
	volatile struct dbdma_regs *dma = state->dma;
	int x;

	regs->config1 = state->host->this_id | CF1_PAR_ENABLE;
	regs->sel_timeout = TIMO_VAL(250);	/* 250ms */
	regs->clk_factor = CLKF_VAL(state->clk_freq);
	regs->config2 = CF2_FEATURE_EN;
	regs->config3 = 0;
	regs->sync_period = 0;
	regs->sync_offset = 0;
	eieio();
	x = regs->interrupt;
	st_le32(&dma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
}

/*
 * Start the next command for a 53C94.
 * Should be called with interrupts disabled.
 */
static void
mac53c94_start(struct fsc_state *state)
{
	Scsi_Cmnd *cmd;
	volatile struct mac53c94_regs *regs = state->regs;
	int i;

	if (state->phase != idle || state->current_req != NULL)
		panic("inappropriate mac53c94_start (state=%p)", state);
	if (state->request_q == NULL)
		return;
	state->current_req = cmd = state->request_q;
	state->request_q = (Scsi_Cmnd *) cmd->host_scribble;

	/* Off we go */
	regs->count_lo = 0;
	regs->count_mid = 0;
	regs->count_hi = 0;
	eieio();
	regs->command = CMD_NOP + CMD_DMA_MODE;
	udelay(1);
	eieio();
	regs->command = CMD_FLUSH;
	udelay(1);
	eieio();
	regs->dest_id = cmd->device->id;
	regs->sync_period = 0;
	regs->sync_offset = 0;
	eieio();

	/* load the command into the FIFO */
	for (i = 0; i < cmd->cmd_len; ++i) {
		regs->fifo = cmd->cmnd[i];
		eieio();
	}

	/* do select without ATN XXX */
	regs->command = CMD_SELECT;
	state->phase = selecting;

	if (cmd->use_sg > 0 || cmd->request_bufflen != 0)
		set_dma_cmds(state, cmd);
}

static irqreturn_t
do_mac53c94_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	unsigned long flags;
	struct Scsi_Host *dev = ((struct fsc_state *) dev_id)->current_req->device->host;
	
	spin_lock_irqsave(dev->host_lock, flags);
	mac53c94_interrupt(irq, dev_id, ptregs);
	spin_unlock_irqrestore(dev->host_lock, flags);
	return IRQ_HANDLED;
}

static void
mac53c94_interrupt(int irq, void *dev_id, struct pt_regs *ptregs)
{
	struct fsc_state *state = (struct fsc_state *) dev_id;
	volatile struct mac53c94_regs *regs = state->regs;
	volatile struct dbdma_regs *dma = state->dma;
	Scsi_Cmnd *cmd = state->current_req;
	int nb, stat, seq, intr;
	static int mac53c94_errors;
	int dma_dir;

	/*
	 * Apparently, reading the interrupt register unlatches
	 * the status and sequence step registers.
	 */
	seq = regs->seqstep;
	stat = regs->status;
	intr = regs->interrupt;

#if 0
	printk(KERN_DEBUG "mac53c94_intr, intr=%x stat=%x seq=%x phase=%d\n",
	       intr, stat, seq, state->phase);
#endif

	if (intr & INTR_RESET) {
		/* SCSI bus was reset */
		printk(KERN_INFO "external SCSI bus reset detected\n");
		regs->command = CMD_NOP;
		st_le32(&dma->control, RUN << 16);	/* stop dma */
		cmd_done(state, DID_RESET << 16);
		return;
	}
	if (intr & INTR_ILL_CMD) {
		printk(KERN_ERR "53c94: invalid cmd, intr=%x stat=%x seq=%x phase=%d\n",
		       intr, stat, seq, state->phase);
		cmd_done(state, DID_ERROR << 16);
		return;
	}
	if (stat & STAT_ERROR) {
#if 0
		/* XXX these seem to be harmless? */
		printk("53c94: bad error, intr=%x stat=%x seq=%x phase=%d\n",
		       intr, stat, seq, state->phase);
#endif
		++mac53c94_errors;
		regs->command = CMD_NOP + CMD_DMA_MODE;
		eieio();
	}
	if (cmd == 0) {
		printk(KERN_DEBUG "53c94: interrupt with no command active?\n");
		return;
	}
	if (stat & STAT_PARITY) {
		printk(KERN_ERR "mac53c94: parity error\n");
		cmd_done(state, DID_PARITY << 16);
		return;
	}
	switch (state->phase) {
	case selecting:
		if (intr & INTR_DISCONNECT) {
			/* selection timed out */
			cmd_done(state, DID_BAD_TARGET << 16);
			return;
		}
		if (intr != INTR_BUS_SERV + INTR_DONE) {
			printk(KERN_DEBUG "got intr %x during selection\n", intr);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		if ((seq & SS_MASK) != SS_DONE) {
			printk(KERN_DEBUG "seq step %x after command\n", seq);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		regs->command = CMD_NOP;
		/* set DMA controller going if any data to transfer */
		if ((stat & (STAT_MSG|STAT_CD)) == 0
		    && (cmd->use_sg > 0 || cmd->request_bufflen != 0)) {
			nb = cmd->SCp.this_residual;
			if (nb > 0xfff0)
				nb = 0xfff0;
			cmd->SCp.this_residual -= nb;
			regs->count_lo = nb;
			regs->count_mid = nb >> 8;
			eieio();
			regs->command = CMD_DMA_MODE + CMD_NOP;
			eieio();
			st_le32(&dma->cmdptr, virt_to_phys(state->dma_cmds));
			st_le32(&dma->control, (RUN << 16) | RUN);
			eieio();
			regs->command = CMD_DMA_MODE + CMD_XFER_DATA;
			state->phase = dataing;
			break;
		} else if ((stat & STAT_PHASE) == STAT_CD + STAT_IO) {
			/* up to status phase already */
			regs->command = CMD_I_COMPLETE;
			state->phase = completing;
		} else {
			printk(KERN_DEBUG "in unexpected phase %x after cmd\n",
			       stat & STAT_PHASE);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		break;

	case dataing:
		if (intr != INTR_BUS_SERV) {
			printk(KERN_DEBUG "got intr %x before status\n", intr);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		if (cmd->SCp.this_residual != 0
		    && (stat & (STAT_MSG|STAT_CD)) == 0) {
			/* Set up the count regs to transfer more */
			nb = cmd->SCp.this_residual;
			if (nb > 0xfff0)
				nb = 0xfff0;
			cmd->SCp.this_residual -= nb;
			regs->count_lo = nb;
			regs->count_mid = nb >> 8;
			eieio();
			regs->command = CMD_DMA_MODE + CMD_NOP;
			eieio();
			regs->command = CMD_DMA_MODE + CMD_XFER_DATA;
			break;
		}
		if ((stat & STAT_PHASE) != STAT_CD + STAT_IO) {
			printk(KERN_DEBUG "intr %x before data xfer complete\n", intr);
		}
		out_le32(&dma->control, RUN << 16);	/* stop dma */
		dma_dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);
		if (cmd->use_sg != 0) {
			pci_unmap_sg(state->pdev,
				(struct scatterlist *)cmd->request_buffer,
				cmd->use_sg, dma_dir);
		} else {
			pci_unmap_single(state->pdev, state->dma_addr,
				cmd->request_bufflen, dma_dir);
		}
		/* should check dma status */
		regs->command = CMD_I_COMPLETE;
		state->phase = completing;
		break;
	case completing:
		if (intr != INTR_DONE) {
			printk(KERN_DEBUG "got intr %x on completion\n", intr);
			cmd_done(state, DID_ERROR << 16);
			return;
		}
		cmd->SCp.Status = regs->fifo; eieio();
		cmd->SCp.Message = regs->fifo; eieio();
		cmd->result = 
		regs->command = CMD_ACCEPT_MSG;
		state->phase = busfreeing;
		break;
	case busfreeing:
		if (intr != INTR_DISCONNECT) {
			printk(KERN_DEBUG "got intr %x when expected disconnect\n", intr);
		}
		cmd_done(state, (DID_OK << 16) + (cmd->SCp.Message << 8)
			 + cmd->SCp.Status);
		break;
	default:
		printk(KERN_DEBUG "don't know about phase %d\n", state->phase);
	}
}

static void
cmd_done(struct fsc_state *state, int result)
{
	Scsi_Cmnd *cmd;

	cmd = state->current_req;
	if (cmd != 0) {
		cmd->result = result;
		(*cmd->scsi_done)(cmd);
		state->current_req = NULL;
	}
	state->phase = idle;
	mac53c94_start(state);
}

/*
 * Set up DMA commands for transferring data.
 */
static void
set_dma_cmds(struct fsc_state *state, Scsi_Cmnd *cmd)
{
	int i, dma_cmd, total;
	struct scatterlist *scl;
	struct dbdma_cmd *dcmds;
	dma_addr_t dma_addr;
	u32 dma_len;
	int dma_dir = scsi_to_pci_dma_dir(cmd->sc_data_direction);

	dma_cmd = data_goes_out(cmd)? OUTPUT_MORE: INPUT_MORE;
	dcmds = state->dma_cmds;
	if (cmd->use_sg > 0) {
		int nseg;

		total = 0;
		scl = (struct scatterlist *) cmd->buffer;
		nseg = pci_map_sg(state->pdev, scl, cmd->use_sg, dma_dir);
		for (i = 0; i < nseg; ++i) {
			dma_addr = sg_dma_address(scl);
			dma_len = sg_dma_len(scl);
			if (dma_len > 0xffff)
				panic("mac53c94: scatterlist element >= 64k");
			total += dma_len;
			st_le16(&dcmds->req_count, dma_len);
			st_le16(&dcmds->command, dma_cmd);
			st_le32(&dcmds->phy_addr, dma_addr);
			dcmds->xfer_status = 0;
			++scl;
			++dcmds;
		}
	} else {
		total = cmd->request_bufflen;
		if (total > 0xffff)
			panic("mac53c94: transfer size >= 64k");
		dma_addr = pci_map_single(state->pdev, cmd->request_buffer,
					  total, dma_dir);
		state->dma_addr = dma_addr;
		st_le16(&dcmds->req_count, total);
		st_le32(&dcmds->phy_addr, dma_addr);
		dcmds->xfer_status = 0;
		++dcmds;
	}
	dma_cmd += OUTPUT_LAST - OUTPUT_MORE;
	st_le16(&dcmds[-1].command, dma_cmd);
	st_le16(&dcmds->command, DBDMA_STOP);
	cmd->SCp.this_residual = total;
}

/*
 * Work out whether data will be going out from the host adaptor or into it.
 */
static int
data_goes_out(Scsi_Cmnd *cmd)
{
	switch (cmd->sc_data_direction) {
	case SCSI_DATA_WRITE:
		return 1;
	case SCSI_DATA_READ:
		return 0;
	}

	/* for SCSI_DATA_UNKNOWN or SCSI_DATA_NONE, fall back on the
	   old method for now... */
	switch (cmd->cmnd[0]) {
	case CHANGE_DEFINITION: 
	case COMPARE:	  
	case COPY:
	case COPY_VERIFY:	    
	case FORMAT_UNIT:	 
	case LOG_SELECT:
	case MEDIUM_SCAN:	  
	case MODE_SELECT:
	case MODE_SELECT_10:
	case REASSIGN_BLOCKS: 
	case RESERVE:
	case SEARCH_EQUAL:	  
	case SEARCH_EQUAL_12: 
	case SEARCH_HIGH:	 
	case SEARCH_HIGH_12:  
	case SEARCH_LOW:
	case SEARCH_LOW_12:
	case SEND_DIAGNOSTIC: 
	case SEND_VOLUME_TAG:	     
	case SET_WINDOW: 
	case UPDATE_BLOCK:	
	case WRITE_BUFFER:
 	case WRITE_6:	
	case WRITE_10:	
	case WRITE_12:	  
	case WRITE_LONG:	
	case WRITE_LONG_2:      /* alternate code for WRITE_LONG */
	case WRITE_SAME:	
	case WRITE_VERIFY:
	case WRITE_VERIFY_12:
		return 1;
	default:
		return 0;
	}
}

static Scsi_Host_Template driver_template = {
	.proc_name	= "53c94",
	.name		= "53C94",
	.detect		= mac53c94_detect,
	.release	= mac53c94_release,
	.queuecommand	= mac53c94_queue,
	.eh_abort_handler = mac53c94_abort,
	.eh_host_reset_handler = mac53c94_host_reset,
	.can_queue	= 1,
	.this_id	= 7,
	.sg_tablesize	= SG_ALL,
	.cmd_per_lun	= 1,
	.use_clustering	= DISABLE_CLUSTERING,
};

#include "scsi_module.c"
