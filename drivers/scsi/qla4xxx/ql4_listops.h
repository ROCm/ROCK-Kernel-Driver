/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
 *
 ******************************************************************************/

/* Management functions for various lists */

/*************************************/

static inline void
sp_put( scsi_qla_host_t *ha, srb_t *sp)
{
	if (atomic_read(&sp->ref_count) == 0) {
		QL4PRINT(QLP2, printk("scsi%d: %s: sp->ref_count zero\n",
				      ha->host_no, __func__));
		DEBUG2(BUG());
		return;
	}

	if (!atomic_dec_and_test(&sp->ref_count)) {
		return;
	}
	
	qla4xxx_complete_request(ha, sp);
}

static inline void
sp_get( scsi_qla_host_t *ha, srb_t *sp)
{
	atomic_inc(&sp->ref_count);

	if (atomic_read(&sp->ref_count) > 2) {
		QL4PRINT(QLP2, printk("scsi%d: %s: sp->ref_count greater than 2\n",
				      ha->host_no, __func__));
		DEBUG2(BUG());
		return;
	}
}

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
	DEBUG(printk("scsi%d: %s: instance %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance,
			      srb ));

	atomic_set(&srb->ref_count, 0);
	list_add_tail(&srb->list_entry, &ha->free_srb_q);
	ha->free_srb_q_count++;
	srb->state = SRB_FREE_STATE;
}

static inline void __del_from_free_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{

	DEBUG(printk("scsi%d: %s: instance %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance,
			      srb ));
	list_del_init(&srb->list_entry);
	atomic_set(&srb->ref_count, 1);
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
		atomic_set(&srb->ref_count, 1);
		srb->state = SRB_NO_QUEUE_STATE;
		ha->free_srb_q_count--;
	}
	DEBUG(printk("scsi%d: %s: instance %d, srb = %p\n",
			      ha->host_no, __func__, ha->instance,
			      srb ));

	return(srb);
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

	spin_lock_irqsave(&ha->list_lock, flags);
	__add_to_done_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline void
del_from_done_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
	__del_from_done_srb_q(ha ,srb);
	spin_unlock_irqrestore(&ha->list_lock, flags);
}

static inline srb_t *
del_from_done_srb_q_head(scsi_qla_host_t *ha)
{
	unsigned long flags;
	srb_t *srb;

	spin_lock_irqsave(&ha->list_lock, flags);
	srb = __del_from_done_srb_q_head(ha);
	spin_unlock_irqrestore(&ha->list_lock, flags);
	return(srb);
}

/*************************************/

static inline void
add_to_free_srb_q(scsi_qla_host_t *ha, srb_t *srb)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->list_lock, flags);
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

	return(srb);
}

/*************************************/
