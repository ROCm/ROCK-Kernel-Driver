/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Enterprise Fibre Channel Host Bus Adapters.                     *
 * Refer to the README file included with this package for         *
 * driver version and adapter support.                             *
 * Copyright (C) 2004 Emulex Corporation.                          *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of the GNU General Public License     *
 * as published by the Free Software Foundation; either version 2  *
 * of the License, or (at your option) any later version.          *
 *                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of  *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the   *
 * GNU General Public License for more details, a copy of which    *
 * can be found in the file COPYING included with this package.    *
 *******************************************************************/

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/unistd.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/irq.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#include <scsi/scsi_device.h>
#include <asm/pci.h>
#else
#include "sd.h"			/* From drivers/scsi */
#endif

#include "hosts.h"

#include "elx_os.h"
#include "prod_os.h"
#include "elx_util.h"
#include "elx_clock.h"
#include "elx_hw.h"
#include "elx_mem.h"
#include "elx_sli.h"
#include "elx_sched.h"
#include "elx.h"
#include "elx_logmsg.h"
#include "elx_disc.h"
#include "elx_scsi.h"
#include "elx_crtn.h"
#include "prod_crtn.h"

#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)

#include <linux/spinlock.h>
#include <linux/rtnetlink.h>

#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <asm/byteorder.h>

int prodMallocCnt = 0;
int prodMallocByte = 0;
int prodFreeCnt = 0;
int prodFreeByte = 0;
int virtMallocCnt = 0;
int virtMallocByte = 0;
int virtFreeCnt = 0;
int virtFreeByte = 0;
int pciMallocCnt = 0;
int pciMallocByte = 0;
int pciFreeCnt = 0;
int pciFreeByte = 0;
int dmaGrowMallocCnt = 0;
int dmaGrowMallocByte = 0;
int dmaGrowFreeCnt = 0;
int dmaGrowFreeByte = 0;

struct elx_mem_pool *elx_mem_dmapool[MAX_ELX_BRDS];
int elx_idx_dmapool[MAX_ELX_BRDS];
int elx_size_dmapool[MAX_ELX_BRDS];
spinlock_t elx_kmem_lock;

uint32_t elx_po2(uint32_t);
void *linux_kmalloc(uint32_t, uint32_t, elx_dma_addr_t *, elxHBA_t *);
void linux_kfree(uint32_t, void *, elx_dma_addr_t, elxHBA_t *);

extern char *elx_drvr_name;
extern int lpfc_isr;
extern int lpfc_tmr;
elxDRVR_t elxDRVR;

void *
linux_kmalloc(uint32_t size,
	      uint32_t type, elx_dma_addr_t * pphys, elxHBA_t * phba)
{
	void *pcidev;
	void *virt;
	struct elx_mem_pool *fmp;
	LINUX_HBA_t *plxhba;
	elx_dma_addr_t phys;
	dma_addr_t physical = INVALID_PHYS;
	int i, instance;
	unsigned long iflag;
	int elx_size_previous, elx_size_new;

	if (pphys == 0) {
		virt = kmalloc(size, type);
		if (virt) {
			virtMallocCnt++;
			virtMallocByte += size;
		}

		return (virt);
	}

	if (phba == 0) {
		/* linux_kmalloc: Bad phba */
		elx_printf_log(0,	/* force brd 0, no p_dev_ctl */
			       &elx_msgBlk1201,	/* ptr to msg structure */
			       elx_mes1201,	/* ptr to msg */
			       elx_msgBlk1201.msgPreambleStr,	/* begin varargs */
			       size, type, elx_idx_dmapool[0]);	/* end varargs */
		return (0);
	}
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	instance = phba->brd_no;
	pcidev = plxhba->pcidev;

	if (size > FC_MAX_SEGSZ) {
		/* linux_kmalloc: Bad size */
		elx_printf_log(instance, &elx_msgBlk1202,	/* ptr to msg structure */
			       elx_mes1202,	/* ptr to msg */
			       elx_msgBlk1202.msgPreambleStr,	/* begin varargs */
			       size, type, elx_idx_dmapool[instance]);	/* end varargs */
		return (0);
	}
	spin_lock_irqsave(&elx_kmem_lock, iflag);

      top:
	fmp = elx_mem_dmapool[instance];
	for (i = 0; i <= elx_idx_dmapool[instance]; i++) {
		fmp = (elx_mem_dmapool[instance] + i);
		if ((fmp->p_virt == 0) || (fmp->p_left >= size))
			break;
	}

	if (i == (elx_size_dmapool[instance] - 2)) {
		/* Lets make it bigger */
		elx_size_previous = elx_size_dmapool[instance];
		elx_size_new =
		    (sizeof (struct elx_mem_pool) * elx_size_dmapool[instance]);
		elx_size_dmapool[instance] += FC_MAX_POOL;
		fmp =
		    kmalloc((sizeof (struct elx_mem_pool) *
			     elx_size_dmapool[instance]), GFP_ATOMIC);
		if (fmp) {
			dmaGrowMallocCnt++;
			dmaGrowMallocByte += elx_size_new;
			memset((void *)fmp, 0,
			       (sizeof (struct elx_mem_pool) *
				elx_size_dmapool[instance]));
			memcpy(fmp, (void *)elx_mem_dmapool[instance],
			       (sizeof (struct elx_mem_pool) *
				(elx_size_dmapool[instance] - FC_MAX_POOL)));
			kfree(elx_mem_dmapool[instance]);
			dmaGrowFreeCnt++;
			dmaGrowFreeByte += elx_size_previous;
			elx_mem_dmapool[instance] = fmp;
			goto top;
		}
		goto out;
	}

	if (fmp->p_virt == 0) {
		virt = pci_alloc_consistent(pcidev, FC_MAX_SEGSZ, &physical);
		if (virt) {
			pciMallocCnt++;
			pciMallocByte += FC_MAX_SEGSZ;
			fmp->p_phys = phys = physical;
			fmp->p_virt = virt;
			fmp->p_refcnt = 0;
			fmp->p_left = (ushort) FC_MAX_SEGSZ;
			if (i == elx_idx_dmapool[instance])
				if (i < (elx_size_dmapool[instance] - 2))
					elx_idx_dmapool[instance]++;
		} else {
			/* linux_kmalloc: Bad virtual addr */
			elx_printf_log(instance, &elx_msgBlk1204,	/* ptr to msg structure */
				       elx_mes1204,	/* ptr to msg */
				       elx_msgBlk1204.msgPreambleStr,	/* begin varargs */
				       i, size, type, elx_idx_dmapool[instance]);	/* end varargs */
			spin_unlock_irqrestore(&elx_kmem_lock, iflag);
			return (0);
		}
	}

	if (fmp->p_left >= size) {
		fmp->p_refcnt++;
		virt =
		    (void *)((uint8_t *) fmp->p_virt + FC_MAX_SEGSZ -
			     fmp->p_left);
		phys = fmp->p_phys + FC_MAX_SEGSZ - fmp->p_left;
		*pphys = phys;
		fmp->p_left -= size;
		spin_unlock_irqrestore(&elx_kmem_lock, iflag);
		return (virt);
	}
      out:
	spin_unlock_irqrestore(&elx_kmem_lock, iflag);
	/* linux_kmalloc: dmapool FULL */
	elx_printf_log(instance, &elx_msgBlk1205,	/* ptr to msg structure */
		       elx_mes1205,	/* ptr to msg */
		       elx_msgBlk1205.msgPreambleStr,	/* begin varargs */
		       i, size, type, elx_idx_dmapool[instance]);	/* end varargs */
	return (0);
}

void
linux_kfree(uint32_t size, void *virt, elx_dma_addr_t phys, elxHBA_t * phba)
{
	struct elx_mem_pool *fmp;
	LINUX_HBA_t *plxhba;
	void *pcidev;
	int i, instance;

	if (phys == INVALID_PHYS) {
		virtFreeCnt++;
		virtFreeByte += size;
		kfree(virt);
		return;
	}

	if (phba == 0) {
		/* linux_kfree: Bad phba */
		elx_printf_log(0,	/* force brd 0, no p_dev_ctl */
			       &elx_msgBlk1206,	/* ptr to msg structure */
			       elx_mes1206,	/* ptr to msg */
			       elx_msgBlk1206.msgPreambleStr,	/* begin varargs */
			       size, elx_idx_dmapool[0]);	/* end varargs */
		return;
	}

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	instance = phba->brd_no;
	pcidev = plxhba->pcidev;

	for (i = 0; i < elx_idx_dmapool[instance]; i++) {
		fmp = (elx_mem_dmapool[instance] + i);
		if ((virt >= fmp->p_virt) &&
		    (virt < (void *)((uint8_t *) fmp->p_virt + FC_MAX_SEGSZ))) {
			fmp->p_refcnt--;
			if (fmp->p_refcnt == 0) {
				pci_free_consistent(pcidev, FC_MAX_SEGSZ,
						    fmp->p_virt, fmp->p_phys);
				memset((void *)fmp, 0,
				       sizeof (struct elx_mem_pool));
				pciFreeCnt++;
				pciFreeByte += FC_MAX_SEGSZ;
			}
			return;
		}
	}
	/* linux_kfree: NOT in dmapool */
	elx_printf_log(instance, &elx_msgBlk1207,	/* ptr to msg structure */
		       elx_mes1207,	/* ptr to msg */
		       elx_msgBlk1207.msgPreambleStr,	/* begin varargs */
		       (uint32_t) ((unsigned long)virt), size, elx_idx_dmapool[instance]);	/* end varargs */
	return;
}

void
elx_sleep_ms(elxHBA_t * phba, int cnt)
{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((cnt * HZ / 1000) + 1);
	return;
}

int
elx_in_intr()
{
	uint32_t cur_cpu;

	cur_cpu = (uint32_t) (1 << smp_processor_id());
	if ((lpfc_isr | lpfc_tmr) & cur_cpu) {
		return (1);
	}
	return (0);
}

void
elx_pci_dma_sync(void *phbarg, void *arg, int size, int direction)
{
	DMABUF_t *pdma;
	LINUX_HBA_t *plxhba;
	elxHBA_t *phba;

	phba = (elxHBA_t *) phbarg;
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pdma = (DMABUF_t *) arg;
	if (size < PAGE_SIZE)
		size = PAGE_SIZE;
	if (direction == ELX_DMA_SYNC_FORDEV) {
		pci_dma_sync_single(plxhba->pcidev,
				    pdma->phys, size, PCI_DMA_TODEVICE);
	} else {
		pci_dma_sync_single(plxhba->pcidev,
				    pdma->phys, size, PCI_DMA_FROMDEVICE);
	}
	return;
}

int
elx_print(char *str, void *a1, void *a2)
{
	printk((const char *)str, a1, a2);
	return (1);
}

int
elx_printf_log_msgblk(int brdno, msgLogDef * msg, char *str)
{				/* String formatted by caller */
	int ddiinst;
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba = NULL;

	phba = elxDRVR.pHba[brdno];
	if (phba != NULL)
		plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	if (plxhba == NULL || phba == NULL) {
		/* Remove: This case should not occur. Sanitize anyway. More testing needed */
		printk(KERN_WARNING "%s%d:%04d:%s\n", elx_drvr_name, brdno,
		       msg->msgNum, str);
		return 1;
	}

	ddiinst = brdno;	/* Board number = instance in LINUX */
	switch (msg->msgType) {
	case ELX_LOG_MSG_TYPE_INFO:
	case ELX_LOG_MSG_TYPE_WARN:
		/* These LOG messages appear in LOG file only */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		printk(KERN_INFO "%s%d:%04d:%s\n", elx_drvr_name, ddiinst,
		       msg->msgNum, str);
#else
		dev_info(&((plxhba->pcidev)->dev), "%d:%04d:%s\n", ddiinst,
			 msg->msgNum, str);
#endif
		break;
	case ELX_LOG_MSG_TYPE_ERR_CFG:
	case ELX_LOG_MSG_TYPE_ERR:
		/* These LOG messages appear on the monitor and in the LOG file */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		printk(KERN_WARNING "%s%d:%04d:%s\n", elx_drvr_name, ddiinst,
		       msg->msgNum, str);
#else
		dev_warn(&((plxhba->pcidev)->dev), "%d:%04d:%s\n", ddiinst,
			 msg->msgNum, str);
#endif
		break;
	case ELX_LOG_MSG_TYPE_PANIC:
		panic("%s%d:%04d:%s\n", elx_drvr_name, ddiinst, msg->msgNum,
		      str);
		break;
	default:
		return (0);
	}
	return (1);
}

uint8_t *
elx_malloc(elxHBA_t * phba, MBUF_INFO_t * buf_info)
{
	uint32_t size;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	buf_info->phys = INVALID_PHYS;
	buf_info->dma_handle = 0;
	switch (buf_info->flags & ELX_MBUF_MASK) {
	case ELX_MBUF_VIRT:	/* LNX - plain virtual memory allocation */
		buf_info->size = ((buf_info->size + 7) & 0xfffffff8);
		buf_info->virt =
		    linux_kmalloc(buf_info->size, GFP_ATOMIC, 0, 0);
		if (buf_info->virt)
			memset(buf_info->virt, 0, buf_info->size);
		buf_info->phys = INVALID_PHYS;
		break;
	case ELX_MBUF_DMA:	/* LNX - dma-able memory from scratch */
		size = elx_po2(buf_info->size);
		if (size > FC_MAX_SEGSZ) {
			buf_info->virt =
			    elx_alloc_bigbuf(phba, &buf_info->phys, size);
			if (buf_info->virt) {
				if (buf_info->phys == INVALID_PHYS) {
					elx_free_bigbuf(phba, buf_info->virt,
							buf_info->phys, size);
					buf_info->virt = 0;
				}
			}
		} else {
			buf_info->phys = INVALID_PHYS;
			buf_info->virt =
			    linux_kmalloc(size, GFP_ATOMIC, &buf_info->phys,
					  phba);
			if (buf_info->virt) {
				if (buf_info->phys == INVALID_PHYS) {
					linux_kfree(size, buf_info->virt,
						    buf_info->phys, phba);
					buf_info->virt = 0;
				}
			}
		}

		buf_info->dma_handle = buf_info->phys;

		if (buf_info->virt == 0) {
			buf_info->phys = INVALID_PHYS;
			buf_info->dma_handle = 0;
		}
		break;
	case ELX_MBUF_PHYSONLY:	/* LNX - convert virtual to dma-able */
		if (buf_info->virt == NULL)
			break;

		buf_info->phys =
		    (elx_dma_addr_t) elx_pci_map(phba, buf_info->virt,
						 buf_info->size,
						 PCI_DMA_BIDIRECTIONAL);

		buf_info->dma_handle = buf_info->phys;

		break;
	}
	return ((uint8_t *) buf_info->virt);
}

uint32_t
elx_po2(uint32_t size)
{
	uint32_t order;

	for (order = 1; order < size; order <<= 1) ;
	return (order);
}

void
elx_free(elxHBA_t * phba, MBUF_INFO_t * buf_info)
{
	uint32_t size;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	switch (buf_info->flags & ELX_MBUF_MASK) {
	case ELX_MBUF_VIRT:	/* LNX - plain virtual memory allocation */
		buf_info->size = ((buf_info->size + 7) & 0xfffffff8);
		linux_kfree(buf_info->size, buf_info->virt, INVALID_PHYS, 0);
		break;
	case ELX_MBUF_DMA:	/* LNX - dma-able memory from scratch */
		size = elx_po2(buf_info->size);
		if (size > FC_MAX_SEGSZ) {
			elx_free_bigbuf(phba, buf_info->virt, buf_info->phys,
					size);
		} else {
			linux_kfree(buf_info->size, buf_info->virt,
				    buf_info->phys, phba);
		}
		break;
	case ELX_MBUF_PHYSONLY:	/* LNX - convert virtual to dma-able */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,12)
		pci_unmap_single(plxhba->pcidev,
				 buf_info->phys, buf_info->size,
				 PCI_DMA_BIDIRECTIONAL);
#else
		pci_unmap_page(plxhba->pcidev,
			       buf_info->phys, buf_info->size,
			       PCI_DMA_BIDIRECTIONAL);
#endif
		break;
	}
}

void *
elx_kmem_alloc(unsigned int size, int flag)
{
	void *ptr;

	if (flag == ELX_MEM_DELAY) {
		ptr = kmalloc(size, GFP_KERNEL);
	} else {
		ptr = kmalloc(size, GFP_ATOMIC);
	}
	if (ptr) {
		prodMallocCnt++;
		prodMallocByte += size;
	}
	return (ptr);
}

void
elx_kmem_free(void *obj, unsigned int size)
{
	if (obj) {
		prodFreeCnt++;
		prodFreeByte += size;
		kfree(obj);
	}
}

void *
elx_kmem_zalloc(unsigned int size, int flag)
{
	void *ptr = elx_kmem_alloc(size, flag);
	if (ptr)
		memset(ptr, 0, size);
	return (ptr);
}

void
elx_sli_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->slilock.elx_lock);
	return;
}

void
elx_sli_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = 0;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_irqsave(&lhba->slilock.elx_lock, flag);
	*iflag = flag;
	return;
}

void
elx_sli_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = *iflag;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_unlock_irqrestore(&lhba->slilock.elx_lock, flag);
	return;
}

void
elx_mem_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->memlock.elx_lock);
	return;
}

void
elx_mem_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = 0;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_irqsave(&lhba->memlock.elx_lock, flag);
	*iflag = flag;
	return;
}

void
elx_mem_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = *iflag;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_unlock_irqrestore(&lhba->memlock.elx_lock, flag);
	return;
}

void
elx_sch_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->schlock.elx_lock);
	return;
}

void
elx_sch_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = 0;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_irqsave(&lhba->schlock.elx_lock, flag);
	*iflag = flag;
	return;
}

void
elx_sch_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = *iflag;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_unlock_irqrestore(&lhba->schlock.elx_lock, flag);
	return;
}

void
elx_ioc_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->ioclock.elx_lock);
	return;
}

void
elx_ioc_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = 0;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_irqsave(&lhba->ioclock.elx_lock, flag);
	*iflag = flag;
	return;
}

void
elx_ioc_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = *iflag;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_unlock_irqrestore(&lhba->ioclock.elx_lock, flag);
	return;
}

void
elx_drvr_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->drvrlock.elx_lock);
	return;
}

void
elx_drvr_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;
	int i;

	if (phba) {
		flag = 0;
		lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
		spin_lock_irqsave(&lhba->drvrlock.elx_lock, flag);
		*iflag = flag;
		phba->iflag = flag;
	} else {

		flag = 0;
		for (i = 0; i < elxDRVR.num_devs; i++) {
			if ((phba = elxDRVR.pHba[i]) != 0) {
				flag = 0;
				lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
				spin_lock_irqsave(&lhba->drvrlock.elx_lock,
						  flag);
				*iflag++ = flag;
				phba->iflag = flag;
			}
		}
	}
	return;
}

void
elx_drvr_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;
	int i;

	if (phba) {
		flag = phba->iflag;
		lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
		spin_unlock_irqrestore(&lhba->drvrlock.elx_lock, flag);
	} else {

		iflag += (elxDRVR.num_devs - 1);
		for (i = (elxDRVR.num_devs - 1); i >= 0; i--) {
			if ((phba = elxDRVR.pHba[i]) != 0) {
				flag = phba->iflag;
				lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
				spin_unlock_irqrestore(&lhba->drvrlock.elx_lock,
						       flag);
			}
		}
	}
	return;
}

void
elx_clk_init_lock(elxHBA_t * phba)
{
	LINUX_DRVR_t *ldrvr;

	/* This lock is per driver */
	ldrvr = (LINUX_DRVR_t *) elxDRVR.pDrvrOSEnv;
	spin_lock_init(&ldrvr->clklock.elx_lock);
	return;
}

void
elx_clk_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_DRVR_t *ldrvr;

	/* This lock is per driver */
	flag = 0;
	ldrvr = (LINUX_DRVR_t *) elxDRVR.pDrvrOSEnv;
	spin_lock_irqsave(&ldrvr->clklock.elx_lock, flag);
	elxDRVR.cflag = flag;
	*iflag = flag;
	return;
}

void
elx_clk_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_DRVR_t *ldrvr;

	flag = elxDRVR.cflag;
	ldrvr = (LINUX_DRVR_t *) elxDRVR.pDrvrOSEnv;
	spin_unlock_irqrestore(&ldrvr->clklock.elx_lock, flag);
	return;
}

void
elx_disc_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->disclock.elx_lock);
	return;
}

void
elx_hipri_init_lock(elxHBA_t * phba)
{
	LINUX_HBA_t *lhba;

	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_init(&lhba->hiprilock.elx_lock);
	return;
}

void
elx_hipri_lock(elxHBA_t * phba, unsigned long *iflag)
{
	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = 0;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_irqsave(&lhba->hiprilock.elx_lock, flag);
	*iflag = flag;
	return;
}

void
elx_hipri_unlock(elxHBA_t * phba, unsigned long *iflag)
{
	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = *iflag;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_unlock_irqrestore(&lhba->hiprilock.elx_lock, flag);
	return;
}

void
elx_disc_lock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = 0;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_lock_irqsave(&lhba->disclock.elx_lock, flag);
	*iflag = flag;
	return;
}

void
elx_disc_unlock(elxHBA_t * phba, unsigned long *iflag)
{

	unsigned long flag;
	LINUX_HBA_t *lhba;

	flag = *iflag;
	lhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	spin_unlock_irqrestore(&lhba->disclock.elx_lock, flag);
	return;
}

uint16_t
elx_read_pci_cmd(elxHBA_t * phba)
{
	uint16_t cmd;
	struct pci_dev *pdev;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/*
	 * PCI device
	 */
	pdev = plxhba->pcidev;
	if (!pdev) {
		panic("no dev in elx_read_pci_cmd\n");
		return ((uint16_t) 0);
	}
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	return ((uint16_t) cmd);
}

uint32_t
elx_read_pci(elxHBA_t * phba, int offset)
{
	uint32_t cmd;
	struct pci_dev *pdev;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/*
	 * PCI device
	 */
	pdev = plxhba->pcidev;
	if (!pdev) {
		panic("no dev in elx_read_pci\n");
		return ((ushort) 0);
	}
	pci_read_config_dword(pdev, offset, &cmd);
	return (cmd);
}

void
elx_write_pci_cmd(elxHBA_t * phba, uint16_t cfg_value)
{
	struct pci_dev *pdev;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/*
	 * PCI device
	 */
	pdev = plxhba->pcidev;
	if (!pdev) {
		panic("no dev in elx_write_pci_cmd\n");
		return;
	}
	pci_write_config_word(pdev, PCI_COMMAND, cfg_value);
}

void
elx_cnt_read_pci(elxHBA_t * phba,
		 uint32_t offset, uint32_t cnt, uint32_t * pci_space)
{
	int i;

	for (i = offset; i < (offset + cnt); i += 4) {
		*pci_space = elx_read_pci(phba, i);
		pci_space++;
	}
}

void
elx_cnt_write_pci(elxHBA_t * phba,
		  uint32_t offset, uint32_t cnt, uint32_t * cfg_value)
{
	struct pci_dev *pdev;
	LINUX_HBA_t *plxhba;
	int i;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/*
	 * PCI device
	 */
	pdev = plxhba->pcidev;
	if (!pdev) {
		panic("no dev in elx_write_pci_cmd\n");
		return;
	}

	for (i = offset; i < (offset + cnt); i += 4) {
		pci_write_config_dword(pdev, i, *cfg_value);
		cfg_value++;
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
int
elx_biosparam(struct scsi_device *psdev,
	      struct block_device *pbdev, sector_t capacity, int ip[])
{
	int size = capacity;
#else
int
elx_biosparam(Disk * disk, kdev_t n, int ip[])
{
	int size = disk->capacity;
#endif

	ip[0] = 64;
	ip[1] = 32;
	ip[2] = size >> 11;
	if (ip[2] > 1024) {
		ip[0] = 255;
		ip[1] = 63;
		ip[2] = size / (ip[0] * ip[1]);
#ifndef FC_EXTEND_TRANS_A
		if (ip[2] > 1023)
			ip[2] = 1023;
#endif
	}
	return (0);
}

void *
elx_remap_pci_mem(unsigned long base, unsigned long size)
{
	void *ptr;

	ptr = ioremap(base, size);
	return (ptr);
}

void
elx_unmap_pci_mem(unsigned long vaddr)
{
	if (vaddr) {
		iounmap((void *)(vaddr));
		/* For some bizarre reason, LINUX used to hang in this
		 * call when running insmod / rmmod in a loop overnight.
		 * In later versions of the kernel, this appears to be fixed.
		 */
	}
	return;
}

int
elx_pci_getadd(struct pci_dev *pdev, int reg, unsigned long *base)
{
	*base = pci_resource_start(pdev, reg);
	reg++;
	return (++reg);
}

void
elx_write_toio(uint32_t * src, uint32_t * dest_io, uint32_t cnt)
{
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = *src++;
		writel(ldata, dest_io);
		dest_io++;
	}
	return;
}

void
elx_read_fromio(uint32_t * src_io, uint32_t * dest, uint32_t cnt)
{
	uint32_t ldata;
	int i;

	for (i = 0; i < (int)cnt; i += sizeof (uint32_t)) {
		ldata = readl(src_io);
		src_io++;
		*dest++ = ldata;
	}
	return;
}

void *
elx_alloc_bigbuf(elxHBA_t * phba, elx_dma_addr_t * pphys, uint32_t size)
{
	void *pcidev;
	void *virt;
	LINUX_HBA_t *plxhba;
	/* pci_alloc_consistent always takes a pointer to dma_addr_t */
	dma_addr_t phys;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pcidev = plxhba->pcidev;
	virt = pci_alloc_consistent(pcidev, size, &phys);
	if (virt) {
		prodMallocCnt++;
		prodMallocByte += size;

		*pphys = phys;
		return (virt);
	}
	return (0);
}

void
elx_free_bigbuf(elxHBA_t * phba, void *virt, elx_dma_addr_t phys, uint32_t size)
{
	struct pci_dev *pcidev;
	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	pcidev = plxhba->pcidev;
	pci_free_consistent(pcidev, size, virt, phys);
	prodFreeCnt++;
	prodFreeByte += size;
	return;
}

void
elx_nodev(unsigned long l)
{
	return;
}

void
elx_ip_get_rcv_buf(elxHBA_t * phba, DMABUFIP_t * matip, uint32_t size)
{
	struct sk_buff *skb;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;

	skb = alloc_skb(size, GFP_ATOMIC);
	if (skb) {

		buf_info = &bufinfo;
		buf_info->phys = INVALID_PHYS;
		buf_info->virt = skb->data;
		buf_info->size = size;
		buf_info->flags = ELX_MBUF_PHYSONLY;
		elx_malloc(phba, buf_info);

		matip->ipbuf = skb;
		if (buf_info->phys != INVALID_PHYS) {
			matip->dma.virt = skb->data;
			matip->dma.phys = buf_info->phys;
		} else {
			elx_ip_free_rcv_buf(phba, matip, size);
			matip->ipbuf = 0;
		}
	}
	return;
}

void
elx_ip_free_rcv_buf(elxHBA_t * phba, DMABUFIP_t * matip, uint32_t size)
{
	struct sk_buff *skb;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;

	while (matip) {
		skb = matip->ipbuf;
		if (skb) {
			if (matip->dma.phys != INVALID_PHYS) {
				buf_info = &bufinfo;
				buf_info->phys = matip->dma.phys;
				buf_info->virt = 0;
				buf_info->size = size;
				buf_info->flags = ELX_MBUF_PHYSONLY;
				elx_free(phba, buf_info);
			}

			if (in_irq()) {
				dev_kfree_skb_irq(skb);
			} else {
				dev_kfree_skb(skb);
			}
		}
		matip = (DMABUFIP_t *) matip->dma.next;
	}
	return;
}

void
elx_wakeup(elxHBA_t * phba, void *wait_q_head)
{
	wake_up_interruptible((wait_queue_head_t *) wait_q_head);
	return;
}

int
elx_sleep(elxHBA_t * phba, void *wait_q_head, long tmo)
{
	wait_queue_t wq_entry;
	unsigned long iflag = phba->iflag;
	int rc = 1;
	long left;

	init_waitqueue_entry(&wq_entry, current);
	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue((wait_queue_head_t *) wait_q_head, &wq_entry);
	if (tmo > 0) {
		ELX_DRVR_UNLOCK(phba, iflag);
		left = schedule_timeout(tmo * HZ);
		ELX_DRVR_LOCK(phba, iflag);
	} else {
		ELX_DRVR_UNLOCK(phba, iflag);
		schedule();
		ELX_DRVR_LOCK(phba, iflag);
		left = 0;
	}
	remove_wait_queue((wait_queue_head_t *) wait_q_head, &wq_entry);

	if (signal_pending(current))
		return (EINTR);
	if (rc > 0)
		return (0);
	else
		return (ETIMEDOUT);
}

uint8_t *
lpfc_get_lpfchba_info(elxHBA_t * phba, uint8_t * buf)
{

	LINUX_HBA_t *plxhba;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	elx_str_sprintf(buf,
			"LINUX_HBA_t structure : \n\n  pci_bar0: 0x%08lx  pci_bar1: 0x%08lx \n HAregaddr: 0x%08lx CAregaddr: 0x%08lx\n HSregaddr: 0x%8lx HCregaddr: 0x%08lx \n",
			(long unsigned int)plxhba->pci_bar0_map,
			(long unsigned int)plxhba->pci_bar1_map,
			(long unsigned int)plxhba->HAregaddr,
			(long unsigned int)plxhba->CAregaddr,
			(long unsigned int)plxhba->HSregaddr,
			(long unsigned int)plxhba->HCregaddr);

	return (buf);
}

uint64_t
elx_pci_map(elxHBA_t * phba, void *virt, int size, int dir)
{
	dma_addr_t physaddr;
	LINUX_HBA_t *plxhba;
#ifdef KERNEL_HAS_PCI_MAP_PAGE
	struct page *page;
	unsigned long offset;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	page = virt_to_page(virt);
	offset = ((unsigned long)virt & ~PAGE_MASK);
	physaddr = 0;
	while (physaddr == 0) {
		physaddr =
		    pci_map_page(plxhba->pcidev, page, offset, size, dir);
#ifndef powerpc
		break;
#endif				/* endif powerpc */
	}
#else
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	physaddr = pci_map_single(plxhba->pcidev, virt, size, dir);
#endif				/* KERNEL_HAS_PCI_MAP_PAGE */
	return (uint64_t) physaddr;
}

void
lpfc_set_pkt_len(void *buf, uint32_t length)
{
	((struct sk_buff *)(buf))->len = length;
	return;
}

void *
lpfc_get_pkt_data(void *buf)
{
	return ((void *)(((struct sk_buff *)(buf))->data));
}

/* Includes. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <linux/blk.h>
#else
#endif
#include <scsi/scsi.h>
#include <scsi.h>

#include "elx_os_scsiport.h"
#include "elx_cfgparm.h"

typedef struct elx_xlat_err {
	uint16_t iocb_status;
	uint16_t host_status;
	uint16_t action_flag;
} elx_xlat_err_t;

/* Defines for action flags */
#define ELX_DELAY_IODONE    0x1
#define ELX_FCPRSP_ERROR    0x2
#define ELX_IOERR_TABLE     0x4
#define ELX_STAT_ACTION     0x8
#define ELX_REQUEUE         0x10

#define ELX_CMD_BEING_RETRIED  0xFFFF

/* This table is indexed by the IOCB ulpStatus */

elx_xlat_err_t elx_iostat_tbl[IOSTAT_CNT] = {
/* f/w code            host_status   flag */

	{IOSTAT_SUCCESS, DID_OK, 0},	/* 0x0 */
	{IOSTAT_FCP_RSP_ERROR, DID_OK, ELX_FCPRSP_ERROR},	/* 0x1 */
	{IOSTAT_REMOTE_STOP, DID_ERROR, 0},	/* 0x2 */
	{IOSTAT_LOCAL_REJECT, DID_ERROR, ELX_IOERR_TABLE},	/* 0x3 */
	{IOSTAT_NPORT_RJT, DID_ERROR, ELX_STAT_ACTION},	/* 0x4 */
	{IOSTAT_FABRIC_RJT, DID_ERROR, ELX_STAT_ACTION},	/* 0x5 */
	{IOSTAT_NPORT_BSY, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x6 */
	{IOSTAT_FABRIC_BSY, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x7 */
	{IOSTAT_INTERMED_RSP, DID_ERROR, 0},	/* 0x8 */
	{IOSTAT_LS_RJT, DID_ERROR, 0},	/* 0x9 */
	{IOSTAT_BA_RJT, DID_ERROR, 0},	/* 0xa */
	{IOSTAT_DRIVER_REJECT, DID_ERROR, ELX_DELAY_IODONE},	/* 0xb */
	{IOSTAT_ISCSI_REJECT, DID_OK, ELX_REQUEUE},	/* 0xc */
	{IOSTAT_DEFAULT, DID_ERROR, 0}	/* 0xd */
};

/* This table is indexed by the IOCB perr.statLocalError */

elx_xlat_err_t elx_ioerr_tbl[IOERR_CNT] = {

/* f/w code                     host_status     flag */
	{0, DID_ERROR, ELX_DELAY_IODONE},
	{IOERR_MISSING_CONTINUE, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x1  */
	{IOERR_SEQUENCE_TIMEOUT, DID_ERROR, ELX_DELAY_IODONE},	/* 0x2  */
	{IOERR_INTERNAL_ERROR, DID_ERROR, ELX_DELAY_IODONE},	/* 0x3  */
	{IOERR_INVALID_RPI, DID_ERROR, ELX_DELAY_IODONE},	/* 0x4  */
	{IOERR_NO_XRI, DID_ERROR, ELX_DELAY_IODONE},	/* 0x5  */
	{IOERR_ILLEGAL_COMMAND, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x6  */
	{IOERR_XCHG_DROPPED, DID_ERROR, ELX_DELAY_IODONE},	/* 0x7  */
	{IOERR_ILLEGAL_FIELD, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x8  */
	{IOERR_BAD_CONTINUE, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x9  */
	{IOERR_TOO_MANY_BUFFERS, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0xA  */
	{IOERR_RCV_BUFFER_WAITING, DID_ERROR, ELX_DELAY_IODONE},	/* 0xB  */
	{IOERR_NO_CONNECTION, DID_ERROR, ELX_DELAY_IODONE},	/* 0xC  */
	{IOERR_TX_DMA_FAILED, DID_ERROR, ELX_DELAY_IODONE},	/* 0xD  */
	{IOERR_RX_DMA_FAILED, DID_ERROR, ELX_DELAY_IODONE},	/* 0xE  */
	{IOERR_ILLEGAL_FRAME, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0xF  */
	{IOERR_EXTRA_DATA, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x10 */
	{IOERR_NO_RESOURCES, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x11 */
	{0, DID_ERROR, ELX_DELAY_IODONE},	/* 0x12 */
	{IOERR_ILLEGAL_LENGTH, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x13 */
	{IOERR_UNSUPPORTED_FEATURE, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x14 */
	{IOERR_ABORT_IN_PROGRESS, DID_ERROR, ELX_DELAY_IODONE},	/* 0x15 */
	{IOERR_ABORT_REQUESTED, DID_ERROR, ELX_DELAY_IODONE},	/* 0x16 */
	{IOERR_RECEIVE_BUFFER_TIMEOUT, DID_ERROR, ELX_DELAY_IODONE},	/* 0x17 */
	{IOERR_LOOP_OPEN_FAILURE, DID_ERROR, ELX_DELAY_IODONE},	/* 0x18 */
	{IOERR_RING_RESET, DID_ERROR, ELX_DELAY_IODONE},	/* 0x19 */
	{IOERR_LINK_DOWN, DID_ERROR, ELX_DELAY_IODONE},	/* 0x1A */
	{IOERR_CORRUPTED_DATA, DID_ERROR, ELX_DELAY_IODONE},	/* 0x1B */
	{IOERR_CORRUPTED_RPI, DID_ERROR, ELX_DELAY_IODONE},	/* 0x1C */
	{IOERR_OUT_OF_ORDER_DATA, DID_ERROR, ELX_DELAY_IODONE},	/* 0x1D */
	{IOERR_OUT_OF_ORDER_ACK, DID_ERROR, ELX_DELAY_IODONE},	/* 0x1E */
	{IOERR_DUP_FRAME, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x1F */
	{IOERR_LINK_CONTROL_FRAME, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x20 */
	{IOERR_BAD_HOST_ADDRESS, DID_ERROR, ELX_DELAY_IODONE},	/* 0x21 */
	{IOERR_RCV_HDRBUF_WAITING, DID_ERROR, ELX_DELAY_IODONE},	/* 0x22 */
	{IOERR_MISSING_HDR_BUFFER, DID_ERROR, ELX_DELAY_IODONE},	/* 0x23 */
	{IOERR_MSEQ_CHAIN_CORRUPTED, DID_ERROR, ELX_DELAY_IODONE},	/* 0x24 */
	{IOERR_ABORTMULT_REQUESTED, DID_ERROR, ELX_DELAY_IODONE},	/* 0x25 */
	{0, DID_ERROR, ELX_DELAY_IODONE},	/* 0x26 */
	{0, DID_ERROR, ELX_DELAY_IODONE},	/* 0x27 */
	{IOERR_BUFFER_SHORTAGE, DID_BUS_BUSY, ELX_DELAY_IODONE},	/* 0x28 */
	{IOERR_DEFAULT, DID_ERROR, ELX_DELAY_IODONE}	/* 0x29 */

};

#define ScsiResult(host_code, scsi_code) (((host_code) << 16) | scsi_code)

#define scsi_sg_dma_address(sc)         sg_dma_address(sc)
#define scsi_sg_dma_len(sc)             sg_dma_len(sc)

ELXSCSITARGET_t *lpfc_find_target(elxHBA_t *, uint32_t);
int lpfc_ValidLun(ELXSCSITARGET_t *, uint64_t);

extern int lpfc_use_data_direction;

void elx_scsi_add_timer(Scsi_Cmnd *, int);
int elx_scsi_delete_timer(Scsi_Cmnd *);
uint32_t elx_os_fcp_err_handle(ELX_SCSI_BUF_t *, elx_xlat_err_t *);

int
elx_data_direction(Scsi_Cmnd * Cmnd)
{
	int ret_code;

	switch (Cmnd->cmnd[0]) {
	case WRITE_6:
	case WRITE_10:
	case WRITE_12:
	case CHANGE_DEFINITION:
	case LOG_SELECT:
	case MODE_SELECT:
	case MODE_SELECT_10:
	case WRITE_BUFFER:
	case VERIFY:
	case WRITE_VERIFY:
	case WRITE_VERIFY_12:
	case WRITE_LONG:
	case WRITE_LONG_2:
	case WRITE_SAME:
	case SEND_DIAGNOSTIC:
	case FORMAT_UNIT:
	case REASSIGN_BLOCKS:
	case FCP_SCSI_RELEASE_LUNR:
	case FCP_SCSI_RELEASE_LUNV:
	case HPVA_SETPASSTHROUGHMODE:
	case HPVA_EXECUTEPASSTHROUGH:
	case HPVA_CREATELUN:
	case HPVA_SETLUNSECURITYLIST:
	case HPVA_SETCLOCK:
	case HPVA_RECOVER:
	case HPVA_GENERICSERVICEOUT:
	case DMEP_EXPORT_OUT:
		ret_code = SCSI_DATA_WRITE;
		break;
	case MDACIOCTL_DIRECT_CMD:
		switch (Cmnd->cmnd[2]) {
		case MDACIOCTL_STOREIMAGE:
		case MDACIOCTL_WRITESIGNATURE:
		case MDACIOCTL_SETREALTIMECLOCK:
		case MDACIOCTL_PASS_THRU_CDB:
		case MDACIOCTL_CREATENEWCONF:
		case MDACIOCTL_ADDNEWCONF:
		case MDACIOCTL_MORE:
		case MDACIOCTL_SETPHYSDEVPARAMETER:
		case MDACIOCTL_SETLOGDEVPARAMETER:
		case MDACIOCTL_SETCONTROLLERPARAMETER:
		case MDACIOCTL_WRITESANMAP:
		case MDACIOCTL_SETMACADDRESS:
			ret_code = SCSI_DATA_WRITE;
			break;
		case MDACIOCTL_PASS_THRU_INITIATE:
			if (Cmnd->cmnd[3] & 0x80) {
				ret_code = SCSI_DATA_WRITE;
			} else {
				ret_code = SCSI_DATA_READ;
			}
			break;
		default:
			ret_code = SCSI_DATA_READ;
		}
		break;
	default:
		if (Cmnd->sc_data_direction == SCSI_DATA_WRITE)
			ret_code = SCSI_DATA_WRITE;
		else
			ret_code = SCSI_DATA_READ;
	}

	Cmnd->sc_data_direction = ret_code;
	return (ret_code);
}

int
elx_os_prep_io(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	LINUX_HBA_t *plxhba;
	FCP_CMND *fcp_cmnd;
	ULP_BDE64 *topbpl;
	ULP_BDE64 *bpl;
	DMABUF_t *bmp;
	DMABUF_t *last_bmp;
	IOCB_t *cmd;
	Scsi_Cmnd *cmnd;
	struct scatterlist *sgel_p;
#ifdef powerpc
	struct scatterlist *sgel_p_t0;
#endif				/* endif powerpc */
	elx_dma_addr_t physaddr;
	uint32_t seg_cnt, cnt, i;
	uint32_t num_bmps, num_bde, max_bde;
	uint16_t use_sg;
	int datadir;

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	bpl = elx_cmd->fcp_bpl;
	fcp_cmnd = elx_cmd->fcp_cmnd;

	bpl += 2;		/* Bump past FCP CMND and FCP RSP */
	max_bde = ELX_SCSI_INITIAL_BPL_SIZE;

	cmnd = (Scsi_Cmnd *) elx_cmd->pOSCmd;
	cmd = &elx_cmd->cur_iocbq.iocb;

	/* These are needed if we chain BPLs */
	last_bmp = elx_cmd->dma_ext;
	num_bmps = 1;
	topbpl = 0;

	use_sg = cmnd->use_sg;
	num_bde = 0;
	sgel_p = 0;

	/*
	 * Fill in the FCP CMND
	 */
	memcpy((void *)&fcp_cmnd->fcpCdb[0], (void *)cmnd->cmnd, 16);

	if (cmnd->device->tagged_supported) {
		switch (cmnd->tag) {
		case HEAD_OF_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = HEAD_OF_Q;
			break;
		case ORDERED_QUEUE_TAG:
			fcp_cmnd->fcpCntl1 = ORDERED_Q;
			break;
		default:
			fcp_cmnd->fcpCntl1 = SIMPLE_Q;
			break;
		}
	} else {
		fcp_cmnd->fcpCntl1 = 0;
	}

	if (cmnd->cmnd[0] == TEST_UNIT_READY) {
		goto nodata;
	}

	if (lpfc_use_data_direction) {
		datadir = cmnd->sc_data_direction;
	} else {
		datadir = elx_data_direction(cmnd);
	}
	elx_cmd->OS_io_info.datadir = datadir;

	/* This next section finishes building the BPL for the I/O from the
	 * scsi_cmnd and updates the IOCB accordingly.
	 */
	if (use_sg) {
		sgel_p = (struct scatterlist *)cmnd->request_buffer;
#ifdef powerpc			/* remap to get different set of phys adds that xclud zero */
	      remapsgl:
#endif				/* endif powerpc */
		seg_cnt = pci_map_sg(plxhba->pcidev, sgel_p, use_sg,
				     scsi_to_pci_dma_dir(datadir));

		/* return error if we cannot map sg list */
		if (seg_cnt == 0) {
			return (1);
		}
#ifdef powerpc			/* check for zero phys address, then remap to get diff ones */
		for (sgel_p_t0 = sgel_p, i = 0; i < seg_cnt; sgel_p_t0++, i++) {
			if (!scsi_sg_dma_address(sgel_p_t0)) {
				goto remapsgl;
			}
		}
#endif				/* endif powerpc */
		cnt = 0;
		/* scatter-gather list case */
		for (i = 0; i < seg_cnt; i++) {
			/* Check to see if current BPL is full of BDEs */
			if (num_bde == max_bde) {
				if ((bmp =
				     (DMABUF_t *) elx_mem_get(phba,
							      MEM_BPL)) == 0) {
					break;
				}
				max_bde = ((1024 / sizeof (ULP_BDE64)) - 3);
				/* Fill in continuation entry to next bpl */
				bpl->addrHigh = putPaddrHigh(bmp->phys);
				bpl->addrHigh = PCIMEM_LONG(bpl->addrHigh);
				bpl->addrLow = putPaddrLow(bmp->phys);
				bpl->addrLow = PCIMEM_LONG(bpl->addrLow);
				bpl->tus.f.bdeFlags = BPL64_SIZE_WORD;
				num_bde++;
				if (num_bmps == 1) {
					cmd->un.fcpi64.bdl.bdeSize +=
					    (num_bde * sizeof (ULP_BDE64));
				} else {
					topbpl->tus.f.bdeSize =
					    (num_bde * sizeof (ULP_BDE64));
					topbpl->tus.w =
					    PCIMEM_LONG(topbpl->tus.w);
				}
				topbpl = bpl;
				bpl = (ULP_BDE64 *) bmp->virt;
				last_bmp->next = (void *)bmp;
				last_bmp = bmp;
				num_bde = 0;
				num_bmps++;
			}

			physaddr = scsi_sg_dma_address(sgel_p);

			bpl->addrLow = PCIMEM_LONG(putPaddrLow(physaddr));
			bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(physaddr));
			bpl->tus.f.bdeSize = scsi_sg_dma_len(sgel_p);
			cnt += bpl->tus.f.bdeSize;
			if (datadir == SCSI_DATA_WRITE) {
				bpl->tus.f.bdeFlags = 0;
			} else {
				bpl->tus.f.bdeFlags = BUFF_USE_RCV;
			}
			bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
			bpl++;
			sgel_p++;
			num_bde++;
		}		/* end for loop */

		if (datadir == SCSI_DATA_WRITE) {
			cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
			fcp_cmnd->fcpCntl3 = WRITE_DATA;

			phba->fc4OutputRequests++;
		} else {
			cmd->ulpCommand = CMD_FCP_IREAD64_CR;
			cmd->ulpPU = PARM_READ_CHECK;
			cmd->un.fcpi.fcpi_parm = cnt;
			fcp_cmnd->fcpCntl3 = READ_DATA;

			phba->fc4InputRequests++;
		}
	} else {
		if (cmnd->request_buffer && cmnd->request_bufflen) {

			physaddr =
			    (elx_dma_addr_t) elx_pci_map(phba,
							 cmnd->request_buffer,
							 cmnd->request_bufflen,
							 scsi_to_pci_dma_dir
							 (datadir));

			/* no scatter-gather list case */
			elx_cmd->OS_io_info.nonsg_phys = physaddr;
			bpl->addrLow = PCIMEM_LONG(putPaddrLow(physaddr));
			bpl->addrHigh = PCIMEM_LONG(putPaddrHigh(physaddr));
			bpl->tus.f.bdeSize = cmnd->request_bufflen;
			cnt = cmnd->request_bufflen;
			if (datadir == SCSI_DATA_WRITE) {
				cmd->ulpCommand = CMD_FCP_IWRITE64_CR;
				fcp_cmnd->fcpCntl3 = WRITE_DATA;
				bpl->tus.f.bdeFlags = 0;

				phba->fc4OutputRequests++;
			} else {
				cmd->ulpCommand = CMD_FCP_IREAD64_CR;
				cmd->ulpPU = PARM_READ_CHECK;
				cmd->un.fcpi.fcpi_parm = cnt;
				fcp_cmnd->fcpCntl3 = READ_DATA;
				bpl->tus.f.bdeFlags = BUFF_USE_RCV;

				phba->fc4InputRequests++;
			}
			bpl->tus.w = PCIMEM_LONG(bpl->tus.w);
			num_bde = 1;
			bpl++;
		} else {
		      nodata:
			cnt = 0;
			cmd->ulpCommand = CMD_FCP_ICMND64_CR;
			cmd->un.fcpi.fcpi_parm = 0;
			fcp_cmnd->fcpCntl3 = 0;

			phba->fc4ControlRequests++;
		}
	}
	bpl->addrHigh = 0;
	bpl->addrLow = 0;
	bpl->tus.w = 0;
	if (num_bmps == 1) {
		cmd->un.fcpi64.bdl.bdeSize += (num_bde * sizeof (ULP_BDE64));
	} else {
		topbpl->tus.f.bdeSize = (num_bde * sizeof (ULP_BDE64));
		topbpl->tus.w = PCIMEM_LONG(topbpl->tus.w);
	}
	cmd->ulpBdeCount = 1;
	cmd->ulpLe = 1;		/* Set the LE bit in the iocb */

	/* set the Data Length field in the FCP CMND accordingly */
	fcp_cmnd->fcpDl = SWAP_DATA(cnt);

	return (0);
}

int
elx_queuecommand(Scsi_Cmnd * cmnd, void (*done) (Scsi_Cmnd *))
{
	elxHBA_t *phba;
	LINUX_HBA_t *plxhba;
	ELX_SCSI_BUF_t *elx_cmd;
	int ret;
	void (*old_done) (Scsi_Cmnd *);
	unsigned long iflag;
	ELXSCSITARGET_t *targetp;
	elxCfgParam_t *clp;
	struct Scsi_Host *host;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	host = cmnd->device->host;
#else
	host = cmnd->host;
#endif

	phba = (elxHBA_t *) host->hostdata[0];
	clp = &phba->config[0];
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	ELX_DRVR_LOCK(phba, iflag);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	/* 
	   if the hba is in blocked state and the command is a retry 
	   queue the command and retry success 
	 */
	if (plxhba->in_retry) {
		cmnd->scsi_done = done;
		cmnd->reset_chain = plxhba->cmnd_retry_list;
		plxhba->cmnd_retry_list = cmnd;
		cmnd->host_scribble = 0;
		ELX_DRVR_UNLOCK(phba, iflag);
		return (0);
	}
#endif

	elx_cmd = elx_get_scsi_buf(phba);
	if (elx_cmd == 0) {
		if (atomic_read(&plxhba->cmnds_in_flight) == 0
		    && (host->host_self_blocked == FALSE)) {
			ELX_DRVR_UNLOCK(phba, iflag);
			/* there are no other commands which will complete to flush
			   the queue, so retry */
			cmnd->result = ScsiResult(DID_BUS_BUSY, 0);
			done(cmnd);
			return (0);
		} else {
			ELX_DRVR_UNLOCK(phba, iflag);
			/* tell the midlayer we can't take commands right now */
			return (1);
		}
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	elx_cmd->scsi_bus = cmnd->device->channel;
	elx_cmd->scsi_target = cmnd->device->id;
	elx_cmd->scsi_lun = cmnd->device->lun;
#else
	elx_cmd->scsi_bus = cmnd->channel;
	elx_cmd->scsi_target = cmnd->target;
	elx_cmd->scsi_lun = cmnd->lun;
#endif

	if ((targetp = lpfc_find_target(phba, elx_cmd->scsi_target))) {
		if ((elx_cmd->scsi_lun >= targetp->max_lun) ||
		    ((targetp->pcontext == 0)
		     && !(targetp->targetFlags & FC_NPR_ACTIVE))) {
			elx_free_scsi_buf(elx_cmd);
			ELX_DRVR_UNLOCK(phba, iflag);
			/* error-out this command */
			cmnd->result = ScsiResult(DID_NO_CONNECT, 0);
			done(cmnd);
			return (0);
		}
		if ((cmnd->cmnd[0] == FCP_SCSI_INQUIRY) &&
		    (!lpfc_ValidLun(targetp, elx_cmd->scsi_lun))) {
			int retcod;
			uint8_t *buf;

			elx_free_scsi_buf(elx_cmd);
			if (clp[ELX_CFG_LUN_SKIP].a_current) {
				buf = (uint8_t *) cmnd->request_buffer;
				*buf = 0x3;
				retcod = DID_OK;
			} else {
				retcod = DID_NO_CONNECT;
			}

			ELX_DRVR_UNLOCK(phba, iflag);
			/* error-out this command */
			cmnd->result = ScsiResult(retcod, 0);
			done(cmnd);
			return (0);
		}
	}

	/* store our command structure for later */
	elx_cmd->pOSCmd = (void *)cmnd;
	cmnd->host_scribble = (unsigned char *)elx_cmd;
	/* Let the driver time I/Os out, NOT the upper layer */
	elx_cmd->scsitmo = elx_scsi_delete_timer(cmnd);
	elx_cmd->timeout = (uint32_t) (cmnd->timeout_per_command / HZ) +
	    phba->fcp_timeout_offset;
	/* save original done function in case we can not issue this
	   command */
	old_done = cmnd->scsi_done;

	cmnd->scsi_done = done;

	ret = elx_scsi_cmd_start(elx_cmd);
	if (ret) {

		elx_scsi_add_timer(cmnd, cmnd->timeout_per_command);

		elx_free_scsi_buf(elx_cmd);

		/* restore original done function in command */
		cmnd->scsi_done = old_done;
		if (ret < 0) {
			/* permanent failure -- error out command */
			cmnd->result = ScsiResult(DID_BAD_TARGET, 0);
			ELX_DRVR_UNLOCK(phba, iflag);
			done(cmnd);
			return (0);
		} else {
			if (atomic_read(&plxhba->cmnds_in_flight) == 0) {
				/* there are no other commands which will complete to
				   flush the queue, so retry */
				cmnd->result = ScsiResult(DID_BUS_BUSY, 0);
				ELX_DRVR_UNLOCK(phba, iflag);
				done(cmnd);
				return (0);
			} else {
				/* tell the midlayer we can't take commands right now */
				ELX_DRVR_UNLOCK(phba, iflag);
				return (1);
			}
		}
	}

	atomic_inc(&plxhba->cmnds_in_flight);
	ELX_DRVR_UNLOCK(phba, iflag);

	/* Return the error code. */
	return (0);
}

int
elx_abort_handler(Scsi_Cmnd * cmnd)
{
	elxHBA_t *phba;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	int rc;
	LINUX_HBA_t *plxhba;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	Scsi_Cmnd *prev_cmnd;

	/* release io_request_lock */
	spin_unlock_irq(&io_request_lock);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	phba = (elxHBA_t *) cmnd->device->host->hostdata[0];
#else
	phba = (elxHBA_t *) cmnd->host->hostdata[0];
#endif
	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;

	ELX_DRVR_LOCK(phba, iflag);

	elx_cmd = (ELX_SCSI_BUF_t *) cmnd->host_scribble;

	/* 
	   If the command is in retry cahin. delete the command from the
	   list.
	 */
	if (!elx_cmd) {

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		if (plxhba->cmnd_retry_list) {
			if (plxhba->cmnd_retry_list == cmnd) {
				plxhba->cmnd_retry_list = cmnd->reset_chain;

			} else {
				prev_cmnd = plxhba->cmnd_retry_list;

				while ((prev_cmnd->reset_chain != NULL) &&
				       (prev_cmnd->reset_chain != cmnd))
					prev_cmnd = prev_cmnd->reset_chain;

				if (prev_cmnd->reset_chain)
					prev_cmnd->reset_chain =
					    cmnd->reset_chain;
			}

		}
#endif
		return (0);
	}

	/* set command timeout to 60 seconds */
	elx_cmd->timeout = 60;

	/* SCSI layer issued abort device */
	elx_printf_log(phba->brd_no, &elx_msgBlk0712,	/* ptr to msg structure */
		       elx_mes0712,	/* ptr to msg */
		       elx_msgBlk0712.msgPreambleStr,	/* begin varargs */
		       elx_cmd->scsi_target, elx_cmd->scsi_lun);	/* end varargs */

	/* tell low layer to abort it */
	rc = elx_scsi_cmd_abort(phba, elx_cmd);

	/* SCSI layer issued abort device */
	elx_printf_log(phba->brd_no, &elx_msgBlk0749,	/* ptr to msg structure */
		       elx_mes0749,	/* ptr to msg */
		       elx_msgBlk0749.msgPreambleStr,	/* begin varargs */
		       elx_cmd->scsi_target, elx_cmd->scsi_lun, rc, elx_cmd->status, elx_cmd->result);	/* end varargs */

	ELX_DRVR_UNLOCK(phba, iflag);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* reacquire io_request_lock for midlayer */
	spin_lock_irq(&io_request_lock);
#endif

	return ((rc == 0) ? SUCCESS : FAILURE);

}

/* This function is now OS-specific and driver-specific */

int
elx_reset_lun_handler(Scsi_Cmnd * cmnd)
{
	elxHBA_t *phba;
	ELX_SCSI_BUF_t *elx_cmd;
	unsigned long iflag;
	int rc;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* release io_request_lock */
	spin_unlock_irq(&io_request_lock);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	phba = (elxHBA_t *) cmnd->device->host->hostdata[0];
#else
	phba = (elxHBA_t *) cmnd->host->hostdata[0];
#endif
	ELX_DRVR_LOCK(phba, iflag);
	elx_cmd = (ELX_SCSI_BUF_t *) cmnd->host_scribble;

	/* set command timeout to 60 seconds */
	elx_cmd->timeout = 60;

	/* SCSI layer issued abort device */
	elx_printf_log(phba->brd_no, &elx_msgBlk0713,	/* ptr to msg structure */
		       elx_mes0713,	/* ptr to msg */
		       elx_msgBlk0713.msgPreambleStr,	/* begin varargs */
		       elx_cmd->scsi_target, elx_cmd->scsi_lun);	/* end varargs */

	rc = elx_scsi_lun_reset(elx_cmd, phba, elx_cmd->scsi_bus,
				elx_cmd->scsi_target, elx_cmd->scsi_lun,
				ELX_EXTERNAL_RESET | ELX_ISSUE_ABORT_TSET);

	/* SCSI layer issued abort device */
	elx_printf_log(phba->brd_no, &elx_msgBlk0747,	/* ptr to msg structure */
		       elx_mes0747,	/* ptr to msg */
		       elx_msgBlk0747.msgPreambleStr,	/* begin varargs */
		       elx_cmd->scsi_target, elx_cmd->scsi_lun, rc, elx_cmd->status, elx_cmd->result);	/* end varargs */

	ELX_DRVR_UNLOCK(phba, iflag);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

	/* reacquire io_request_lock for midlayer */
	spin_lock_irq(&io_request_lock);
#endif

	return ((rc == 0) ? SUCCESS : FAILURE);

}

void
freeLun(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	ELXSCSITARGET_t *targetp;
	ELXSCSILUN_t *lunp, *curLun, *prevLun;
	MBUF_INFO_t *buf_info;
	MBUF_INFO_t bufinfo;

	lunp = elx_cmd->pLun;
	if (lunp == 0) {
		return;
	}

	targetp = lunp->pTarget;
	if (targetp == 0) {
		return;
	}

	elx_sched_remove_lun_from_ring(phba, lunp);

	prevLun = 0;
	curLun = (ELXSCSILUN_t *) targetp->lunlist.q_first;

	while ((curLun != 0) && (curLun != lunp)) {
		prevLun = curLun;
		curLun = prevLun->pnextLun;
	}

	if (curLun) {
		if (prevLun) {
			prevLun->pnextLun = curLun->pnextLun;
		}
		if (curLun == (ELXSCSILUN_t *) targetp->lunlist.q_first) {
			targetp->lunlist.q_first =
			    (ELX_SLINK_t *) curLun->pnextLun;
		}
		if (curLun == (ELXSCSILUN_t *) targetp->lunlist.q_last) {
			targetp->lunlist.q_last = (ELX_SLINK_t *) prevLun;
		}
		targetp->lunlist.q_cnt--;

		buf_info = &bufinfo;
		memset(buf_info, 0, sizeof (MBUF_INFO_t));
		buf_info->size = sizeof (ELXSCSILUN_t);
		buf_info->flags = ELX_MBUF_VIRT;
		buf_info->align = sizeof (void *);
		buf_info->virt = lunp;
		elx_free(phba, buf_info);
	}
}

void
elx_os_return_scsi_cmd(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	Scsi_Cmnd *lnx_cmnd = (Scsi_Cmnd *) elx_cmd->pOSCmd;
	elx_xlat_err_t resultdata;
	elx_xlat_err_t *presult;
	PARM_ERR *perr;
	uint32_t host_status;
	uint32_t scsi_status;

	FCP_CMND *fcp_cmnd;

	if (elx_cmd->status >= IOSTAT_CNT)
		elx_cmd->status = IOSTAT_DEFAULT;
	presult = &elx_iostat_tbl[elx_cmd->status];

	host_status = presult->host_status;
	scsi_status = 0;

	/* Now check if there are any special actions to perform */
	if (presult->action_flag) {

		/* FCP cmd <cmnd> failed <target>/<lun> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0729,	/* ptr to msg structure */
			       elx_mes0729,	/* ptr to msg */
			       elx_msgBlk0729.msgPreambleStr,	/* begin varargs */
			       lnx_cmnd->cmnd[0], elx_cmd->scsi_target, (uint32_t) elx_cmd->scsi_lun, elx_cmd->status, elx_cmd->result, elx_cmd->IOxri, elx_cmd->cur_iocbq.iocb.ulpIoTag);	/* end varargs */

		if (presult->action_flag & ELX_FCPRSP_ERROR) {
			presult = &resultdata;
			presult->host_status = DID_OK;
			presult->action_flag = 0;
			/* Call FCP RSP handler to determine result */
			scsi_status = elx_os_fcp_err_handle(elx_cmd, presult);
			if (scsi_status == ELX_CMD_BEING_RETRIED) {
				return;
			}
		} else if (presult->action_flag & ELX_REQUEUE) {
			/* set the Session Failure Recovery flag for this target */
			elx_cmd->pLun->pnode->nlp_rflag |= NLP_SFR_ACTIVE;
			/* pause scheduler queue for this device/LUN */
			elx_sched_pause_target(elx_cmd->pLun->pTarget);
			/* bypass OS error handling by deleting command's timer */
			elx_scsi_delete_timer((Scsi_Cmnd *) elx_cmd->pOSCmd);
			/* put this command back in the scheduler queue */
			elx_sched_queue_command(phba, elx_cmd);
			return;
		} else {
			elx_cmd->fcp_rsp->rspSnsLen = 0;
			if (presult->action_flag & ELX_IOERR_TABLE) {

				perr = (PARM_ERR *) & elx_cmd->result;
				if (perr->statLocalError >= IOERR_CNT)
					perr->statLocalError = IOERR_DEFAULT;
				presult = &elx_ioerr_tbl[perr->statLocalError];
			}
		}
		host_status = presult->host_status;

		if (presult->action_flag & ELX_STAT_ACTION) {
			perr = (PARM_ERR *) & elx_cmd->result;
			if (perr->statAction == RJT_RETRYABLE) {
				host_status = DID_BUS_BUSY;
			}
		}

		if (presult->action_flag & ELX_DELAY_IODONE) {
			lnx_cmnd->result = ScsiResult(host_status, scsi_status);
			elx_scsi_delay_iodone(phba, elx_cmd);
			return;
		}
	} else {
		elx_cmd->fcp_rsp->rspSnsLen = 0;
	}

	fcp_cmnd = elx_cmd->fcp_cmnd;
	if (fcp_cmnd->fcpCdb[0] == FCP_SCSI_INQUIRY) {
		unsigned char *buf;
		elxCfgParam_t *clp;

		buf = (unsigned char *)lnx_cmnd->request_buffer;
		if ((*buf == 0x7f) || ((*buf & 0xE0) == 0x20)) {
			freeLun(phba, elx_cmd);

			/* If a LINUX OS patch to support, LUN skipping / no LUN 0, is not present,
			 * this code will fake out the LINUX scsi layer to allow it to detect
			 * all LUNs if there are LUN holes on a device.
			 */
			clp = &phba->config[0];
			if (clp[ELX_CFG_LUN_SKIP].a_current) {
				/* Make lun unassigned and wrong type */
				*buf = 0x3;
			}
		}
	}

	lnx_cmnd->result = ScsiResult(host_status, scsi_status);
	elx_iodone(phba, elx_cmd);

	return;
}

void
elx_scsi_add_timer(Scsi_Cmnd * SCset, int timeout)
{

	if (SCset->eh_timeout.function != NULL) {
		del_timer(&SCset->eh_timeout);
	}

	if (SCset->eh_timeout.data != (unsigned long)SCset) {
		SCset->eh_timeout.data = (unsigned long)SCset;
		SCset->eh_timeout.function = (void (*)(unsigned long))elx_nodev;
	}
	SCset->eh_timeout.expires = jiffies + timeout;

	add_timer(&SCset->eh_timeout);
	return;
}

int
elx_scsi_delete_timer(Scsi_Cmnd * SCset)
{
	int rtn;

	rtn = SCset->eh_timeout.expires - jiffies;
	del_timer(&SCset->eh_timeout);
	SCset->eh_timeout.data = (unsigned long)NULL;
	SCset->eh_timeout.function = NULL;
	return (rtn);
}

uint32_t
elx_os_fcp_err_handle(ELX_SCSI_BUF_t * elx_cmd, elx_xlat_err_t * presult)
{
	Scsi_Cmnd *cmnd = (Scsi_Cmnd *) elx_cmd->pOSCmd;
	FCP_CMND *fcpcmd;
	FCP_RSP *fcprsp;
	elxHBA_t *phba;
	ELXSCSILUN_t *plun;
	IOCB_t *iocb;
	elxCfgParam_t *clp;
	int datadir;
	uint8_t iostat;
	uint32_t scsi_status;

	phba = elx_cmd->scsi_hba;
	plun = elx_cmd->pLun;
	clp = &phba->config[0];
	iocb = &elx_cmd->cur_iocbq.iocb;
	fcpcmd = elx_cmd->fcp_cmnd;
	fcprsp = elx_cmd->fcp_rsp;
	iostat = (uint8_t) (elx_cmd->status);

	/* Make sure presult->host_status is identically DID_OK and scsi_status
	 * is identically 0.  The driver alters this value later on an as-needed
	 * basis.
	 */
	presult->host_status = DID_OK;
	scsi_status = 0;

	/*
	 *  If this is a task management command, there is no
	 *  scsi packet associated with it.  Return here.
	 */
	if ((cmnd == NULL) || (fcpcmd->fcpCntl2)) {
		return (scsi_status);
	}

	/* FCP cmd failed: RSP */
	elx_printf_log(phba->brd_no, &elx_msgBlk0730,	/* ptr to msg structure */
		       elx_mes0730,	/* ptr to msg */
		       elx_msgBlk0730.msgPreambleStr,	/* begin varargs */
		       fcprsp->rspStatus2, fcprsp->rspStatus3, SWAP_DATA(fcprsp->rspResId), SWAP_DATA(fcprsp->rspSnsLen), SWAP_DATA(fcprsp->rspRspLen), fcprsp->rspInfo3);	/* end varargs */

	if (fcprsp->rspStatus2 & RSP_LEN_VALID) {
		uint32_t rsplen;

		rsplen = SWAP_DATA(fcprsp->rspRspLen);
		if (rsplen > 8) {
			presult->host_status = DID_ERROR;
			scsi_status = (uint32_t) (fcprsp->rspStatus3);
			fcprsp->rspSnsLen = 0;
			return (scsi_status);
		}
		if (fcprsp->rspInfo3 != RSP_NO_FAILURE) {
			presult->host_status = DID_ERROR;
			scsi_status = (uint32_t) (fcprsp->rspStatus3);

			fcprsp->rspSnsLen = 0;
			return (scsi_status);
		}
	}

	/*
	 * In the Tape Env., there is an early WARNNING  right before EOM without 
	 * data xfer error. We should set b_resid to be 0 before we check all other 
	 * cases.
	 */

	cmnd->resid = 0;

	if (fcprsp->rspStatus2 & (RESID_UNDER | RESID_OVER)) {
		if (fcprsp->rspStatus2 & RESID_UNDER) {
			/* 
			 * This is not an error! Just setup the resid field. 
			 */
			cmnd->resid = SWAP_DATA(fcprsp->rspResId);

			/* FCP Read Underrun, expected <len>, residual <resid> */
			elx_printf_log(phba->brd_no, &elx_msgBlk0716,	/* ptr to msg structure */
				       elx_mes0716,	/* ptr to msg */
				       elx_msgBlk0716.msgPreambleStr,	/* begin varargs */
				       SWAP_DATA(fcpcmd->fcpDl), cmnd->resid, iocb->un.fcpi.fcpi_parm, cmnd->cmnd[0], cmnd->underflow);	/* end varargs */
		}
	} else {
		datadir = elx_cmd->OS_io_info.datadir;

		if ((datadir == SCSI_DATA_READ) && iocb->un.fcpi.fcpi_parm) {
			/* 
			 * This is ALWAYS a readcheck error!! 
			 * Give Check Condition priority over Read Check 
			 */

			if (fcprsp->rspStatus3 != SCSI_STAT_BUSY) {
				if (fcprsp->rspStatus3 != SCSI_STAT_CHECK_COND) {
					/* FCP Read Check Error */
					elx_printf_log(phba->brd_no, &elx_msgBlk0734,	/* ptr to msg structure */
						       elx_mes0734,	/* ptr to msg */
						       elx_msgBlk0734.msgPreambleStr,	/* begin varargs */
						       SWAP_DATA(fcpcmd->fcpDl), SWAP_DATA(fcprsp->rspResId), iocb->un.fcpi.fcpi_parm, cmnd->cmnd[0]);	/* end varargs */

					presult->host_status = DID_ERROR;
					cmnd->resid = cmnd->request_bufflen;
					scsi_status =
					    (uint32_t) (fcprsp->rspStatus3);
					fcprsp->rspSnsLen = 0;
					return (scsi_status);
				}

				/* FCP Read Check Error with Check Condition */
				elx_printf_log(phba->brd_no, &elx_msgBlk0735,	/* ptr to msg structure */
					       elx_mes0735,	/* ptr to msg */
					       elx_msgBlk0735.msgPreambleStr,	/* begin varargs */
					       SWAP_DATA(fcpcmd->fcpDl), SWAP_DATA(fcprsp->rspResId), iocb->un.fcpi.fcpi_parm, cmnd->cmnd[0]);	/* end varargs */
			}
		}
	}

	if ((fcprsp->rspStatus2 & SNS_LEN_VALID) && (fcprsp->rspSnsLen != 0)) {
		uint32_t snsLen, rspLen;

		rspLen = SWAP_DATA(fcprsp->rspRspLen);
		snsLen = SWAP_DATA(fcprsp->rspSnsLen);
		if (snsLen > MAX_ELX_SNS) {
			snsLen = MAX_ELX_SNS;
		}
		memcpy(plun->sense, ((uint8_t *) & fcprsp->rspInfo0) + rspLen,
		       snsLen);
		plun->sense_length = snsLen;

		/* then we return this sense info in the sense buffer for this cmd */
		if (snsLen > SCSI_SENSE_BUFFERSIZE) {
			snsLen = SCSI_SENSE_BUFFERSIZE;
		}
		memcpy(cmnd->sense_buffer, plun->sense, snsLen);
		plun->sense_valid = 0;
	} else {
		fcprsp->rspSnsLen = 0;
	}

	if (fcprsp->rspStatus2 & RESID_UNDER) {
		uint32_t len, resid;

		switch (cmnd->cmnd[0]) {
		case TEST_UNIT_READY:
		case REQUEST_SENSE:
		case INQUIRY:
		case RECEIVE_DIAGNOSTIC:
		case READ_CAPACITY:
		case FCP_SCSI_READ_DEFECT_LIST:
		case MDACIOCTL_DIRECT_CMD:
			/* If sense indicated 29,00, then we want to retry the cmd */
			if ((fcprsp->rspStatus3 == SCSI_STAT_CHECK_COND) &&
			    (fcprsp->rspStatus2 & SNS_LEN_VALID) &&
			    (fcprsp->rspSnsLen != 0)) {
				uint32_t i;
				uint32_t cc;
				uint32_t *lp;

				i = SWAP_DATA(fcprsp->rspRspLen);
				lp = (uint32_t
				      *) (((uint8_t *) & fcprsp->rspInfo0) + i);
				cc = (SWAP_DATA
				      ((lp[3]) & SWAP_DATA(0xFF000000)));
				if (cc == 0x29000000)
					break;
			}
			/* No error */
			fcprsp->rspSnsLen = 0;
			return (scsi_status);
		default:
			len = cmnd->request_bufflen;
			resid = SWAP_DATA(fcprsp->rspResId);
			if (!(fcprsp->rspStatus2 & SNS_LEN_VALID) &&
			    (len - resid < cmnd->underflow)) {

				/* FCP command <cmd> residual underrun converted to error */
				elx_printf_log(phba->brd_no, &elx_msgBlk0717,	/* ptr to msg structure */
					       elx_mes0717,	/* ptr to msg */
					       elx_msgBlk0717.msgPreambleStr,	/* begin varargs */
					       cmnd->cmnd[0], len, resid, cmnd->underflow);	/* end varargs */

				presult->host_status = DID_ERROR;
				scsi_status = (uint32_t) (fcprsp->rspStatus3);
				fcprsp->rspSnsLen = 0;
				return (scsi_status);
			}
		}
	}

	scsi_status = (uint32_t) (fcprsp->rspStatus3);

	switch (scsi_status) {
	case SCSI_STAT_QUE_FULL:
		if (clp[ELX_CFG_DQFULL_THROTTLE_UP_TIME].a_current) {
			elx_scsi_lower_lun_qthrottle(phba, elx_cmd);
		}
		break;
	case SCSI_STAT_BUSY:
		presult->host_status = DID_BUS_BUSY;
		scsi_status = (uint32_t) (fcprsp->rspStatus3);
		presult->action_flag |= ELX_DELAY_IODONE;
		break;
	case SCSI_STAT_CHECK_COND:
		{
			uint32_t i;
			uint32_t cc;
			uint32_t *lp;

			i = SWAP_DATA(fcprsp->rspRspLen);
			lp = (uint32_t *) (((uint8_t *) & fcprsp->rspInfo0) +
					   i);
			cc = (SWAP_DATA((lp[3]) & SWAP_DATA(0xFF000000)));

			/* <ASC ASCQ> Check condition received */
			elx_printf_log(phba->brd_no, &elx_msgBlk0737,	/* ptr to msg structure */
				       elx_mes0737,	/* ptr to msg */
				       elx_msgBlk0737.msgPreambleStr,	/* begin varargs */
				       cc, clp[ELX_CFG_CHK_COND_ERR].a_current, clp[ELX_CFG_DELAY_RSP_ERR].a_current, *lp);	/* end varargs */

			switch (cc) {
			case 0x29000000:
				/* Retry FCP command due to 29,00 check condition */
				elx_printf_log(phba->brd_no, &elx_msgBlk0732,	/* ptr to msg structure */
					       elx_mes0732,	/* ptr to msg */
					       elx_msgBlk0732.msgPreambleStr,	/* begin varargs */
					       *lp, *(lp + 1), *(lp + 2), *(lp + 3));	/* end varargs */

				/* since we retry the cmd here, any sense should be cleared */
				memset(cmnd->sense_buffer, 0,
				       SCSI_SENSE_BUFFERSIZE);
				elx_sched_queue_command(phba, elx_cmd);
				scsi_status = ELX_CMD_BEING_RETRIED;
				break;
			case 0x0:	/* ASC and ASCQ = 0 */
				break;
			case 0x44000000:	/* Internal Target Failure */
			case 0x25000000:	/* Login Unit not supported */
			case 0x20000000:	/* Invalid cmd operation code */
				/* These will be considered an error if the command is not a TUR
				 * and CHK_COND_ERR is not set */
				if ((fcpcmd->fcpCdb[0] !=
				     FCP_SCSI_TEST_UNIT_READY)
				    && (clp[ELX_CFG_CHK_COND_ERR].a_current)) {
					presult->host_status = DID_ERROR;
					scsi_status = 0;
				}
			}
		}
		break;

	default:
		break;
	}
	return (scsi_status);
}

void
elx_iodone(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	Scsi_Cmnd *lnx_cmnd = (Scsi_Cmnd *) elx_cmd->pOSCmd;
	LINUX_HBA_t *plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	ELXSCSILUN_t *lun_device;
	uint32_t *lp;
	int datadir;
	unsigned long flag;

	if ((lnx_cmnd->result) || (elx_cmd->fcp_rsp->rspSnsLen)) {
		lp = (uint32_t *) lnx_cmnd->sense_buffer;
		/* Iodone <target>/<lun> error <result> SNS <lp> <lp3> */
		elx_printf_log(phba->brd_no, &elx_msgBlk0710,	/* ptr to msg structure */
			       elx_mes0710,	/* ptr to msg */
			       elx_msgBlk0710.msgPreambleStr,	/* begin varargs */
			       elx_cmd->scsi_target, (uint32_t) elx_cmd->scsi_lun, lnx_cmnd->result, *lp, *(lp + 3), lnx_cmnd->retries, lnx_cmnd->resid);	/* end varargs */
		lun_device = elx_find_lun_device(elx_cmd);
		if (lnx_cmnd->result && lun_device &&
		    (lun_device->pTarget->targetFlags & FC_NPR_ACTIVE)) {
			lnx_cmnd->result =
			    ScsiResult(DID_BUS_BUSY, SCSI_STAT_BUSY);
		}
	}

	datadir = elx_cmd->OS_io_info.datadir;
	if (lnx_cmnd->use_sg) {
		pci_unmap_sg(plxhba->pcidev, lnx_cmnd->request_buffer,
			     lnx_cmnd->use_sg, scsi_to_pci_dma_dir(datadir));
	} else if ((lnx_cmnd->request_bufflen)
		   && (elx_cmd->OS_io_info.nonsg_phys)) {
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,12)
		pci_unmap_single(plxhba->pcidev,
				 (uint64_t) ((unsigned long)(elx_cmd->
							     OS_io_info.
							     nonsg_phys)),
				 lnx_cmnd->request_bufflen,
				 scsi_to_pci_dma_dir(datadir));
#else
		pci_unmap_page(plxhba->pcidev,
			       (uint64_t) ((unsigned long)(elx_cmd->OS_io_info.
							   nonsg_phys)),
			       lnx_cmnd->request_bufflen,
			       scsi_to_pci_dma_dir(datadir));
#endif
	}
	elx_free_scsi_buf(elx_cmd);

	plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	/* If Link Down Timer or Nodev Timer is running for this I/O
	 * put it on a different queue.
	 */
	spin_lock_irqsave(&plxhba->iodonelock.elx_lock, flag);
	/* Queue iodone to be called at end of ISR */
	if (plxhba->iodone.q_first) {
		((Scsi_Cmnd *) (plxhba->iodone.q_last))->host_scribble =
		    (void *)lnx_cmnd;
	} else {
		plxhba->iodone.q_first = (ELX_SLINK_t *) lnx_cmnd;
	}
	plxhba->iodone.q_last = (ELX_SLINK_t *) lnx_cmnd;
	plxhba->iodone.q_cnt++;
	lnx_cmnd->host_scribble = 0;
	spin_unlock_irqrestore(&plxhba->iodonelock.elx_lock, flag);

	return;
}

int
elx_scsi_delay_iodone(elxHBA_t * phba, ELX_SCSI_BUF_t * elx_cmd)
{
	elxCfgParam_t *clp;
	uint32_t tmout;

	clp = &phba->config[0];

	if (clp[ELX_CFG_NO_DEVICE_DELAY].a_current) {
		/* Set a timer so iodone can be called
		 * for buffer upon expiration.
		 */
		tmout = clp[ELX_CFG_NO_DEVICE_DELAY].a_current;

		/* If able to clock set this request, then just return here */
		if (elx_clk_set
		    (phba, tmout,
		     (void (*)(elxHBA_t *, void *, void *))elx_iodone,
		     (void *)elx_cmd, 0) != 0) {
			return (1);
		}
	}
	elx_iodone(phba, elx_cmd);
	return (0);
}

void
elx_block_requests(elxHBA_t * phba)
{
	LINUX_HBA_t *plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
	plxhba->in_retry = 1;
	scsi_block_requests(plxhba->host);

}

void
elx_unblock_requests(elxHBA_t * phba)
{
	LINUX_HBA_t *plxhba = (LINUX_HBA_t *) phba->pHbaOSEnv;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	Scsi_Cmnd *cmnd, *next_cmnd;
#endif
	unsigned long iflag;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
	cmnd = plxhba->cmnd_retry_list;
	plxhba->in_retry = 0;
	while (cmnd) {
		next_cmnd = cmnd->reset_chain;
		cmnd->reset_chain = 0;
		cmnd->result = ScsiResult(DID_RESET, 0);

		ELX_DRVR_UNLOCK(phba, iflag);
		cmnd->scsi_done(cmnd);
		ELX_DRVR_LOCK(phba, iflag);

		cmnd = next_cmnd;
	}
	plxhba->cmnd_retry_list = 0;
#endif
	iflag = phba->iflag;

	ELX_DRVR_UNLOCK(phba, iflag);
	scsi_unblock_requests(plxhba->host);
	ELX_DRVR_LOCK(phba, iflag);
	phba->iflag = iflag;
}

#include <linux/wait.h>

void
elx_sli_wake_iocb_wait(elxHBA_t * phba,
		       ELX_IOCBQ_t * queue1, ELX_IOCBQ_t * queue2)
{
	wait_queue_head_t *pdone_q;

	queue1->iocb_flag |= ELX_IO_WAIT;
	if (queue1->context2 && queue2)
		memcpy(queue1->context2, queue2, sizeof (ELX_IOCBQ_t));
	pdone_q = (wait_queue_head_t *) queue1->context1;
	if (pdone_q) {
		wake_up_interruptible(pdone_q);
	}
	/* if pdone_q/context3 was NULL, it means the waiter already gave
	   up and returned, so we don't have to do anything */

	return;
}

int
elx_sli_issue_iocb_wait(elxHBA_t * phba,
			ELX_SLI_RING_t * pring,
			ELX_IOCBQ_t * piocb,
			uint32_t flag,
			ELX_IOCBQ_t * prspiocbq, uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD(done_q);
	DECLARE_WAITQUEUE(wq_entry, current);
	uint32_t timeleft = 0;
	int retval;
	unsigned long iflag = phba->iflag;

	/* The caller must leave context1 empty for the driver. */
	if (piocb->context1 != 0) {
		return (IOCB_ERROR);
	}
	/* If the caller has provided a response iocbq buffer, then context2 
	 * is NULL or its an error.
	 */
	if (prspiocbq) {
		if (piocb->context2) {
			return (IOCB_ERROR);
		}
		piocb->context2 = prspiocbq;
	}

	/* setup wake call as IOCB callback */
	piocb->iocb_cmpl = elx_sli_wake_iocb_wait;
	/* setup context field to pass wait_queue pointer to wake function  */
	piocb->context1 = &done_q;

	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&done_q, &wq_entry);

	/* now issue the command */
	retval = elx_sli_issue_iocb(phba, pring, piocb, flag);
	if ((retval == IOCB_SUCCESS) ||
	    ((!(flag & SLI_IOCB_RET_IOCB)) && retval == IOCB_BUSY)) {
		ELX_DRVR_UNLOCK(phba, iflag);
		timeleft = schedule_timeout(timeout * HZ);
		ELX_DRVR_LOCK(phba, iflag);
		piocb->context1 = 0;	/* prevents completion fcn from signalling */
		piocb->iocb_cmpl = 0;
		if (piocb->context2 == prspiocbq)
			piocb->context2 = 0;

		/* if schedule_timeout returns 0, we timed out and were not woken up 
		 * if ELX_IO_WAIT is not set, we go woken up by a signal.
		 */
		if ((timeleft == 0) || !(piocb->iocb_flag & ELX_IO_WAIT)) {
			if (timeleft == 0)
				retval = IOCB_TIMEDOUT;

			if (piocb->q_f && piocb->q_b)
				elx_deque(piocb);
		}
	}
	remove_wait_queue(&done_q, &wq_entry);
	set_current_state(TASK_RUNNING);
	return retval;
}

void
elx_sli_wake_iocb_high_priority(elxHBA_t * phba,
				ELX_IOCBQ_t * queue1, ELX_IOCBQ_t * queue2)
{
	if (queue1->context2 && queue2)
		memcpy(queue1->context2, queue2, sizeof (ELX_IOCBQ_t));

	/* The waiter is looking for a non-zero context3 value 
	   as a signal to wake up */
	queue1->context3 = (void *)1;

	return;
}

int
elx_sli_issue_iocb_wait_high_priority(elxHBA_t * phba,
				      ELX_SLI_RING_t * pring,
				      ELX_IOCBQ_t * piocb,
				      uint32_t flag,
				      ELX_IOCBQ_t * prspiocbq, uint32_t timeout)
{
	int retval, j;
	unsigned long drvr_flag = phba->iflag;
	unsigned long iflag;

	/* The caller must left context1 empty.  */
	if (piocb->context1 != 0) {
		return (IOCB_ERROR);
	}
	/* If the caller has provided a response iocbq buffer, context2 is NULL
	 * or its an error.
	 */
	if (prspiocbq) {
		if (piocb->context2) {
			return (IOCB_ERROR);
		}
		piocb->context2 = prspiocbq;
	}

	/* setup wake call as IOCB callback */
	piocb->iocb_cmpl = elx_sli_wake_iocb_high_priority;

	/* now issue the command */
	retval =
	    elx_sli_issue_iocb(phba, pring, piocb,
			       flag | SLI_IOCB_HIGH_PRIORITY);

	/* 20 * 50ms is 1sec */
	for (j = 0; j < 20; j++) {
		ELX_DRVR_UNLOCK(phba, drvr_flag);
		mdelay(100);
		ELX_DRVR_LOCK(phba, drvr_flag);

		elx_hipri_lock(phba, &iflag);
		if (piocb->context3) {
			elx_hipri_unlock(phba, &iflag);
			break;
		}
		elx_hipri_unlock(phba, &iflag);
	}

	retval = IOCB_SUCCESS;

	return retval;
}

void
elx_sli_wake_mbox_wait(elxHBA_t * phba, ELX_MBOXQ_t * pmboxq)
{
	wait_queue_head_t *pdone_q;

	pdone_q = (wait_queue_head_t *) pmboxq->context1;
	if (pdone_q)
		wake_up_interruptible(pdone_q);
	/* if pdone_q/context3 was NULL, it means the waiter already gave
	   up and returned, so we don't have to do anything */

	return;
}

int
elx_sli_issue_mbox_wait(elxHBA_t * phba, ELX_MBOXQ_t * pmboxq, uint32_t timeout)
{
	DECLARE_WAIT_QUEUE_HEAD(done_q);
	DECLARE_WAITQUEUE(wq_entry, current);
	uint32_t timeleft = 0;
	int retval;
	unsigned long iflag = phba->iflag;

	/* The caller must leave context1 empty. */
	if (pmboxq->context1 != 0) {
		return (MBX_NOT_FINISHED);
	}

	/* setup wake call as IOCB callback */
	pmboxq->mbox_cmpl = elx_sli_wake_mbox_wait;
	/* setup context field to pass wait_queue pointer to wake function  */
	pmboxq->context1 = &done_q;

	/* start to sleep before we wait, to avoid races */
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&done_q, &wq_entry);

	/* now issue the command */
	retval = elx_sli_issue_mbox(phba, pmboxq, MBX_NOWAIT);
	if (retval == MBX_BUSY || retval == MBX_SUCCESS) {
		ELX_DRVR_UNLOCK(phba, iflag);
		timeleft = schedule_timeout(timeout * HZ);
		ELX_DRVR_LOCK(phba, iflag);
		pmboxq->context1 = 0;
	}

	/* if schedule_timeout returns 0, we timed out and were not woken up */

	else if (timeleft == 0) {
		retval = MBX_TIMEOUT;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&done_q, &wq_entry);
	return retval;
}
