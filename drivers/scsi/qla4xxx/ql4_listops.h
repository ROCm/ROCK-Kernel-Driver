/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.4.x
 * Copyright (C) 2004 Qlogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/

/* Management functions for various lists */

/*************************************/

static inline void
__add_to_pending_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_add_tail(&srb->list_entry, &ha->pending_srb_q);
	srb->state = SRB_PENDING_STATE;
	ha->pending_srb_q_count++;
}

static inline void
__add_to_pending_srb_q_head(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_add(&srb->list_entry, &ha->pending_srb_q);
	srb->state = SRB_PENDING_STATE;
	ha->pending_srb_q_count++;
}

static inline void
__del_from_pending_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_del_init(&srb->list_entry);
	srb->state = SRB_NO_QUEUE_STATE;
	ha->pending_srb_q_count--;
}

/*************************************/

static inline void
__add_to_retry_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_add_tail(&srb->list_entry, &ha->retry_srb_q);
	srb->state = SRB_RETRY_STATE;
	ha->retry_srb_q_count++;
	srb->ha = ha;
}

static inline void
__del_from_retry_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_del_init(&srb->list_entry);
	srb->state = SRB_NO_QUEUE_STATE;
	ha->retry_srb_q_count--;
}

/*************************************/

static inline void
__add_to_done_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_add_tail(&srb->list_entry, &ha->done_srb_q);
	srb->state = SRB_DONE_STATE;
	ha->done_srb_q_count++;
	srb->ha = ha;
}

static inline void
__del_from_done_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_del_init(&srb->list_entry);
	srb->state = SRB_NO_QUEUE_STATE;
	ha->done_srb_q_count--;
}

static inline srb_t *__del_from_done_srb_q_head(scsi_qla_host_t *ha)
{
	struct list_head *ptr;
	srb_t *srb = NULL;

	if (!list_empty(&ha->done_srb_q)) {
		/* Remove list entry from head of queue */
		ptr = ha->done_srb_q.next;
		list_del_init(ptr);

		/* Return pointer to srb structure */
		srb = list_entry(ptr, srb_t, list_entry);
		srb->state = SRB_NO_QUEUE_STATE;
		ha->done_srb_q_count--;
	}
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));

	return(srb);
}

/*************************************/

static inline void
__add_to_free_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));

	//memset(srb, 0, sizeof(srb_t));
	list_add_tail(&srb->list_entry, &ha->free_srb_q);
	ha->free_srb_q_count++;
	srb->state = SRB_FREE_STATE;
}

static inline void __del_from_free_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{

	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));
	list_del_init(&srb->list_entry);
	srb->state = SRB_NO_QUEUE_STATE;
	ha->free_srb_q_count--;
}

static inline srb_t *__del_from_free_srb_q_head(scsi_qla_host_t *ha)
{
	struct list_head *ptr;
	srb_t *srb = NULL;

	if (!list_empty(&ha->free_srb_q)) {
		/* Remove list entry from head of queue */
		ptr = ha->free_srb_q.next;
		list_del_init(ptr);

		/* Return pointer to srb structure */
		srb = list_entry(ptr, srb_t, list_entry);
		memset(srb, 0, sizeof(*srb));
		srb->state = SRB_NO_QUEUE_STATE;
		ha->free_srb_q_count--;
	}
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance, srb));

	return(srb);
}

/*************************************/

static inline void
__add_to_suspended_lun_q(scsi_qla_host_t *ha, os_lun_t *lun)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, lun = %d\n",
			      ha->host_no, __func__, ha->instance, lun->lun));
	list_add_tail(&lun->list_entry, &ha->suspended_lun_q);
	ha->suspended_lun_q_count++;
}

static inline void
__del_from_suspended_lun_q(scsi_qla_host_t *ha, os_lun_t *lun)
{
	QL4PRINT(QLP8, printk("scsi%d: %s: ha %d, lun = %d\n",
			      ha->host_no, __func__, ha->instance, lun->lun));
	list_del_init(&lun->list_entry);
	ha->suspended_lun_q_count--;
}


/******************************************************************************/
/******************************************************************************/

static inline void
add_to_pending_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_pending_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
add_to_pending_srb_q_head(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_pending_srb_q_head(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
del_from_pending_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__del_from_pending_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*************************************/

static inline void
add_to_retry_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_retry_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
del_from_retry_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__del_from_retry_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

/*************************************/

static inline void
add_to_done_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	// spin_lock_irqsave(&ha->adapter_lock, flags);
	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_done_srb_q(ha ,srb);
	// spin_unlock_irqrestore(&ha->adapter_lock, flags);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
del_from_done_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	// spin_lock_irqsave(&ha->adapter_lock, flags);
	__del_from_done_srb_q(ha ,srb);
	// spin_unlock_irqrestore(&ha->adapter_lock, flags);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline srb_t *
del_from_done_srb_q_head(scsi_qla_host_t *ha)
{
	unsigned long flags;
	srb_t *srb;

	// spin_lock_irqsave(&ha->adapter_lock, flags);
	spin_lock_irqsave(&ha->list_lock, flags);
	srb = __del_from_done_srb_q_head(ha);
	spin_unlock_irqrestore(&ha->list_lock, flags);
	// spin_unlock_irqrestore(&ha->adapter_lock, flags);
	return(srb);
}

/*************************************/

static inline void
add_to_free_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	memset(srb, 0, sizeof(*srb));
	__add_to_free_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline srb_t *
del_from_free_srb_q_head(scsi_qla_host_t *ha)
{
	unsigned long flags;
	srb_t *srb;

	spin_lock_irqsave(&ha->list_lock, flags);
	srb = __del_from_free_srb_q_head(ha);
	spin_unlock_irqrestore(&ha->list_lock, flags);

	if (srb) {
		#ifdef DEBUG
		if (atomic_read(&srb->ref_count) != 0) {
			QL4PRINT(QLP2, printk("scsi%d: %s: WARNING: "
					      "ref_count not zero.\n",
					      ha->host_no, __func__));
		}
		#endif

		atomic_set(&srb->ref_count, 1);
	}
	return(srb);
}

/*************************************/


#ifdef CONFIG_SCSI_QLA4XXX_FAILOVER
/*
 * Failover Stuff.
 */
static inline void
__add_to_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	/*
	if( sp->state != SRB_NO_QUEUE_STATE &&
		sp->state != SRB_ACTIVE_STATE)
		BUG();
	*/

	list_add_tail(&sp->list_entry,&ha->failover_queue);
	ha->failover_cnt++;
	sp->state = SRB_FAILOVER_STATE;
	sp->ha = ha;
}

static inline void add_to_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);

	__add_to_failover_queue(ha,sp);

	spin_unlock_irqrestore(&ha->list_lock, flags);
}
static inline void __del_from_failover_queue(struct scsi_qla_host * ha, srb_t *
					     sp)
{
	ha->failover_cnt--;
	list_del_init(&sp->list_entry);
	sp->state = SRB_NO_QUEUE_STATE;
}

static inline void del_from_failover_queue(struct scsi_qla_host * ha, srb_t * sp)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);

	__del_from_failover_queue(ha,sp);

	spin_unlock_irqrestore(&ha->list_lock, flags);
}
#endif


