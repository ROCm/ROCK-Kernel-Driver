/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Where this Software is combined with software released under the terms of 
 * the GNU Public License ("GPL") and the terms of the GPL would require the 
 * combined work to also be released under the terms of the GPL, the terms
 * and conditions of this License will apply in addition to those of the
 * GPL with the exception of any terms or conditions of this License that
 * conflict with, or are expressly prohibited by, the GPL.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#define SYM_GLUE_C

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_spi.h>

#include "sym_glue.h"
#include "sym_nvram.h"

#define NAME53C		"sym53c"
#define NAME53C8XX	"sym53c8xx"

static int __devinit
pci_get_base_address(struct pci_dev *pdev, int index, u_long *base)
{
	u32 tmp;
#define PCI_BAR_OFFSET(index) (PCI_BASE_ADDRESS_0 + (index<<2))

	pci_read_config_dword(pdev, PCI_BAR_OFFSET(index), &tmp);
	*base = tmp;
	++index;
	if ((tmp & 0x7) == PCI_BASE_ADDRESS_MEM_TYPE_64) {
#if BITS_PER_LONG > 32
		pci_read_config_dword(pdev, PCI_BAR_OFFSET(index), &tmp);
		*base |= (((u_long)tmp) << 32);
#endif
		++index;
	}
	return index;
#undef PCI_BAR_OFFSET
}

/* This lock protects only the memory allocation/free.  */
spinlock_t sym53c8xx_lock = SPIN_LOCK_UNLOCKED;

static struct scsi_transport_template *sym2_transport_template = NULL;

/*
 *  Wrappers to the generic memory allocator.
 */
void *sym_calloc(int size, char *name)
{
	unsigned long flags;
	void *m;
	spin_lock_irqsave(&sym53c8xx_lock, flags);
	m = sym_calloc_unlocked(size, name);
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
	return m;
}

void sym_mfree(void *m, int size, char *name)
{
	unsigned long flags;
	spin_lock_irqsave(&sym53c8xx_lock, flags);
	sym_mfree_unlocked(m, size, name);
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
}

void *__sym_calloc_dma(m_pool_ident_t dev_dmat, int size, char *name)
{
	unsigned long flags;
	void *m;
	spin_lock_irqsave(&sym53c8xx_lock, flags);
	m = __sym_calloc_dma_unlocked(dev_dmat, size, name);
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
	return m;
}

void __sym_mfree_dma(m_pool_ident_t dev_dmat, void *m, int size, char *name)
{
	unsigned long flags;
	spin_lock_irqsave(&sym53c8xx_lock, flags);
	__sym_mfree_dma_unlocked(dev_dmat, m, size, name);
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
}

m_addr_t __vtobus(m_pool_ident_t dev_dmat, void *m)
{
	unsigned long flags;
	m_addr_t b;
	spin_lock_irqsave(&sym53c8xx_lock, flags);
	b = __vtobus_unlocked(dev_dmat, m);
	spin_unlock_irqrestore(&sym53c8xx_lock, flags);
	return b;
}

/*
 *  Driver host data structure.
 */
struct host_data {
	struct sym_hcb *ncb;
};

/*
 *  Used by the eh thread to wait for command completion.
 *  It is allocated on the eh thread stack.
 */
struct sym_eh_wait {
	struct semaphore sem;
	struct timer_list timer;
	void (*old_done)(struct scsi_cmnd *);
	int to_do;
	int timed_out;
};

/*
 *  Driver private area in the SCSI command structure.
 */
struct sym_ucmd {		/* Override the SCSI pointer structure */
	SYM_QUEHEAD link_cmdq;	/* Must stay at offset ZERO */
	dma_addr_t data_mapping;
	u_char	data_mapped;
	struct sym_eh_wait *eh_wait;
};

#define SYM_UCMD_PTR(cmd)  ((struct sym_ucmd *)(&(cmd)->SCp))
#define SYM_SCMD_PTR(ucmd) sym_que_entry(ucmd, struct scsi_cmnd, SCp)
#define SYM_SOFTC_PTR(cmd) (((struct host_data *)cmd->device->host->hostdata)->ncb)

static void __unmap_scsi_data(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	int dma_dir = cmd->sc_data_direction;

	switch(SYM_UCMD_PTR(cmd)->data_mapped) {
	case 2:
		pci_unmap_sg(pdev, cmd->buffer, cmd->use_sg, dma_dir);
		break;
	case 1:
		pci_unmap_single(pdev, SYM_UCMD_PTR(cmd)->data_mapping,
				 cmd->request_bufflen, dma_dir);
		break;
	}
	SYM_UCMD_PTR(cmd)->data_mapped = 0;
}

static dma_addr_t __map_scsi_single_data(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	dma_addr_t mapping;
	int dma_dir = cmd->sc_data_direction;

	mapping = pci_map_single(pdev, cmd->request_buffer,
				 cmd->request_bufflen, dma_dir);
	if (mapping) {
		SYM_UCMD_PTR(cmd)->data_mapped  = 1;
		SYM_UCMD_PTR(cmd)->data_mapping = mapping;
	}

	return mapping;
}

static int __map_scsi_sg_data(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	int use_sg;
	int dma_dir = cmd->sc_data_direction;

	use_sg = pci_map_sg(pdev, cmd->buffer, cmd->use_sg, dma_dir);
	if (use_sg > 0) {
		SYM_UCMD_PTR(cmd)->data_mapped  = 2;
		SYM_UCMD_PTR(cmd)->data_mapping = use_sg;
	}

	return use_sg;
}

static void __sync_scsi_data_for_cpu(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	int dma_dir = cmd->sc_data_direction;

	switch(SYM_UCMD_PTR(cmd)->data_mapped) {
	case 2:
		pci_dma_sync_sg_for_cpu(pdev, cmd->buffer, cmd->use_sg, dma_dir);
		break;
	case 1:
		pci_dma_sync_single_for_cpu(pdev, SYM_UCMD_PTR(cmd)->data_mapping,
					    cmd->request_bufflen, dma_dir);
		break;
	}
}

static void __sync_scsi_data_for_device(struct pci_dev *pdev, struct scsi_cmnd *cmd)
{
	int dma_dir = cmd->sc_data_direction;

	switch(SYM_UCMD_PTR(cmd)->data_mapped) {
	case 2:
		pci_dma_sync_sg_for_device(pdev, cmd->buffer, cmd->use_sg, dma_dir);
		break;
	case 1:
		pci_dma_sync_single_for_device(pdev, SYM_UCMD_PTR(cmd)->data_mapping,
					       cmd->request_bufflen, dma_dir);
		break;
	}
}

#define unmap_scsi_data(np, cmd)	\
		__unmap_scsi_data(np->s.device, cmd)
#define map_scsi_single_data(np, cmd)	\
		__map_scsi_single_data(np->s.device, cmd)
#define map_scsi_sg_data(np, cmd)	\
		__map_scsi_sg_data(np->s.device, cmd)
#define sync_scsi_data_for_cpu(np, cmd)		\
		__sync_scsi_data_for_cpu(np->s.device, cmd)
#define sync_scsi_data_for_device(np, cmd)		\
		__sync_scsi_data_for_device(np->s.device, cmd)

/*
 *  Complete a pending CAM CCB.
 */
void sym_xpt_done(struct sym_hcb *np, struct scsi_cmnd *ccb)
{
	sym_remque(&SYM_UCMD_PTR(ccb)->link_cmdq);
	unmap_scsi_data(np, ccb);
	ccb->scsi_done(ccb);
}

void sym_xpt_done2(struct sym_hcb *np, struct scsi_cmnd *ccb, int cam_status)
{
	sym_set_cam_status(ccb, cam_status);
	sym_xpt_done(np, ccb);
}


/*
 *  Print something that identifies the IO.
 */
void sym_print_addr(struct sym_ccb *cp)
{
	struct scsi_cmnd *cmd = cp->cam_ccb;
	if (cmd)
		printf("%s:%d:%d:", sym_name(SYM_SOFTC_PTR(cmd)),
				cmd->device->id, cmd->device->lun);
}

/*
 *  Tell the SCSI layer about a BUS RESET.
 */
void sym_xpt_async_bus_reset(struct sym_hcb *np)
{
	printf_notice("%s: SCSI BUS has been reset.\n", sym_name(np));
	np->s.settle_time = jiffies + sym_driver_setup.settle_delay * HZ;
	np->s.settle_time_valid = 1;
	if (sym_verbose >= 2)
		printf_info("%s: command processing suspended for %d seconds\n",
			    sym_name(np), sym_driver_setup.settle_delay);
}

/*
 *  Tell the SCSI layer about a BUS DEVICE RESET message sent.
 */
void sym_xpt_async_sent_bdr(struct sym_hcb *np, int target)
{
	printf_notice("%s: TARGET %d has been reset.\n", sym_name(np), target);
}

/*
 *  Tell the SCSI layer about the new transfer parameters.
 */
void sym_xpt_async_nego_wide(struct sym_hcb *np, int target)
{
	if (sym_verbose < 3)
		return;
	sym_announce_transfer_rate(np, target);
}

/*
 *  Choose the more appropriate CAM status if 
 *  the IO encountered an extended error.
 */
static int sym_xerr_cam_status(int cam_status, int x_status)
{
	if (x_status) {
		if	(x_status & XE_PARITY_ERR)
			cam_status = DID_PARITY;
		else if	(x_status &(XE_EXTRA_DATA|XE_SODL_UNRUN|XE_SWIDE_OVRUN))
			cam_status = DID_ERROR;
		else if	(x_status & XE_BAD_PHASE)
			cam_status = DID_ERROR;
		else
			cam_status = DID_ERROR;
	}
	return cam_status;
}

/*
 *  Build CAM result for a failed or auto-sensed IO.
 */
void sym_set_cam_result_error(struct sym_hcb *np, struct sym_ccb *cp, int resid)
{
	struct scsi_cmnd *csio = cp->cam_ccb;
	u_int cam_status, scsi_status, drv_status;

	drv_status  = 0;
	cam_status  = DID_OK;
	scsi_status = cp->ssss_status;

	if (cp->host_flags & HF_SENSE) {
		scsi_status = cp->sv_scsi_status;
		resid = cp->sv_resid;
		if (sym_verbose && cp->sv_xerr_status)
			sym_print_xerr(cp, cp->sv_xerr_status);
		if (cp->host_status == HS_COMPLETE &&
		    cp->ssss_status == S_GOOD &&
		    cp->xerr_status == 0) {
			cam_status = sym_xerr_cam_status(DID_OK,
							 cp->sv_xerr_status);
			drv_status = DRIVER_SENSE;
			/*
			 *  Bounce back the sense data to user.
			 */
			bzero(&csio->sense_buffer, sizeof(csio->sense_buffer));
			memcpy(csio->sense_buffer, cp->sns_bbuf,
			      min(sizeof(csio->sense_buffer),
				  (size_t)SYM_SNS_BBUF_LEN));
#if 0
			/*
			 *  If the device reports a UNIT ATTENTION condition 
			 *  due to a RESET condition, we should consider all 
			 *  disconnect CCBs for this unit as aborted.
			 */
			if (1) {
				u_char *p;
				p  = (u_char *) csio->sense_data;
				if (p[0]==0x70 && p[2]==0x6 && p[12]==0x29)
					sym_clear_tasks(np, DID_ABORT,
							cp->target,cp->lun, -1);
			}
#endif
		} else {
			/*
			 * Error return from our internal request sense.  This
			 * is bad: we must clear the contingent allegiance
			 * condition otherwise the device will always return
			 * BUSY.  Use a big stick.
			 */
			sym_reset_scsi_target(np, csio->device->id);
			cam_status = DID_ERROR;
		}
	} else if (cp->host_status == HS_COMPLETE) 	/* Bad SCSI status */
		cam_status = DID_OK;
	else if (cp->host_status == HS_SEL_TIMEOUT)	/* Selection timeout */
		cam_status = DID_NO_CONNECT;
	else if (cp->host_status == HS_UNEXPECTED)	/* Unexpected BUS FREE*/
		cam_status = DID_ERROR;
	else {						/* Extended error */
		if (sym_verbose) {
			PRINT_ADDR(cp);
			printf ("COMMAND FAILED (%x %x %x).\n",
				cp->host_status, cp->ssss_status,
				cp->xerr_status);
		}
		/*
		 *  Set the most appropriate value for CAM status.
		 */
		cam_status = sym_xerr_cam_status(DID_ERROR, cp->xerr_status);
	}
	csio->resid = resid;
	csio->result = (drv_status << 24) + (cam_status << 16) + scsi_status;
}


/*
 *  Called on successfull INQUIRY response.
 */
void sym_sniff_inquiry(struct sym_hcb *np, struct scsi_cmnd *cmd, int resid)
{
	int retv;

	if (!cmd || cmd->use_sg)
		return;

	sync_scsi_data_for_cpu(np, cmd);
	retv = __sym_sniff_inquiry(np, cmd->device->id, cmd->device->lun,
				   (u_char *) cmd->request_buffer,
				   cmd->request_bufflen - resid);
	sync_scsi_data_for_device(np, cmd);
	if (retv < 0)
		return;
	else if (retv)
		sym_update_trans_settings(np, &np->target[cmd->device->id]);
}

/*
 *  Build the scatter/gather array for an I/O.
 */

static int sym_scatter_no_sglist(struct sym_hcb *np, struct sym_ccb *cp, struct scsi_cmnd *cmd)
{
	struct sym_tblmove *data = &cp->phys.data[SYM_CONF_MAX_SG-1];
	int segment;

	cp->data_len = cmd->request_bufflen;

	if (cmd->request_bufflen) {
		dma_addr_t baddr = map_scsi_single_data(np, cmd);
		if (baddr) {
			sym_build_sge(np, data, baddr, cmd->request_bufflen);
			segment = 1;
		} else {
			segment = -2;
		}
	} else {
		segment = 0;
	}

	return segment;
}

static int sym_scatter(struct sym_hcb *np, struct sym_ccb *cp, struct scsi_cmnd *cmd)
{
	int segment;
	int use_sg = (int) cmd->use_sg;

	cp->data_len = 0;

	if (!use_sg)
		segment = sym_scatter_no_sglist(np, cp, cmd);
	else if ((use_sg = map_scsi_sg_data(np, cmd)) > 0) {
		struct scatterlist *scatter = (struct scatterlist *)cmd->buffer;
		struct sym_tblmove *data;

		if (use_sg > SYM_CONF_MAX_SG) {
			unmap_scsi_data(np, cmd);
			return -1;
		}

		data = &cp->phys.data[SYM_CONF_MAX_SG - use_sg];

		for (segment = 0; segment < use_sg; segment++) {
			dma_addr_t baddr = sg_dma_address(&scatter[segment]);
			unsigned int len = sg_dma_len(&scatter[segment]);

			sym_build_sge(np, &data[segment], baddr, len);
			cp->data_len += len;
		}
	} else {
		segment = -2;
	}

	return segment;
}

/*
 *  Queue a SCSI command.
 */
static int sym_queue_command(struct sym_hcb *np, struct scsi_cmnd *ccb)
{
/*	struct scsi_device        *device    = ccb->device; */
	struct sym_tcb *tp;
	struct sym_lcb *lp;
	struct sym_ccb *cp;
	int	order;

	/*
	 *  Minimal checkings, so that we will not 
	 *  go outside our tables.
	 */
	if (ccb->device->id == np->myaddr ||
	    ccb->device->id >= SYM_CONF_MAX_TARGET ||
	    ccb->device->lun >= SYM_CONF_MAX_LUN) {
		sym_xpt_done2(np, ccb, CAM_DEV_NOT_THERE);
		return 0;
	}

	/*
	 *  Retreive the target descriptor.
	 */
	tp = &np->target[ccb->device->id];

	/*
	 *  Complete the 1st INQUIRY command with error 
	 *  condition if the device is flagged NOSCAN 
	 *  at BOOT in the NVRAM. This may speed up 
	 *  the boot and maintain coherency with BIOS 
	 *  device numbering. Clearing the flag allows 
	 *  user to rescan skipped devices later.
	 *  We also return error for devices not flagged 
	 *  for SCAN LUNS in the NVRAM since some mono-lun 
	 *  devices behave badly when asked for some non 
	 *  zero LUN. Btw, this is an absolute hack.:-)
	 */
	if (ccb->cmnd[0] == 0x12 || ccb->cmnd[0] == 0x0) {
		if ((tp->usrflags & SYM_SCAN_BOOT_DISABLED) ||
		    ((tp->usrflags & SYM_SCAN_LUNS_DISABLED) && 
		     ccb->device->lun != 0)) {
			tp->usrflags &= ~SYM_SCAN_BOOT_DISABLED;
			sym_xpt_done2(np, ccb, CAM_DEV_NOT_THERE);
			return 0;
		}
	}

	/*
	 *  Select tagged/untagged.
	 */
	lp = sym_lp(np, tp, ccb->device->lun);
	order = (lp && lp->s.reqtags) ? M_SIMPLE_TAG : 0;

	/*
	 *  Queue the SCSI IO.
	 */
	cp = sym_get_ccb(np, ccb->device->id, ccb->device->lun, order);
	if (!cp)
		return 1;	/* Means resource shortage */
	sym_queue_scsiio(np, ccb, cp);
	return 0;
}

/*
 *  Setup buffers and pointers that address the CDB.
 */
static inline int sym_setup_cdb(struct sym_hcb *np, struct scsi_cmnd *ccb, struct sym_ccb *cp)
{
	u32	cmd_ba;
	int	cmd_len;

	/*
	 *  CDB is 16 bytes max.
	 */
	if (ccb->cmd_len > sizeof(cp->cdb_buf)) {
		sym_set_cam_status(cp->cam_ccb, CAM_REQ_INVALID);
		return -1;
	}

	memcpy(cp->cdb_buf, ccb->cmnd, ccb->cmd_len);
	cmd_ba  = CCB_BA (cp, cdb_buf[0]);
	cmd_len = ccb->cmd_len;

	cp->phys.cmd.addr	= cpu_to_scr(cmd_ba);
	cp->phys.cmd.size	= cpu_to_scr(cmd_len);

	return 0;
}

/*
 *  Setup pointers that address the data and start the I/O.
 */
int sym_setup_data_and_start(struct sym_hcb *np, struct scsi_cmnd *csio, struct sym_ccb *cp)
{
	int dir;
	struct sym_tcb *tp = &np->target[cp->target];
	struct sym_lcb *lp = sym_lp(np, tp, cp->lun);

	/*
	 *  Build the CDB.
	 */
	if (sym_setup_cdb(np, csio, cp))
		goto out_abort;

	/*
	 *  No direction means no data.
	 */
	dir = csio->sc_data_direction;
	if (dir != DMA_NONE) {
		cp->segments = sym_scatter(np, cp, csio);
		if (cp->segments < 0) {
			if (cp->segments == -2)
				sym_set_cam_status(csio, CAM_RESRC_UNAVAIL);
			else
				sym_set_cam_status(csio, CAM_REQ_TOO_BIG);
			goto out_abort;
		}
	} else {
		cp->data_len = 0;
		cp->segments = 0;
	}

	/*
	 *  Set data pointers.
	 */
	sym_setup_data_pointers(np, cp, dir);

	/*
	 *  When `#ifed 1', the code below makes the driver 
	 *  panic on the first attempt to write to a SCSI device.
	 *  It is the first test we want to do after a driver 
	 *  change that does not seem obviously safe. :)
	 */
#if 0
	switch (cp->cdb_buf[0]) {
	case 0x0A: case 0x2A: case 0xAA:
		panic("XXXXXXXXXXXXX WRITE NOT YET ALLOWED XXXXXXXXXXXXXX\n");
		MDELAY(10000);
		break;
	default:
		break;
	}
#endif

	/*
	 *	activate this job.
	 */
	if (lp)
		sym_start_next_ccbs(np, lp, 2);
	else
		sym_put_start_queue(np, cp);
	return 0;

out_abort:
	sym_free_ccb(np, cp);
	sym_xpt_done(np, csio);
	return 0;
}


/*
 *  timer daemon.
 *
 *  Misused to keep the driver running when
 *  interrupts are not configured correctly.
 */
static void sym_timer(struct sym_hcb *np)
{
	unsigned long thistime = jiffies;

	/*
	 *  Restart the timer.
	 */
	np->s.timer.expires = thistime + SYM_CONF_TIMER_INTERVAL;
	add_timer(&np->s.timer);

	/*
	 *  If we are resetting the ncr, wait for settle_time before 
	 *  clearing it. Then command processing will be resumed.
	 */
	if (np->s.settle_time_valid) {
		if (time_before_eq(np->s.settle_time, thistime)) {
			if (sym_verbose >= 2 )
				printk("%s: command processing resumed\n",
				       sym_name(np));
			np->s.settle_time_valid = 0;
		}
		return;
	}

	/*
	 *	Nothing to do for now, but that may come.
	 */
	if (np->s.lasttime + 4*HZ < thistime) {
		np->s.lasttime = thistime;
	}

#ifdef SYM_CONF_PCIQ_MAY_MISS_COMPLETIONS
	/*
	 *  Some way-broken PCI bridges may lead to 
	 *  completions being lost when the clearing 
	 *  of the INTFLY flag by the CPU occurs 
	 *  concurrently with the chip raising this flag.
	 *  If this ever happen, lost completions will 
	 * be reaped here.
	 */
	sym_wakeup_done(np);
#endif
}


/*
 *  PCI BUS error handler.
 */
void sym_log_bus_error(struct sym_hcb *np)
{
	u_short pci_sts;
	pci_read_config_word(np->s.device, PCI_STATUS, &pci_sts);
	if (pci_sts & 0xf900) {
		pci_write_config_word(np->s.device, PCI_STATUS, pci_sts);
		printf("%s: PCI STATUS = 0x%04x\n",
			sym_name(np), pci_sts & 0xf900);
	}
}


/*
 *  Requeue awaiting commands.
 */
static void sym_requeue_awaiting_cmds(struct sym_hcb *np)
{
	struct scsi_cmnd *cmd;
	struct sym_ucmd *ucp = SYM_UCMD_PTR(cmd);
	SYM_QUEHEAD tmp_cmdq;
	int sts;

	sym_que_move(&np->s.wait_cmdq, &tmp_cmdq);

	while ((ucp = (struct sym_ucmd *) sym_remque_head(&tmp_cmdq)) != 0) {
		sym_insque_tail(&ucp->link_cmdq, &np->s.busy_cmdq);
		cmd = SYM_SCMD_PTR(ucp);
		sts = sym_queue_command(np, cmd);
		if (sts) {
			sym_remque(&ucp->link_cmdq);
			sym_insque_head(&ucp->link_cmdq, &np->s.wait_cmdq);
		}
	}
}

/*
 * queuecommand method.  Entered with the host adapter lock held and
 * interrupts disabled.
 */
static int sym53c8xx_queue_command(struct scsi_cmnd *cmd,
					void (*done)(struct scsi_cmnd *))
{
	struct sym_hcb *np = SYM_SOFTC_PTR(cmd);
	struct sym_ucmd *ucp = SYM_UCMD_PTR(cmd);
	int sts = 0;

	cmd->scsi_done     = done;
	cmd->host_scribble = NULL;
	memset(ucp, 0, sizeof(*ucp));

	/*
	 *  Shorten our settle_time if needed for 
	 *  this command not to time out.
	 */
	if (np->s.settle_time_valid && cmd->timeout_per_command) {
		unsigned long tlimit = jiffies + cmd->timeout_per_command;
		tlimit -= SYM_CONF_TIMER_INTERVAL*2;
		if (time_after(np->s.settle_time, tlimit)) {
			np->s.settle_time = tlimit;
		}
	}

	if (np->s.settle_time_valid || !sym_que_empty(&np->s.wait_cmdq)) {
		sym_insque_tail(&ucp->link_cmdq, &np->s.wait_cmdq);
		goto out;
	}

	sym_insque_tail(&ucp->link_cmdq, &np->s.busy_cmdq);
	sts = sym_queue_command(np, cmd);
	if (sts) {
		sym_remque(&ucp->link_cmdq);
		sym_insque_tail(&ucp->link_cmdq, &np->s.wait_cmdq);
	}
out:
	return 0;
}

/*
 *  Linux entry point of the interrupt handler.
 */
static irqreturn_t sym53c8xx_intr(int irq, void *dev_id, struct pt_regs * regs)
{
	unsigned long flags;
	struct sym_hcb *np = (struct sym_hcb *)dev_id;

	if (DEBUG_FLAGS & DEBUG_TINY) printf_debug ("[");

	spin_lock_irqsave(np->s.host->host_lock, flags);

	sym_interrupt(np);

	/*
	 * push queue walk-through to tasklet
	 */
	if (!sym_que_empty(&np->s.wait_cmdq) && !np->s.settle_time_valid)
		sym_requeue_awaiting_cmds(np);

	spin_unlock_irqrestore(np->s.host->host_lock, flags);

	if (DEBUG_FLAGS & DEBUG_TINY) printf_debug ("]\n");

	return IRQ_HANDLED;
}

/*
 *  Linux entry point of the timer handler
 */
static void sym53c8xx_timer(unsigned long npref)
{
	struct sym_hcb *np = (struct sym_hcb *)npref;
	unsigned long flags;

	spin_lock_irqsave(np->s.host->host_lock, flags);

	sym_timer(np);

	if (!sym_que_empty(&np->s.wait_cmdq) && !np->s.settle_time_valid)
		sym_requeue_awaiting_cmds(np);

	spin_unlock_irqrestore(np->s.host->host_lock, flags);
}


/*
 *  What the eh thread wants us to perform.
 */
#define SYM_EH_ABORT		0
#define SYM_EH_DEVICE_RESET	1
#define SYM_EH_BUS_RESET	2
#define SYM_EH_HOST_RESET	3

/*
 *  What we will do regarding the involved SCSI command.
 */
#define SYM_EH_DO_IGNORE	0
#define SYM_EH_DO_COMPLETE	1
#define SYM_EH_DO_WAIT		2

/*
 *  Our general completion handler.
 */
static void __sym_eh_done(struct scsi_cmnd *cmd, int timed_out)
{
	struct sym_eh_wait *ep = SYM_UCMD_PTR(cmd)->eh_wait;
	if (!ep)
		return;

	/* Try to avoid a race here (not 100% safe) */
	if (!timed_out) {
		ep->timed_out = 0;
		if (ep->to_do == SYM_EH_DO_WAIT && !del_timer(&ep->timer))
			return;
	}

	/* Revert everything */
	SYM_UCMD_PTR(cmd)->eh_wait = NULL;
	cmd->scsi_done = ep->old_done;

	/* Wake up the eh thread if it wants to sleep */
	if (ep->to_do == SYM_EH_DO_WAIT)
		up(&ep->sem);
}

/*
 *  scsi_done() alias when error recovery is in progress. 
 */
static void sym_eh_done(struct scsi_cmnd *cmd) { __sym_eh_done(cmd, 0); }

/*
 *  Some timeout handler to avoid waiting too long.
 */
static void sym_eh_timeout(u_long p) { __sym_eh_done((struct scsi_cmnd *)p, 1); }

/*
 *  Generic method for our eh processing.
 *  The 'op' argument tells what we have to do.
 */
static int sym_eh_handler(int op, char *opname, struct scsi_cmnd *cmd)
{
	struct sym_hcb *np = SYM_SOFTC_PTR(cmd);
	SYM_QUEHEAD *qp;
	int to_do = SYM_EH_DO_IGNORE;
	int sts = -1;
	struct sym_eh_wait eh, *ep = &eh;
	char devname[20];

	sprintf(devname, "%s:%d:%d", sym_name(np), cmd->device->id, cmd->device->lun);

	printf_warning("%s: %s operation started.\n", devname, opname);

#if 0
	/* This one should be the result of some race, thus to ignore */
	if (cmd->serial_number != cmd->serial_number_at_timeout)
		goto prepare;
#endif

	/* This one is not queued to the core driver -> to complete here */ 
	FOR_EACH_QUEUED_ELEMENT(&np->s.wait_cmdq, qp) {
		if (SYM_SCMD_PTR(qp) == cmd) {
			to_do = SYM_EH_DO_COMPLETE;
			goto prepare;
		}
	}

	/* This one is queued in some place -> to wait for completion */
	FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
		struct sym_ccb *cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		if (cp->cam_ccb == cmd) {
			to_do = SYM_EH_DO_WAIT;
			goto prepare;
		}
	}

prepare:
	/* Prepare stuff to either ignore, complete or wait for completion */
	switch(to_do) {
	default:
	case SYM_EH_DO_IGNORE:
		break;
	case SYM_EH_DO_WAIT:
		init_MUTEX_LOCKED(&ep->sem);
		/* fall through */
	case SYM_EH_DO_COMPLETE:
		ep->old_done = cmd->scsi_done;
		cmd->scsi_done = sym_eh_done;
		SYM_UCMD_PTR(cmd)->eh_wait = ep;
	}

	/* Try to proceed the operation we have been asked for */
	sts = -1;
	switch(op) {
	case SYM_EH_ABORT:
		sts = sym_abort_scsiio(np, cmd, 1);
		break;
	case SYM_EH_DEVICE_RESET:
		sts = sym_reset_scsi_target(np, cmd->device->id);
		break;
	case SYM_EH_BUS_RESET:
		sym_reset_scsi_bus(np, 1);
		sts = 0;
		break;
	case SYM_EH_HOST_RESET:
		sym_reset_scsi_bus(np, 0);
		sym_start_up (np, 1);
		sts = 0;
		break;
	default:
		break;
	}

	/* On error, restore everything and cross fingers :) */
	if (sts) {
		SYM_UCMD_PTR(cmd)->eh_wait = NULL;
		cmd->scsi_done = ep->old_done;
		to_do = SYM_EH_DO_IGNORE;
	}

	ep->to_do = to_do;
	/* Complete the command with locks held as required by the driver */
	if (to_do == SYM_EH_DO_COMPLETE)
		sym_xpt_done2(np, cmd, CAM_REQ_ABORTED);

	/* Wait for completion with locks released, as required by kernel */
	if (to_do == SYM_EH_DO_WAIT) {
		init_timer(&ep->timer);
		ep->timer.expires = jiffies + (5*HZ);
		ep->timer.function = sym_eh_timeout;
		ep->timer.data = (u_long)cmd;
		ep->timed_out = 1;	/* Be pessimistic for once :) */
		add_timer(&ep->timer);
		spin_unlock_irq(np->s.host->host_lock);
		down(&ep->sem);
		spin_lock_irq(np->s.host->host_lock);
		if (ep->timed_out)
			sts = -2;
	}
	printf_warning("%s: %s operation %s.\n", devname, opname,
			sts==0?"complete":sts==-2?"timed-out":"failed");
	return sts? SCSI_FAILED : SCSI_SUCCESS;
}


/*
 * Error handlers called from the eh thread (one thread per HBA).
 */
static int sym53c8xx_eh_abort_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_ABORT, "ABORT", cmd);
}

static int sym53c8xx_eh_device_reset_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_DEVICE_RESET, "DEVICE RESET", cmd);
}

static int sym53c8xx_eh_bus_reset_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_BUS_RESET, "BUS RESET", cmd);
}

static int sym53c8xx_eh_host_reset_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_HOST_RESET, "HOST RESET", cmd);
}

/*
 *  Tune device queuing depth, according to various limits.
 */
static void sym_tune_dev_queuing(struct sym_hcb *np, int target, int lun, u_short reqtags)
{
	struct sym_tcb *tp = &np->target[target];
	struct sym_lcb *lp = sym_lp(np, tp, lun);
	u_short	oldtags;

	if (!lp)
		return;

	oldtags = lp->s.reqtags;

	if (reqtags > lp->s.scdev_depth)
		reqtags = lp->s.scdev_depth;

	lp->started_limit = reqtags ? reqtags : 2;
	lp->started_max   = 1;
	lp->s.reqtags     = reqtags;

	if (reqtags != oldtags) {
		printf_info("%s:%d:%d: "
		         "tagged command queuing %s, command queue depth %d.\n",
		          sym_name(np), target, lun,
		          lp->s.reqtags ? "enabled" : "disabled",
 		          lp->started_limit);
	}
}

#ifdef	SYM_LINUX_BOOT_COMMAND_LINE_SUPPORT
/*
 *  Linux select queue depths function
 */
#define DEF_DEPTH	(sym_driver_setup.max_tag)
#define ALL_TARGETS	-2
#define NO_TARGET	-1
#define ALL_LUNS	-2
#define NO_LUN		-1

static int device_queue_depth(struct sym_hcb *np, int target, int lun)
{
	int c, h, t, u, v;
	char *p = sym_driver_setup.tag_ctrl;
	char *ep;

	h = -1;
	t = NO_TARGET;
	u = NO_LUN;
	while ((c = *p++) != 0) {
		v = simple_strtoul(p, &ep, 0);
		switch(c) {
		case '/':
			++h;
			t = ALL_TARGETS;
			u = ALL_LUNS;
			break;
		case 't':
			if (t != target)
				t = (target == v) ? v : NO_TARGET;
			u = ALL_LUNS;
			break;
		case 'u':
			if (u != lun)
				u = (lun == v) ? v : NO_LUN;
			break;
		case 'q':
			if (h == np->s.unit &&
				(t == ALL_TARGETS || t == target) &&
				(u == ALL_LUNS    || u == lun))
				return v;
			break;
		case '-':
			t = ALL_TARGETS;
			u = ALL_LUNS;
			break;
		default:
			break;
		}
		p = ep;
	}
	return DEF_DEPTH;
}
#else
#define device_queue_depth(np, t, l)	(sym_driver_setup.max_tag)
#endif	/* SYM_LINUX_BOOT_COMMAND_LINE_SUPPORT */

/*
 * Linux entry point for device queue sizing.
 */
static int sym53c8xx_slave_configure(struct scsi_device *device)
{
	struct Scsi_Host *host = device->host;
	struct sym_hcb *np;
	struct sym_tcb *tp;
	struct sym_lcb *lp;
	int reqtags, depth_to_use;

	np = ((struct host_data *) host->hostdata)->ncb;
	tp = &np->target[device->id];

	/*
	 *  Get user settings for transfer parameters.
	 */
	tp->inq_byte7_valid = (INQ7_SYNC|INQ7_WIDE16);
	sym_update_trans_settings(np, tp);

	/*
	 *  Allocate the LCB if not yet.
	 *  If it fail, we may well be in the sh*t. :)
	 */
	lp = sym_alloc_lcb(np, device->id, device->lun);
	if (!lp)
		return -ENOMEM;

	/*
	 *  Get user flags.
	 */
	lp->curr_flags = lp->user_flags;

	/*
	 *  Select queue depth from driver setup.
	 *  Donnot use more than configured by user.
	 *  Use at least 2.
	 *  Donnot use more than our maximum.
	 */
	reqtags = device_queue_depth(np, device->id, device->lun);
	if (reqtags > tp->usrtags)
		reqtags = tp->usrtags;
	if (!device->tagged_supported)
		reqtags = 0;
#if 1 /* Avoid to locally queue commands for no good reasons */
	if (reqtags > SYM_CONF_MAX_TAG)
		reqtags = SYM_CONF_MAX_TAG;
	depth_to_use = (reqtags ? reqtags : 2);
#else
	depth_to_use = (reqtags ? SYM_CONF_MAX_TAG : 2);
#endif
	scsi_adjust_queue_depth(device,
				(device->tagged_supported ?
				 MSG_SIMPLE_TAG : 0),
				depth_to_use);
	lp->s.scdev_depth = depth_to_use;
	sym_tune_dev_queuing(np, device->id, device->lun, reqtags);

	spi_dv_device(device);

	return 0;
}

/*
 *  Linux entry point for info() function
 */
static const char *sym53c8xx_info (struct Scsi_Host *host)
{
	return sym_driver_name();
}


#ifdef SYM_LINUX_PROC_INFO_SUPPORT
/*
 *  Proc file system stuff
 *
 *  A read operation returns adapter information.
 *  A write operation is a control command.
 *  The string is parsed in the driver code and the command is passed 
 *  to the sym_usercmd() function.
 */

#ifdef SYM_LINUX_USER_COMMAND_SUPPORT

struct	sym_usrcmd {
	u_long	target;
	u_long	lun;
	u_long	data;
	u_long	cmd;
};

#define UC_SETSYNC      10
#define UC_SETTAGS	11
#define UC_SETDEBUG	12
#define UC_SETWIDE	14
#define UC_SETFLAG	15
#define UC_SETVERBOSE	17
#define UC_RESETDEV	18
#define UC_CLEARDEV	19

static void sym_exec_user_command (struct sym_hcb *np, struct sym_usrcmd *uc)
{
	struct sym_tcb *tp;
	int t, l;

	switch (uc->cmd) {
	case 0: return;

#ifdef SYM_LINUX_DEBUG_CONTROL_SUPPORT
	case UC_SETDEBUG:
		sym_debug_flags = uc->data;
		break;
#endif
	case UC_SETVERBOSE:
		np->verbose = uc->data;
		break;
	default:
		/*
		 * We assume that other commands apply to targets.
		 * This should always be the case and avoid the below 
		 * 4 lines to be repeated 6 times.
		 */
		for (t = 0; t < SYM_CONF_MAX_TARGET; t++) {
			if (!((uc->target >> t) & 1))
				continue;
			tp = &np->target[t];

			switch (uc->cmd) {

			case UC_SETSYNC:
				if (!uc->data || uc->data >= 255) {
					tp->tinfo.goal.options = 0;
					tp->tinfo.goal.offset  = 0;
					break;
				}
				if (uc->data <= 9 && np->minsync_dt) {
					if (uc->data < np->minsync_dt)
						uc->data = np->minsync_dt;
					tp->tinfo.goal.options = PPR_OPT_DT;
					tp->tinfo.goal.width   = 1;
					tp->tinfo.goal.period = uc->data;
					tp->tinfo.goal.offset = np->maxoffs_dt;
				} else {
					if (uc->data < np->minsync)
						uc->data = np->minsync;
					tp->tinfo.goal.options = 0;
					tp->tinfo.goal.period = uc->data;
					tp->tinfo.goal.offset = np->maxoffs;
				}
				break;
			case UC_SETWIDE:
				tp->tinfo.goal.width = uc->data ? 1 : 0;
				break;
			case UC_SETTAGS:
				for (l = 0; l < SYM_CONF_MAX_LUN; l++)
					sym_tune_dev_queuing(np, t,l, uc->data);
				break;
			case UC_RESETDEV:
				tp->to_reset = 1;
				np->istat_sem = SEM;
				OUTB (nc_istat, SIGP|SEM);
				break;
			case UC_CLEARDEV:
				for (l = 0; l < SYM_CONF_MAX_LUN; l++) {
					struct sym_lcb *lp = sym_lp(np, tp, l);
					if (lp) lp->to_clear = 1;
				}
				np->istat_sem = SEM;
				OUTB (nc_istat, SIGP|SEM);
				break;
			case UC_SETFLAG:
				tp->usrflags = uc->data;
				break;
			}
		}
		break;
	}
}

#define digit_to_bin(c)	((c) - '0')

static int skip_spaces(char *ptr, int len)
{
	int cnt, c;

	for (cnt = len; cnt > 0 && (c = *ptr++) && isspace(c); cnt--);

	return (len - cnt);
}

static int get_int_arg(char *ptr, int len, u_long *pv)
{
	int	cnt, c;
	u_long	v;

	for (v = 0, cnt = len; cnt > 0 && (c = *ptr++) && isdigit(c); cnt--) {
		v = (v * 10) + digit_to_bin(c);
	}

	if (pv)
		*pv = v;

	return (len - cnt);
}

static int is_keyword(char *ptr, int len, char *verb)
{
	int verb_len = strlen(verb);

	if (len >= verb_len && !memcmp(verb, ptr, verb_len))
		return verb_len;
	else
		return 0;

}

#define SKIP_SPACES(min_spaces)						\
	if ((arg_len = skip_spaces(ptr, len)) < (min_spaces))		\
		return -EINVAL;						\
	ptr += arg_len; len -= arg_len;

#define GET_INT_ARG(v)							\
	if (!(arg_len = get_int_arg(ptr, len, &(v))))			\
		return -EINVAL;						\
	ptr += arg_len; len -= arg_len;


/*
 * Parse a control command
 */

static int sym_user_command(struct sym_hcb *np, char *buffer, int length)
{
	char *ptr	= buffer;
	int len		= length;
	struct sym_usrcmd cmd, *uc = &cmd;
	int		arg_len;
	u_long 		target;

	bzero(uc, sizeof(*uc));

	if (len > 0 && ptr[len-1] == '\n')
		--len;

	if	((arg_len = is_keyword(ptr, len, "setsync")) != 0)
		uc->cmd = UC_SETSYNC;
	else if	((arg_len = is_keyword(ptr, len, "settags")) != 0)
		uc->cmd = UC_SETTAGS;
	else if	((arg_len = is_keyword(ptr, len, "setverbose")) != 0)
		uc->cmd = UC_SETVERBOSE;
	else if	((arg_len = is_keyword(ptr, len, "setwide")) != 0)
		uc->cmd = UC_SETWIDE;
#ifdef SYM_LINUX_DEBUG_CONTROL_SUPPORT
	else if	((arg_len = is_keyword(ptr, len, "setdebug")) != 0)
		uc->cmd = UC_SETDEBUG;
#endif
	else if	((arg_len = is_keyword(ptr, len, "setflag")) != 0)
		uc->cmd = UC_SETFLAG;
	else if	((arg_len = is_keyword(ptr, len, "resetdev")) != 0)
		uc->cmd = UC_RESETDEV;
	else if	((arg_len = is_keyword(ptr, len, "cleardev")) != 0)
		uc->cmd = UC_CLEARDEV;
	else
		arg_len = 0;

#ifdef DEBUG_PROC_INFO
printk("sym_user_command: arg_len=%d, cmd=%ld\n", arg_len, uc->cmd);
#endif

	if (!arg_len)
		return -EINVAL;
	ptr += arg_len; len -= arg_len;

	switch(uc->cmd) {
	case UC_SETSYNC:
	case UC_SETTAGS:
	case UC_SETWIDE:
	case UC_SETFLAG:
	case UC_RESETDEV:
	case UC_CLEARDEV:
		SKIP_SPACES(1);
		if ((arg_len = is_keyword(ptr, len, "all")) != 0) {
			ptr += arg_len; len -= arg_len;
			uc->target = ~0;
		} else {
			GET_INT_ARG(target);
			uc->target = (1<<target);
#ifdef DEBUG_PROC_INFO
printk("sym_user_command: target=%ld\n", target);
#endif
		}
		break;
	}

	switch(uc->cmd) {
	case UC_SETVERBOSE:
	case UC_SETSYNC:
	case UC_SETTAGS:
	case UC_SETWIDE:
		SKIP_SPACES(1);
		GET_INT_ARG(uc->data);
#ifdef DEBUG_PROC_INFO
printk("sym_user_command: data=%ld\n", uc->data);
#endif
		break;
#ifdef SYM_LINUX_DEBUG_CONTROL_SUPPORT
	case UC_SETDEBUG:
		while (len > 0) {
			SKIP_SPACES(1);
			if	((arg_len = is_keyword(ptr, len, "alloc")))
				uc->data |= DEBUG_ALLOC;
			else if	((arg_len = is_keyword(ptr, len, "phase")))
				uc->data |= DEBUG_PHASE;
			else if	((arg_len = is_keyword(ptr, len, "queue")))
				uc->data |= DEBUG_QUEUE;
			else if	((arg_len = is_keyword(ptr, len, "result")))
				uc->data |= DEBUG_RESULT;
			else if	((arg_len = is_keyword(ptr, len, "scatter")))
				uc->data |= DEBUG_SCATTER;
			else if	((arg_len = is_keyword(ptr, len, "script")))
				uc->data |= DEBUG_SCRIPT;
			else if	((arg_len = is_keyword(ptr, len, "tiny")))
				uc->data |= DEBUG_TINY;
			else if	((arg_len = is_keyword(ptr, len, "timing")))
				uc->data |= DEBUG_TIMING;
			else if	((arg_len = is_keyword(ptr, len, "nego")))
				uc->data |= DEBUG_NEGO;
			else if	((arg_len = is_keyword(ptr, len, "tags")))
				uc->data |= DEBUG_TAGS;
			else if	((arg_len = is_keyword(ptr, len, "pointer")))
				uc->data |= DEBUG_POINTER;
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
#ifdef DEBUG_PROC_INFO
printk("sym_user_command: data=%ld\n", uc->data);
#endif
		break;
#endif /* SYM_LINUX_DEBUG_CONTROL_SUPPORT */
	case UC_SETFLAG:
		while (len > 0) {
			SKIP_SPACES(1);
			if	((arg_len = is_keyword(ptr, len, "no_disc")))
				uc->data &= ~SYM_DISC_ENABLED;
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
		break;
	default:
		break;
	}

	if (len)
		return -EINVAL;
	else {
		unsigned long flags;

		spin_lock_irqsave(np->s.host->host_lock, flags);
		sym_exec_user_command (np, uc);
		spin_unlock_irqrestore(np->s.host->host_lock, flags);
	}
	return length;
}

#endif	/* SYM_LINUX_USER_COMMAND_SUPPORT */


#ifdef SYM_LINUX_USER_INFO_SUPPORT
/*
 *  Informations through the proc file system.
 */
struct info_str {
	char *buffer;
	int length;
	int offset;
	int pos;
};

static void copy_mem_info(struct info_str *info, char *data, int len)
{
	if (info->pos + len > info->length)
		len = info->length - info->pos;

	if (info->pos + len < info->offset) {
		info->pos += len;
		return;
	}
	if (info->pos < info->offset) {
		data += (info->offset - info->pos);
		len  -= (info->offset - info->pos);
	}

	if (len > 0) {
		memcpy(info->buffer + info->pos, data, len);
		info->pos += len;
	}
}

static int copy_info(struct info_str *info, char *fmt, ...)
{
	va_list args;
	char buf[81];
	int len;

	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	copy_mem_info(info, buf, len);
	return len;
}

/*
 *  Copy formatted information into the input buffer.
 */
static int sym_host_info(struct sym_hcb *np, char *ptr, off_t offset, int len)
{
	struct info_str info;

	info.buffer	= ptr;
	info.length	= len;
	info.offset	= offset;
	info.pos	= 0;

	copy_info(&info, "Chip " NAME53C "%s, device id 0x%x, "
			 "revision id 0x%x\n",
			 np->s.chip_name, np->device_id, np->revision_id);
	copy_info(&info, "At PCI address %s, "
#ifdef __sparc__
		"IRQ %s\n",
#else
		"IRQ %d\n",
#endif
		pci_name(np->s.device),
#ifdef __sparc__
		__irq_itoa(np->s.irq));
#else
		(int) np->s.irq);
#endif
	copy_info(&info, "Min. period factor %d, %s SCSI BUS%s\n",
			 (int) (np->minsync_dt ? np->minsync_dt : np->minsync),
			 np->maxwide ? "Wide" : "Narrow",
			 np->minsync_dt ? ", DT capable" : "");

	copy_info(&info, "Max. started commands %d, "
			 "max. commands per LUN %d\n",
			 SYM_CONF_MAX_START, SYM_CONF_MAX_TAG);

	return info.pos > info.offset? info.pos - info.offset : 0;
}
#endif /* SYM_LINUX_USER_INFO_SUPPORT */

/*
 *  Entry point of the scsi proc fs of the driver.
 *  - func = 0 means read  (returns adapter infos)
 *  - func = 1 means write (not yet merget from sym53c8xx)
 */
static int sym53c8xx_proc_info(struct Scsi_Host *host, char *buffer,
			char **start, off_t offset, int length, int func)
{
	struct host_data *host_data;
	struct sym_hcb *np = NULL;
	int retv;

	host_data = (struct host_data *) host->hostdata;
	np = host_data->ncb;
	if (!np)
		return -EINVAL;

	if (func) {
#ifdef	SYM_LINUX_USER_COMMAND_SUPPORT
		retv = sym_user_command(np, buffer, length);
#else
		retv = -EINVAL;
#endif
	} else {
		if (start)
			*start = buffer;
#ifdef SYM_LINUX_USER_INFO_SUPPORT
		retv = sym_host_info(np, buffer, offset, length);
#else
		retv = -EINVAL;
#endif
	}

	return retv;
}
#endif /* SYM_LINUX_PROC_INFO_SUPPORT */

/*
 *	Free controller resources.
 */
static void sym_free_resources(struct sym_hcb *np)
{
	/*
	 *  Free O/S specific resources.
	 */
	if (np->s.irq)
		free_irq(np->s.irq, np);
#ifndef SYM_CONF_IOMAPPED
	if (np->s.mmio_va)
		iounmap(np->s.mmio_va);
#endif
	if (np->s.ram_va)
		iounmap(np->s.ram_va);
	/*
	 *  Free O/S independent resources.
	 */
	sym_hcb_free(np);

	sym_mfree_dma(np, sizeof(*np), "HCB");
}

/*
 *  Ask/tell the system about DMA addressing.
 */
static int sym_setup_bus_dma_mask(struct sym_hcb *np)
{
#if   SYM_CONF_DMA_ADDRESSING_MODE == 0
	if (pci_set_dma_mask(np->s.device, 0xffffffffUL))
		goto out_err32;
#else
#if   SYM_CONF_DMA_ADDRESSING_MODE == 1
#define	PciDmaMask	0xffffffffffULL
#elif SYM_CONF_DMA_ADDRESSING_MODE == 2
#define	PciDmaMask	0xffffffffffffffffULL
#endif
	if (np->features & FE_DAC) {
		if (!pci_set_dma_mask(np->s.device, PciDmaMask)) {
			np->use_dac = 1;
			printf_info("%s: using 64 bit DMA addressing\n",
					sym_name(np));
		} else {
			if (pci_set_dma_mask(np->s.device, 0xffffffffUL))
				goto out_err32;
		}
	}
#undef	PciDmaMask
#endif
	return 0;

out_err32:
	printf_warning("%s: 32 BIT DMA ADDRESSING NOT SUPPORTED\n",
			sym_name(np));
	return -1;
}

/*
 *  Host attach and initialisations.
 *
 *  Allocate host data and ncb structure.
 *  Remap MMIO region.
 *  Do chip initialization.
 *  If all is OK, install interrupt handling and
 *  start the timer daemon.
 */
static struct Scsi_Host * __devinit sym_attach(struct scsi_host_template *tpnt,
		int unit, struct sym_device *dev)
{
	struct host_data *host_data;
	struct sym_hcb *np = NULL;
	struct Scsi_Host *instance = NULL;
	unsigned long flags;
	struct sym_fw *fw;

	printk(KERN_INFO
		"sym%d: <%s> rev 0x%x at pci %s "
#ifdef __sparc__
		"irq %s\n",
#else
		"irq %d\n",
#endif
		unit, dev->chip.name, dev->chip.revision_id,
		pci_name(dev->pdev),
#ifdef __sparc__
		__irq_itoa(dev->s.irq));
#else
		dev->s.irq);
#endif

	/*
	 *  Get the firmware for this chip.
	 */
	fw = sym_find_firmware(&dev->chip);
	if (!fw)
		goto attach_failed;

	/*
	 *	Allocate host_data structure
	 */
	instance = scsi_host_alloc(tpnt, sizeof(*host_data));
	if (!instance)
		goto attach_failed;
	host_data = (struct host_data *) instance->hostdata;

	/*
	 *  Allocate immediately the host control block, 
	 *  since we are only expecting to succeed. :)
	 *  We keep track in the HCB of all the resources that 
	 *  are to be released on error.
	 */
	np = __sym_calloc_dma(dev->pdev, sizeof(*np), "HCB");
	if (!np)
		goto attach_failed;
	np->s.device = dev->pdev;
	np->bus_dmat = dev->pdev; /* Result in 1 DMA pool per HBA */
	host_data->ncb = np;
	np->s.host = instance;

	pci_set_drvdata(dev->pdev, np);

	/*
	 *  Copy some useful infos to the HCB.
	 */
	np->hcb_ba	= vtobus(np);
	np->verbose	= sym_driver_setup.verbose;
	np->s.device	= dev->pdev;
	np->s.unit	= unit;
	np->device_id	= dev->chip.device_id;
	np->revision_id	= dev->chip.revision_id;
	np->features	= dev->chip.features;
	np->clock_divn	= dev->chip.nr_divisor;
	np->maxoffs	= dev->chip.offset_max;
	np->maxburst	= dev->chip.burst_max;
	np->myaddr	= dev->host_id;

	/*
	 *  Edit its name.
	 */
	strlcpy(np->s.chip_name, dev->chip.name, sizeof(np->s.chip_name));
	sprintf(np->s.inst_name, "sym%d", np->s.unit);

	/*
	 *  Ask/tell the system about DMA addressing.
	 */
	if (sym_setup_bus_dma_mask(np))
		goto attach_failed;

	/*
	 *  Try to map the controller chip to
	 *  virtual and physical memory.
	 */
	np->mmio_ba	= (u32)dev->s.base;
	np->s.io_ws	= (np->features & FE_IO256)? 256 : 128;

#ifndef SYM_CONF_IOMAPPED
	np->s.mmio_va = ioremap(dev->s.base_c, np->s.io_ws);
	if (!np->s.mmio_va) {
		printf_err("%s: can't map PCI MMIO region\n", sym_name(np));
		goto attach_failed;
	} else if (sym_verbose > 1)
		printf_info("%s: using memory mapped IO\n", sym_name(np));
#endif /* !defined SYM_CONF_IOMAPPED */

	np->s.io_port = dev->s.io_port;

	/*
	 *  Map on-chip RAM if present and supported.
	 */
	if (!(np->features & FE_RAM))
		dev->s.base_2 = 0;
	if (dev->s.base_2) {
		np->ram_ba = (u32)dev->s.base_2;
		if (np->features & FE_RAM8K)
			np->ram_ws = 8192;
		else
			np->ram_ws = 4096;
		np->s.ram_va = ioremap(dev->s.base_2_c, np->ram_ws);
		if (!np->s.ram_va) {
			printf_err("%s: can't map PCI MEMORY region\n",
				sym_name(np));
			goto attach_failed;
		}
	}

	/*
	 *  Perform O/S independent stuff.
	 */
	if (sym_hcb_attach(np, fw, dev->nvram))
		goto attach_failed;


	/*
	 *  Install the interrupt handler.
	 *  If we synchonize the C code with SCRIPTS on interrupt, 
	 *  we donnot want to share the INTR line at all.
	 */
	if (request_irq(dev->s.irq, sym53c8xx_intr, SA_SHIRQ,
			NAME53C8XX, np)) {
		printf_err("%s: request irq %d failure\n",
			sym_name(np), dev->s.irq);
		goto attach_failed;
	}
	np->s.irq = dev->s.irq;

	/*
	 *  After SCSI devices have been opened, we cannot
	 *  reset the bus safely, so we do it here.
	 */
	spin_lock_irqsave(instance->host_lock, flags);
	if (sym_reset_scsi_bus(np, 0))
		goto reset_failed;

	/*
	 *  Initialize some queue headers.
	 */
	sym_que_init(&np->s.wait_cmdq);
	sym_que_init(&np->s.busy_cmdq);

	/*
	 *  Start the SCRIPTS.
	 */
	sym_start_up (np, 1);

	/*
	 *  Start the timer daemon
	 */
	init_timer(&np->s.timer);
	np->s.timer.data     = (unsigned long) np;
	np->s.timer.function = sym53c8xx_timer;
	np->s.lasttime=0;
	sym_timer (np);

	/*
	 *  Fill Linux host instance structure
	 *  and return success.
	 */
	instance->max_channel	= 0;
	instance->this_id	= np->myaddr;
	instance->max_id	= np->maxwide ? 16 : 8;
	instance->max_lun	= SYM_CONF_MAX_LUN;
#ifndef SYM_CONF_IOMAPPED
	instance->base		= (unsigned long) np->s.mmio_va;
#endif
	instance->irq		= np->s.irq;
	instance->unique_id	= np->s.io_port;
	instance->io_port	= np->s.io_port;
	instance->n_io_port	= np->s.io_ws;
	instance->dma_channel	= 0;
	instance->cmd_per_lun	= SYM_CONF_MAX_TAG;
	instance->can_queue	= (SYM_CONF_MAX_START-2);
	instance->sg_tablesize	= SYM_CONF_MAX_SG;
	instance->max_cmd_len	= 16;
	BUG_ON(sym2_transport_template == NULL);
	instance->transportt	= sym2_transport_template;

	spin_unlock_irqrestore(instance->host_lock, flags);

	return instance;

 reset_failed:
	printf_err("%s: FATAL ERROR: CHECK SCSI BUS - CABLES, "
		   "TERMINATION, DEVICE POWER etc.!\n", sym_name(np));
	spin_unlock_irqrestore(instance->host_lock, flags);
 attach_failed:
	if (!instance)
		return NULL;
	printf_info("%s: giving up ...\n", sym_name(np));
	if (np)
		sym_free_resources(np);
	scsi_host_put(instance);

	return NULL;
 }


/*
 *    Detect and try to read SYMBIOS and TEKRAM NVRAM.
 */
#if SYM_CONF_NVRAM_SUPPORT
static void __devinit sym_get_nvram(struct sym_device *devp, struct sym_nvram *nvp)
{
	devp->nvram = nvp;
	devp->device_id = devp->chip.device_id;
	nvp->type = 0;

	/*
	 *  Get access to chip IO registers
	 */
#ifndef SYM_CONF_IOMAPPED
	devp->s.mmio_va = ioremap(devp->s.base_c, 128);
	if (!devp->s.mmio_va)
		return;
#endif

	sym_read_nvram(devp, nvp);

	/*
	 *  Release access to chip IO registers
	 */
#ifndef SYM_CONF_IOMAPPED
	iounmap(devp->s.mmio_va);
#endif
}
#else
static inline void sym_get_nvram(struct sym_device *devp, struct sym_nvram *nvp)
{
}
#endif	/* SYM_CONF_NVRAM_SUPPORT */

/*
 *  Driver setup from the boot command line
 */
#ifdef	SYM_LINUX_BOOT_COMMAND_LINE_SUPPORT

static struct sym_driver_setup
	sym_driver_safe_setup __initdata = SYM_LINUX_DRIVER_SAFE_SETUP;
#ifdef	MODULE
char *sym53c8xx;	/* command line passed by insmod */
MODULE_PARM(sym53c8xx, "s");
#endif

#define OPT_MAX_TAG		1
#define OPT_BURST_ORDER		2
#define OPT_SCSI_LED		3
#define OPT_SCSI_DIFF		4
#define OPT_IRQ_MODE		5
#define OPT_SCSI_BUS_CHECK	6
#define	OPT_HOST_ID		7
#define OPT_REVERSE_PROBE	8
#define OPT_VERBOSE		9
#define OPT_DEBUG		10
#define OPT_SETTLE_DELAY	11
#define OPT_USE_NVRAM		12
#define OPT_EXCLUDE		13
#define OPT_SAFE_SETUP		14

static char setup_token[] __initdata =
	"tags:"		"burst:"
	"led:"		"diff:"
	"irqm:"		"buschk:"
	"hostid:"	"revprob:"
	"verb:"		"debug:"
	"settle:"	"nvram:"
	"excl:"		"safe:"
	;

#ifdef MODULE
#define	ARG_SEP	' '
#else
#define	ARG_SEP	','
#endif

static int __init get_setup_token(char *p)
{
	char *cur = setup_token;
	char *pc;
	int i = 0;

	while (cur != NULL && (pc = strchr(cur, ':')) != NULL) {
		++pc;
		++i;
		if (!strncmp(p, cur, pc - cur))
			return i;
		cur = pc;
	}
	return 0;
}
#endif	/* SYM_LINUX_BOOT_COMMAND_LINE_SUPPORT */

int __init sym53c8xx_setup(char *str)
{
#ifdef	SYM_LINUX_BOOT_COMMAND_LINE_SUPPORT
	char *cur = str;
	char *pc, *pv;
	unsigned long val;
	unsigned int i,  c;
	int xi = 0;

	while (cur != NULL && (pc = strchr(cur, ':')) != NULL) {
		char *pe;

		val = 0;
		pv = pc;
		c = *++pv;

		if	(c == 'n')
			val = 0;
		else if	(c == 'y')
			val = 1;
		else
			val = (int) simple_strtoul(pv, &pe, 0);

		switch (get_setup_token(cur)) {
		case OPT_MAX_TAG:
			sym_driver_setup.max_tag = val;
			if (!(pe && *pe == '/'))
				break;
			i = 0;
			while (*pe && *pe != ARG_SEP && 
				i < sizeof(sym_driver_setup.tag_ctrl)-1) {
				sym_driver_setup.tag_ctrl[i++] = *pe++;
			}
			sym_driver_setup.tag_ctrl[i] = '\0';
			break;
		case OPT_SAFE_SETUP:
			memcpy(&sym_driver_setup, &sym_driver_safe_setup,
				sizeof(sym_driver_setup));
			break;
		case OPT_EXCLUDE:
			if (xi < 8)
				sym_driver_setup.excludes[xi++] = val;
			break;

#define __SIMPLE_OPTION(NAME, name) \
		case OPT_ ## NAME :		\
			sym_driver_setup.name = val;\
			break;

		__SIMPLE_OPTION(BURST_ORDER, burst_order)
		__SIMPLE_OPTION(SCSI_LED, scsi_led)
		__SIMPLE_OPTION(SCSI_DIFF, scsi_diff)
		__SIMPLE_OPTION(IRQ_MODE, irq_mode)
		__SIMPLE_OPTION(SCSI_BUS_CHECK, scsi_bus_check)
		__SIMPLE_OPTION(HOST_ID, host_id)
		__SIMPLE_OPTION(REVERSE_PROBE, reverse_probe)
		__SIMPLE_OPTION(VERBOSE, verbose)
		__SIMPLE_OPTION(DEBUG, debug)
		__SIMPLE_OPTION(SETTLE_DELAY, settle_delay)
		__SIMPLE_OPTION(USE_NVRAM, use_nvram)

#undef __SIMPLE_OPTION

		default:
			printk("sym53c8xx_setup: unexpected boot option '%.*s' ignored\n", (int)(pc-cur+1), cur);
			break;
		}

		if ((cur = strchr(cur, ARG_SEP)) != NULL)
			++cur;
	}
#endif	/* SYM_LINUX_BOOT_COMMAND_LINE_SUPPORT */
	return 1;
}

#ifndef MODULE
__setup("sym53c8xx=", sym53c8xx_setup);
#endif

/*
 *  Read and check the PCI configuration for any detected NCR 
 *  boards and save data for attaching after all boards have 
 *  been detected.
 */
static int __devinit
sym53c8xx_pci_init(struct pci_dev *pdev, struct sym_device *device)
{
	struct sym_pci_chip *chip;
	u_long base, base_2; 
	u_long base_c, base_2_c, io_port; 
	int i;
	u_short device_id, status_reg;
	u_char revision;

	/* Choose some short name for this device */
	sprintf(device->s.inst_name, "sym.%d.%d.%d", pdev->bus->number,
			PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn));

	device_id = pdev->device;

	io_port = pdev->resource[0].start;

	base_c = pdev->resource[1].start;
	i = pci_get_base_address(pdev, 1, &base);

	base_2_c = pdev->resource[i].start;
	pci_get_base_address(pdev, i, &base_2);

	base	&= PCI_BASE_ADDRESS_MEM_MASK;
	base_2	&= PCI_BASE_ADDRESS_MEM_MASK;

	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision);

	/*
	 *  If user excluded this chip, do not initialize it.
	 */
	if (io_port) {
		for (i = 0 ; i < 8 ; i++) {
			if (sym_driver_setup.excludes[i] == io_port)
				return -1;
		}
	}

	/*
	 *  Check if the chip is supported.
	 */
	chip = sym_lookup_pci_chip_table(device_id, revision);
	if (!chip) {
		printf_info("%s: device not supported\n", sym_name(device));
		return -1;
	}

	/*
	 *  Check if the chip has been assigned resources we need.
	 *  XXX: can this still happen with Linux 2.6's PCI layer?
	 */
#ifdef SYM_CONF_IOMAPPED
	if (!io_port) {
		printf_info("%s: IO base address disabled.\n",
			    sym_name(device));
		return -1;
	}
#else
	if (!base) {
		printf_info("%s: MMIO base address disabled.\n",
			    sym_name(device));
		return -1;
	}
#endif

	/*
	 *  Ignore Symbios chips controlled by various RAID controllers.
	 *  These controllers set value 0x52414944 at RAM end - 16.
	 */
#if defined(__i386__)
	if (base_2_c) {
		unsigned int ram_size, ram_val;
		void *ram_ptr;

		if (chip->features & FE_RAM8K)
			ram_size = 8192;
		else
			ram_size = 4096;

		ram_ptr = ioremap(base_2_c, ram_size);
		if (ram_ptr) {
			ram_val = readl_raw(ram_ptr + ram_size - 16);
			iounmap(ram_ptr);
			if (ram_val == 0x52414944) {
				printf_info("%s: not initializing, "
					    "driven by RAID controller.\n",
					    sym_name(device));
				return -1;
			}
		}
	}
#endif /* i386 and PCI MEMORY accessible */

	/*
	 *  Copy the chip description to our device structure, 
	 *  so we can make it match the actual device and options.
	 */
	memcpy(&device->chip, chip, sizeof(device->chip));
	device->chip.revision_id = revision;

	/*
	 *  Some features are required to be enabled in order to 
	 *  work around some chip problems. :) ;)
	 *  (ITEM 12 of a DEL about the 896 I haven't yet).
	 *  We must ensure the chip will use WRITE AND INVALIDATE.
	 *  The revision number limit is for now arbitrary.
	 */
	if (device_id == PCI_DEVICE_ID_NCR_53C896 && revision < 0x4) {
		chip->features	|= (FE_WRIE | FE_CLSE);
	}

	/* If the chip can do Memory Write Invalidate, enable it */
	if (chip->features & FE_WRIE) {
		if (pci_set_mwi(pdev))
			return -1;
	}

	/*
	 *  Work around for errant bit in 895A. The 66Mhz
	 *  capable bit is set erroneously. Clear this bit.
	 *  (Item 1 DEL 533)
	 *
	 *  Make sure Config space and Features agree.
	 *
	 *  Recall: writes are not normal to status register -
	 *  write a 1 to clear and a 0 to leave unchanged.
	 *  Can only reset bits.
	 */
	pci_read_config_word(pdev, PCI_STATUS, &status_reg);
	if (chip->features & FE_66MHZ) {
		if (!(status_reg & PCI_STATUS_66MHZ))
			chip->features &= ~FE_66MHZ;
	} else {
		if (status_reg & PCI_STATUS_66MHZ) {
			status_reg = PCI_STATUS_66MHZ;
			pci_write_config_word(pdev, PCI_STATUS, status_reg);
			pci_read_config_word(pdev, PCI_STATUS, &status_reg);
		}
	}

 	/*
	 *  Initialise device structure with items required by sym_attach.
	 */
	device->pdev		= pdev;
	device->s.base		= base;
	device->s.base_2	= base_2;
	device->s.base_c	= base_c;
	device->s.base_2_c	= base_2_c;
	device->s.io_port	= io_port;
	device->s.irq		= pdev->irq;

	return 0;
}

/*
 * The NCR PQS and PDS cards are constructed as a DEC bridge
 * behind which sits a proprietary NCR memory controller and
 * either four or two 53c875s as separate devices.  We can tell
 * if an 875 is part of a PQS/PDS or not since if it is, it will
 * be on the same bus as the memory controller.  In its usual
 * mode of operation, the 875s are slaved to the memory
 * controller for all transfers.  To operate with the Linux
 * driver, the memory controller is disabled and the 875s
 * freed to function independently.  The only wrinkle is that
 * the preset SCSI ID (which may be zero) must be read in from
 * a special configuration space register of the 875.
 */
void sym_config_pqs(struct pci_dev *pdev, struct sym_device *sym_dev)
{
	int slot;

	for (slot = 0; slot < 256; slot++) {
		u8 tmp;
		struct pci_dev *memc = pci_get_slot(pdev->bus, slot);

		if (!memc || memc->vendor != 0x101a || memc->device == 0x0009) {
			pci_dev_put(memc);
			continue;
		}

		/*
		 * We set these bits in the memory controller once per 875.
		 * This isn't a problem in practice.
		 */

		/* bit 1: allow individual 875 configuration */
		pci_read_config_byte(memc, 0x44, &tmp);
		tmp |= 0x2;
		pci_write_config_byte(memc, 0x44, tmp);

		/* bit 2: drive individual 875 interrupts to the bus */
		pci_read_config_byte(memc, 0x45, &tmp);
		tmp |= 0x4;
		pci_write_config_byte(memc, 0x45, tmp);

		pci_read_config_byte(pdev, 0x84, &tmp);
		sym_dev->host_id = tmp;

		pci_dev_put(memc);

		break;
	}
}

/*
 *  Called before unloading the module.
 *  Detach the host.
 *  We have to free resources and halt the NCR chip.
 */
static int sym_detach(struct sym_hcb *np)
{
	printk("%s: detaching ...\n", sym_name(np));

	del_timer_sync(&np->s.timer);

	/*
	 * Reset NCR chip.
	 * We should use sym_soft_reset(), but we don't want to do 
	 * so, since we may not be safe if interrupts occur.
	 */
	printk("%s: resetting chip\n", sym_name(np));
	OUTB (nc_istat, SRST);
	UDELAY (10);
	OUTB (nc_istat, 0);

	sym_free_resources(np);

	return 1;
}

MODULE_LICENSE("Dual BSD/GPL");

/*
 * Driver host template.
 */
static struct scsi_host_template sym2_template = {
	.module			= THIS_MODULE,
	.name			= "sym53c8xx",
	.info			= sym53c8xx_info, 
	.queuecommand		= sym53c8xx_queue_command,
	.slave_configure	= sym53c8xx_slave_configure,
	.eh_abort_handler	= sym53c8xx_eh_abort_handler,
	.eh_device_reset_handler = sym53c8xx_eh_device_reset_handler,
	.eh_bus_reset_handler	= sym53c8xx_eh_bus_reset_handler,
	.eh_host_reset_handler	= sym53c8xx_eh_host_reset_handler,
	.this_id		= 7,
	.use_clustering		= DISABLE_CLUSTERING,
#ifdef SYM_LINUX_PROC_INFO_SUPPORT
	.proc_info		= sym53c8xx_proc_info,
	.proc_name		= NAME53C8XX,
#endif
};

static int attach_count;

static int __devinit sym2_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct sym_device sym_dev;
	struct sym_nvram nvram;
	struct Scsi_Host *instance;

	memset(&sym_dev, 0, sizeof(sym_dev));
	memset(&nvram, 0, sizeof(nvram));

	if (pci_enable_device(pdev))
		return -ENODEV;

	pci_set_master(pdev);

	if (pci_request_regions(pdev, NAME53C8XX))
		goto disable;

	sym_dev.host_id = SYM_SETUP_HOST_ID;
	if (sym53c8xx_pci_init(pdev, &sym_dev))
		goto free;

	sym_config_pqs(pdev, &sym_dev);

	sym_get_nvram(&sym_dev, &nvram);

	instance = sym_attach(&sym2_template, attach_count, &sym_dev);
	if (!instance)
		goto free;

	if (scsi_add_host(instance, &pdev->dev))
		goto detach;
	scsi_scan_host(instance);

	attach_count++;

	return 0;

 detach:
	sym_detach(pci_get_drvdata(pdev));
 free:
	pci_release_regions(pdev);
 disable:
	pci_disable_device(pdev);
	return -ENODEV;
}

static void __devexit sym2_remove(struct pci_dev *pdev)
{
	struct sym_hcb *np = pci_get_drvdata(pdev);
	struct Scsi_Host *host = np->s.host;

	scsi_remove_host(host);
	scsi_host_put(host);

	sym_detach(np);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	attach_count--;
}

static void sym2_get_offset(struct scsi_device *sdev)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	spi_offset(sdev) = tp->tinfo.curr.offset;
}

static void sym2_set_offset(struct scsi_device *sdev, int offset)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	if (tp->tinfo.curr.options & PPR_OPT_DT) {
		if (offset > np->maxoffs_dt)
			offset = np->maxoffs_dt;
	} else {
		if (offset > np->maxoffs)
			offset = np->maxoffs;
	}
	tp->tinfo.goal.offset = offset;
}


static void sym2_get_period(struct scsi_device *sdev)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	spi_period(sdev) = tp->tinfo.curr.period;
}

static void sym2_set_period(struct scsi_device *sdev, int period)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	if (period <= 9 && np->minsync_dt) {
		if (period < np->minsync_dt)
			period = np->minsync_dt;
		tp->tinfo.goal.options = PPR_OPT_DT;
		tp->tinfo.goal.period = period;
		if (!tp->tinfo.curr.offset ||
					tp->tinfo.curr.offset > np->maxoffs_dt)
			tp->tinfo.goal.offset = np->maxoffs_dt;
	} else {
		if (period < np->minsync)
			period = np->minsync;
		tp->tinfo.goal.options = 0;
		tp->tinfo.goal.period = period;
		if (!tp->tinfo.curr.offset ||
					tp->tinfo.curr.offset > np->maxoffs)
			tp->tinfo.goal.offset = np->maxoffs;
	}
}

static void sym2_get_width(struct scsi_device *sdev)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	spi_width(sdev) = tp->tinfo.curr.width ? 1 : 0;
}

static void sym2_set_width(struct scsi_device *sdev, int width)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	tp->tinfo.goal.width = width;
}

static void sym2_get_dt(struct scsi_device *sdev)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	spi_dt(sdev) = (tp->tinfo.curr.options & PPR_OPT_DT) ? 1 : 0;
}

static void sym2_set_dt(struct scsi_device *sdev, int dt)
{
	struct sym_hcb *np = ((struct host_data *)sdev->host->hostdata)->ncb;
	struct sym_tcb *tp = &np->target[sdev->id];

	if (!dt) {
		/* if clearing DT, then we may need to reduce the
		 * period and the offset */
		if (tp->tinfo.curr.period < np->minsync)
			tp->tinfo.goal.period = np->minsync;
		if (tp->tinfo.curr.offset > np->maxoffs)
			tp->tinfo.goal.offset = np->maxoffs;
		tp->tinfo.goal.options &= ~PPR_OPT_DT;
	} else {
		tp->tinfo.goal.options |= PPR_OPT_DT;
	}
}
	

static struct spi_function_template sym2_transport_functions = {
	.set_offset	= sym2_set_offset,
	.get_offset	= sym2_get_offset,
	.show_offset	= 1,
	.set_period	= sym2_set_period,
	.get_period	= sym2_get_period,
	.show_period	= 1,
	.set_width	= sym2_set_width,
	.get_width	= sym2_get_width,
	.show_width	= 1,
	.get_dt		= sym2_get_dt,
	.set_dt		= sym2_set_dt,
	.show_dt	= 1,
};

static struct pci_device_id sym2_id_table[] __devinitdata = {
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C810,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C820,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL }, /* new */
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C825,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C815,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C810AP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL }, /* new */
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C860,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C1510,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C896,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C895,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C885,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C875,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C1510,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL }, /* new */
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C895A,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C875A,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C1010_33,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C1010_66,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C875J,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, sym2_id_table);

static struct pci_driver sym2_driver = {
	.name		= NAME53C8XX,
	.id_table	= sym2_id_table,
	.probe		= sym2_probe,
	.remove		= __devexit_p(sym2_remove),
};

static int __init sym2_init(void)
{
	sym2_transport_template = spi_attach_transport(&sym2_transport_functions);
	if (!sym2_transport_template)
		return -ENODEV;

	pci_register_driver(&sym2_driver);
	return 0;
}

static void __exit sym2_exit(void)
{
	pci_unregister_driver(&sym2_driver);
	spi_release_transport(sym2_transport_template);
}

module_init(sym2_init);
module_exit(sym2_exit);
