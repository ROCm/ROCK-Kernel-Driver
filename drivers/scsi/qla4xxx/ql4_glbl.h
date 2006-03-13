/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#ifndef __QLA4x_GBL_H
#define	__QLA4x_GBL_H

/*
 * Defined in ql4_os.c
 */
extern void qla4xxx_start_io(scsi_qla_host_t * ha);
extern srb_t *del_from_active_array(scsi_qla_host_t * ha, uint32_t index);
extern void qla4xxx_srb_compl(scsi_qla_host_t *, srb_t *);
extern int qla4xxx_reset_lun(scsi_qla_host_t * ha, ddb_entry_t * ddb_entry,
				 int);
extern int qla4xxx_soft_reset(scsi_qla_host_t *);
extern const char *host_sts_msg[];
extern void qla4xxx_delete_timer_from_cmd(srb_t * srb);
extern scsi_qla_host_t *qla4xxx_get_adapter_handle(uint16_t instance);
extern void qla4xxx_free_ddb_list(scsi_qla_host_t * ha);
extern void qla4xxx_mark_device_missing(scsi_qla_host_t *, ddb_entry_t *);
extern int extended_error_logging;
extern int ql4xdontresethba;

/*
 * Defined in  ql4_iocb.c
 */
extern int qla4xxx_send_marker(scsi_qla_host_t * ha,
				   ddb_entry_t * ddb_entry, int);
extern int qla4xxx_send_marker_iocb(scsi_qla_host_t * ha,
					ddb_entry_t * ddb_entry, int);
extern int qla4xxx_get_req_pkt(scsi_qla_host_t *, QUEUE_ENTRY **);
extern PDU_ENTRY *qla4xxx_get_pdu(scsi_qla_host_t *, uint32_t);
extern void qla4xxx_free_pdu(scsi_qla_host_t *, PDU_ENTRY *);
extern int qla4xxx_send_passthru0_iocb(scsi_qla_host_t *, uint16_t,
					   uint16_t, dma_addr_t, uint32_t,
					   uint32_t, uint16_t, uint32_t);

/*
 * Defined in  ql4_isr.c
 */
extern irqreturn_t qla4xxx_intr_handler(int, void *, struct pt_regs *);
extern void qla4xxx_interrupt_service_routine(scsi_qla_host_t * ha,
					      uint32_t intr_status);

/*
 * Defined in  ql4_init.c
 */
extern int qla4xxx_initialize_adapter(scsi_qla_host_t * ha,
					  uint8_t renew_ddb_list);
extern ddb_entry_t *qla4xxx_alloc_ddb(scsi_qla_host_t * ha,
				      uint32_t fw_ddb_index);
extern int qla4xxx_update_ddb_entry(scsi_qla_host_t * ha,
					ddb_entry_t * ddb_entry,
					uint32_t fw_ddb_index);
extern int qla4xxx_get_fwddb_entry(scsi_qla_host_t * ha,
				       uint16_t fw_ddb_index,
				       DEV_DB_ENTRY * fw_ddb_entry,
				       dma_addr_t fw_ddb_entry_dma,
				       uint32_t * num_valid_ddb_entries,
				       uint32_t * next_ddb_index,
				       uint32_t * fw_ddb_device_state,
				       uint32_t * time2wait,
				       uint16_t * tcp_source_port_num,
				       uint16_t * connection_id);
extern int qla4xxx_relogin_device(scsi_qla_host_t * ha,
				      ddb_entry_t * ddb_entry);
extern int qla4xxx_send_command_to_isp(scsi_qla_host_t *, srb_t *);
extern int qla4xxx_get_prop_12chars(scsi_qla_host_t * ha, uint8_t * propname,
				    uint8_t * propval, uint8_t * db);
extern void qla4xxx_free_ddb(scsi_qla_host_t * ha, ddb_entry_t * ddb_entry);
extern int qla4xxx_resize_ioctl_dma_buf(scsi_qla_host_t * ha,
					    uint32_t size);
extern int qla4xxx_set_ddb_entry(scsi_qla_host_t * ha,
				     uint16_t fw_ddb_index,
				     DEV_DB_ENTRY * fw_ddb_entry,
				     dma_addr_t fw_ddb_entry_dma);
extern int qla4xxx_conn_open_session_login(scsi_qla_host_t * ha,
					       uint16_t fw_ddb_index);
extern int qla4xxx_process_ddb_changed(scsi_qla_host_t * ha,
					   uint32_t fw_ddb_index,
					   uint32_t state);
extern int qla4xxx_init_rings(scsi_qla_host_t * ha);
extern int qla4xxx_reinitialize_ddb_list(scsi_qla_host_t * ha);

/*
 * Defined in  ql4_mbx.c
 */
extern void qla4xxx_process_aen(scsi_qla_host_t * ha,
				uint8_t flush_ddb_chg_aens);
extern int qla4xxx_mailbox_command(scsi_qla_host_t * ha, uint8_t inCount,
				       uint8_t outCount, uint32_t * mbx_cmd,
				       uint32_t * mbx_sts);
extern int qla4xxx_issue_iocb(scsi_qla_host_t * ha, void *buffer,
				  dma_addr_t phys_addr, size_t size);
extern int qla4xxx_get_flash(scsi_qla_host_t *, dma_addr_t, uint32_t,
				 uint32_t);
extern int qla4xxx_initialize_fw_cb(scsi_qla_host_t *);
extern int qla4xxx_get_dhcp_ip_address(scsi_qla_host_t *);

extern int qla4xxx_get_firmware_state(scsi_qla_host_t *);
extern void qla4xxx_get_crash_record(scsi_qla_host_t *);
extern int qla4xxx_conn_close_sess_logout(scsi_qla_host_t *, uint16_t,
					      uint16_t, uint16_t);
extern int qla4xxx_clear_database_entry(scsi_qla_host_t *, uint16_t);
extern int qla4xxx_get_fw_version(scsi_qla_host_t * ha);
extern int qla4xxx_get_firmware_status(scsi_qla_host_t * ha);
extern void qla4xxx_get_conn_event_log(scsi_qla_host_t * ha);

/*
 * Defined in  ql4_inioct.c
 */
extern void qla4xxx_iocb_pass_done(scsi_qla_host_t * ha,
				   PASSTHRU_STATUS_ENTRY * sts_entry);

/*
 * Defined in  ql4_xioct.c
 */
extern void qla4xxx_scsi_pass_done(struct scsi_cmnd *cmd);
extern void qla4xxx_ioctl_sem_init(scsi_qla_host_t * ha);

/*
 * Defined in  ql4_nvram.c
 */
extern u16 RD_NVRAM_WORD(scsi_qla_host_t *, int);
extern int qla4xxx_is_nvram_configuration_valid(scsi_qla_host_t * ha);
extern int ql4xxx_sem_lock(scsi_qla_host_t * ha, u32 sem_mask, u32 sem_bits);
extern void ql4xxx_sem_unlock(scsi_qla_host_t * ha, u32 sem_mask);
extern int ql4xxx_sem_spinlock(scsi_qla_host_t * ha, u32 sem_mask,
			       u32 sem_bits);

/*
 * Defined in  ql4_dbg.c
 */
extern void qla4xxx_dump_buffer(uint8_t *, uint32_t);

#endif				/* _QLA4x_GBL_H */
