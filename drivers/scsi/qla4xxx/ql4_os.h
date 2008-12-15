/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

/*
 * This file encapsulates RHEL5 Specific Code
 */

#ifndef __QLA4x_OS_H
#define	__QLA4x_OS_H

/* Common across all O.S platforms */
#define IOCB_CMD_TIMEOUT	30
#define RELOGIN_TOV		18
#define RECOVERY_TIMEOUT	20 /* ddb state MISSING -> DEAD */

#define QL_IOCB_CMD_TIMEOUT(cmd)

#define QL_SET_DDB_OFFLINE(ha, ddb_entry)

#define QL_SESS_RECOVERY_TO(ddb_entry) ddb_entry->ha->port_down_retry_count

#define QL_DPC_OFFLINE_SET(ha) 0

#define QL_ISCSI_CONN_TO_SESS(conn) iscsi_dev_to_session(conn->dev.parent)

#define QL_ISCSI_SDEV_TO_SESS(sdev) starget_to_session(sdev->sdev_target)

#define QL_ISCSI_ADD_SESS(ddb_entry) \
		iscsi_add_session(ddb_entry->sess, ddb_entry->os_target_id)

#define QL_ISCSI_REGISTER_HOST(host, trans) 0
#define QL_ISCSI_UNREGISTER_HOST(host, trans)

#define QL_ISCSI_SESSION_ID(ddb_entry) ddb_entry->sess->sid
#define QL_ISCSI_IF_DESTROY_SESSION_DONE(ddb_entry)
#define QL_ISCSI_DESTROY_CONN(ddb_entry)
#define QL_ISCSI_CREATE_CONN(ddb_entry) \
		iscsi_create_conn(ddb_entry->sess, 0, 0)
#define QL_ISCSI_CREATE_SESS_DONE(ddb_entry) \
		iscsi_unblock_session(ddb_entry->sess)
#define QL_ISCSI_ALLOC_SESSION(ha, trans) \
		iscsi_alloc_session(ha->host, trans, sizeof(struct ddb_entry))
#define QL_SET_SDEV_HOSTDATA(sdev, sess)

#define QL_DDB_STATE_REMOVED(ddb_entry) 0

#define QL_MISC_INIT 0
#define QL_MISC_EXIT

#define qla4xxx_check_dev_offline(ha)
#define qla4xxx_proc_info NULL
#define qla4xxx_target_destroy NULL

#define QL_SET_SCSI_RESID(cmd, residual) scsi_set_resid(cmd, residual)
#define QL_SCSI_BUFFLEN(cmd) scsi_bufflen(cmd)

#define QL_DPC_DATA_TO_HA(work) \
	container_of((struct work_struct *)work, struct scsi_qla_host, dpc_work)

#define QL_INIT_WORK(ha, dpc_func) INIT_WORK(&ha->dpc_work, dpc_func)

#define QL_REQ_IRQ_FLAGS (IRQF_DISABLED | IRQF_SHARED)

#define QL_DECLARE_INTR_HANDLER(intr_func, irq, dev_id, regs) \
		irqreturn_t intr_func(int irq, void *dev_id)

#define QL_DECLARE_DPC(dpc_func, data) \
		void dpc_func(struct work_struct *data)

#define QL_INIT_SESSION_DATASIZE(sessiondata_size)

#define QL_INIT_HOST_TEMPLATE(host_template)

QL_DECLARE_INTR_HANDLER(qla4xxx_intr_handler, irq, dev_id, regs);

static inline struct kmem_cache *ql_kmem_cache_create(void)
{
	return (kmem_cache_create("qla4xxx_srbs", sizeof(struct srb), 0,
			SLAB_HWCACHE_ALIGN, NULL));
}

static inline void qla4xxx_scan_target(struct ddb_entry * ddb_entry)
{
	scsi_scan_target(&ddb_entry->sess->dev, 0,
		ddb_entry->sess->target_id, SCAN_WILD_CARD, 0);
}

static void ql4_get_aen_log(struct scsi_qla_host *ha, struct ql4_aen_log *aenl)
{
	if (aenl) {
		memcpy(aenl, &ha->aen_log, sizeof (ha->aen_log));
		ha->aen_log.count = 0;
	}
}

static inline int qla4xxx_ioctl_init(struct scsi_qla_host *ha)
{
	ha->ql4mbx = qla4xxx_mailbox_command;
	ha->ql4cmd = qla4xxx_send_command_to_isp;
	ha->ql4getaenlog = ql4_get_aen_log;
	return 0;
}

static inline void qla4xxx_ioctl_exit(struct scsi_qla_host *ha)
{
	return;
}

static inline void qla4xxx_srb_free_dma(struct scsi_qla_host *ha,
			struct srb *srb)
{
	struct scsi_cmnd *cmd = srb->cmd;

	if (srb->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		srb->flags &= ~SRB_DMA_VALID;
	}

	cmd->SCp.ptr = NULL;
}

static inline void qla4xxx_remove_device(struct scsi_qla_host *ha)
{
	struct ddb_entry *ddb_entry, *dtemp;

	if (test_and_clear_bit(DPC_REMOVE_DEVICE, &ha->dpc_flags)) {
		list_for_each_entry_safe(ddb_entry, dtemp,
			&ha->ddb_list, list) {
			if (test_and_clear_bit(DF_REMOVE, &ddb_entry->flags)) {
				dev_info(&ha->pdev->dev,
					"%s: ddb[%d] os[%d] - removed\n",
					__func__, ddb_entry->fw_ddb_index,
					ddb_entry->os_target_id);
				qla4xxx_free_ddb(ha, ddb_entry);
			}
		}
	}
}

#endif	/* _QLA4x_OS_H */
