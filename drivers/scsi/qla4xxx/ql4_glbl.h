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
 ******************************************************************************
 * Global include file.
 ****************************************************************************/
#ifndef __QLA4x_GBL_H
#define	__QLA4x_GBL_H

#include <linux/interrupt.h>

/*
 * Defined in ql4_os.c
 */

extern void qla4xxx_start_io(scsi_qla_host_t *ha);
extern srb_t *del_from_active_array(scsi_qla_host_t *ha, uint32_t index);
extern uint8_t qla4xxx_complete_request(scsi_qla_host_t *ha, srb_t *srb);
extern uint8_t qla4xxx_reset_lun(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry, lun_entry_t *lun_entry);
extern uint8_t qla4xxx_soft_reset(scsi_qla_host_t *);
extern const char *host_sts_msg[];
extern void qla4xxx_delete_timer_from_cmd(srb_t *srb);
extern scsi_qla_host_t *qla4xxx_get_adapter_handle(uint16_t instance);
extern uint32_t qla4xxx_get_hba_count(void);
extern void qla4xxx_free_ddb_list(scsi_qla_host_t *ha);

extern void qla4xxx_tgt_free(scsi_qla_host_t *ha, uint16_t t);
extern os_tgt_t *qla4xxx_tgt_alloc(scsi_qla_host_t *, uint16_t);
extern os_lun_t * qla4xxx_lun_alloc(scsi_qla_host_t *, uint16_t, uint16_t);
extern void qla4xxx_extend_timeout(struct scsi_cmnd *cmd, int timeout);
extern int qla4xxx_done(scsi_qla_host_t *old_ha);
extern int qla4xxx_device_suspend( scsi_qla_host_t *, os_lun_t *, srb_t * );
extern void qla4xxx_add_timer_to_cmd(srb_t *, int );
extern int ql4xdontresethba;

extern int extended_error_logging;
/*
 * Defined in  ql4_iocb.c
 */
extern uint8_t qla4xxx_send_marker(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry, lun_entry_t *lun_entry);
extern uint8_t qla4xxx_send_marker_iocb(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry, lun_entry_t *lun_entry);

extern uint8_t qla4xxx_get_req_pkt(scsi_qla_host_t *, QUEUE_ENTRY **);

extern PDU_ENTRY *qla4xxx_get_pdu(scsi_qla_host_t *, uint32_t);
extern void qla4xxx_free_pdu(scsi_qla_host_t *, PDU_ENTRY *);
extern uint8_t qla4xxx_send_passthru0_iocb(scsi_qla_host_t *, uint16_t,
    uint16_t, dma_addr_t, uint32_t, uint32_t, uint16_t, uint32_t);

/*
 * Defined in  ql4_isr.c
 */

extern irqreturn_t qla4xxx_intr_handler(int, void *, struct pt_regs *);
extern void qla4xxx_interrupt_service_routine(scsi_qla_host_t *ha, uint32_t  intr_status);
extern void __qla4xxx_suspend_lun(scsi_qla_host_t *ha, srb_t *srb, os_lun_t *lun_entry, uint16_t time,
		    uint16_t retries, int delay);


/*
 * Defined in  ql4_init.c
 */
extern uint8_t qla4xxx_initialize_adapter(scsi_qla_host_t *ha, uint8_t renew_ddb_list);

extern ddb_entry_t *qla4xxx_alloc_ddb(scsi_qla_host_t *ha, uint32_t fw_ddb_index);
extern uint8_t qla4xxx_update_ddb_entry(scsi_qla_host_t *ha, ddb_entry_t
    *ddb_entry, uint32_t fw_ddb_index);
extern uint8_t qla4xxx_get_fwddb_entry(scsi_qla_host_t *ha, uint16_t fw_ddb_index, DEV_DB_ENTRY *fw_ddb_entry, dma_addr_t fw_ddb_entry_dma, uint32_t *num_valid_ddb_entries, uint32_t *next_ddb_index, uint32_t *fw_ddb_device_state, uint32_t *time2wait, uint16_t *tcp_source_port_num, uint16_t *connection_id);
extern uint8_t qla4xxx_relogin_device(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry);
extern uint8_t qla4xxx_send_command_to_isp(scsi_qla_host_t *, srb_t *);
extern int qla4xxx_get_prop_12chars(scsi_qla_host_t *ha, uint8_t *propname, uint8_t *propval, uint8_t *db);
extern void qla4xxx_free_ddb(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry);
extern uint8_t qla4xxx_resize_ioctl_dma_buf(scsi_qla_host_t *ha, uint32_t size);
extern uint8_t qla4xxx_set_ddb_entry(scsi_qla_host_t *ha, uint16_t fw_ddb_index, DEV_DB_ENTRY *fw_ddb_entry, dma_addr_t fw_ddb_entry_dma);
extern uint8_t qla4xxx_conn_open_session_login(scsi_qla_host_t *ha,
		      uint16_t        fw_ddb_index );
extern uint8_t qla4xxx_process_ddb_changed(scsi_qla_host_t *ha, uint32_t fw_ddb_index, uint32_t state);
extern uint8_t qla4xxx_init_rings(scsi_qla_host_t *ha);
extern uint8_t qla4xxx_reinitialize_ddb_list(scsi_qla_host_t *ha);
extern fc_lun_t * qla4xxx_add_fclun(fc_port_t *fcport, uint16_t lun);
extern os_lun_t *
qla4xxx_fclun_bind(scsi_qla_host_t *ha, fc_port_t *fcport, fc_lun_t *fclun);
extern void qla4xxx_flush_all_srbs(scsi_qla_host_t *ha, ddb_entry_t *ddb_entry, os_lun_t *lun_entry);


/*
 * Defined in  ql4_mbx.c
 */
extern void qla4xxx_process_aen(scsi_qla_host_t *ha, uint8_t flush_ddb_chg_aens);
extern uint8_t qla4xxx_mailbox_command(scsi_qla_host_t *ha, uint8_t inCount, uint8_t outCount, uint32_t *mbx_cmd, uint32_t *mbx_sts);
extern uint8_t qla4xxx_issue_iocb(scsi_qla_host_t *ha, void*  buffer, dma_addr_t phys_addr, size_t size);

extern uint8_t qla4xxx_isns_enable(scsi_qla_host_t *, uint32_t, uint16_t);
extern uint8_t qla4xxx_isns_disable(scsi_qla_host_t *);

extern uint8_t qla4xxx_get_flash(scsi_qla_host_t *, dma_addr_t, uint32_t,
    uint32_t);

extern uint8_t qla4xxx_initialize_fw_cb(scsi_qla_host_t *);

extern uint8_t qla4xxx_get_dhcp_ip_address(scsi_qla_host_t *);

extern uint8_t qla4xxx_get_firmware_state(scsi_qla_host_t *);

extern void qla4xxx_get_crash_record(scsi_qla_host_t *);

extern uint8_t qla4xxx_conn_close_sess_logout(scsi_qla_host_t *, uint16_t,
    uint16_t, uint16_t);

extern uint8_t qla4xxx_clear_database_entry(scsi_qla_host_t *, uint16_t);

extern uint8_t qla4xxx_get_fw_version(scsi_qla_host_t *ha);

extern uint8_t qla4xxx_restore_factory_defaults(scsi_qla_host_t *ha, uint32_t, uint32_t);

extern uint8_t qla4xxx_get_firmware_status(scsi_qla_host_t *ha);

extern void qla4xxx_get_conn_event_log(scsi_qla_host_t *ha);


/*
 * Defined in  ql4_inioct.c
 */
extern void qla4xxx_iocb_pass_done(scsi_qla_host_t *ha, PASSTHRU_STATUS_ENTRY *sts_entry);

/*
 * Defined in  ql4_xioct.c
 */
extern void qla4xxx_scsi_pass_done(struct scsi_cmnd *cmd);
extern void qla4xxx_ioctl_sem_init (scsi_qla_host_t *ha);


/*
 * Defined in  ql4_isns.c
 */
extern uint8_t qla4xxx_isns_process_response(scsi_qla_host_t *ha, PASSTHRU_STATUS_ENTRY *sts_entry);

extern uint8_t
qla4xxx_isns_restart_service_completion(scsi_qla_host_t *ha,
					uint32_t isns_ip_addr,
					uint16_t isns_server_port_num);
extern uint8_t qla4xxx_isns_restart_service(scsi_qla_host_t *);

extern uint8_t qla4xxx_isns_init_attributes(scsi_qla_host_t *);

extern uint8_t qla4xxx_isns_reenable(scsi_qla_host_t *, uint32_t, uint16_t);

extern void qla4xxx_isns_enable_callback(scsi_qla_host_t *, uint32_t, uint32_t,
    uint32_t, uint32_t);
extern uint8_t qla4xxx_isns_get_server_request(scsi_qla_host_t *, uint32_t,
    uint16_t);

	/*
 * Defined in  ql4_nvram.c
	 */

extern u16 RD_NVRAM_WORD(scsi_qla_host_t *, int);
extern uint8_t qla4xxx_is_NVRAM_configuration_valid(scsi_qla_host_t *ha);
extern int ql4xxx_sem_lock(scsi_qla_host_t *ha, u32 sem_mask, u32 sem_bits) ;
extern void ql4xxx_sem_unlock(scsi_qla_host_t *ha, u32 sem_mask) ;
extern int ql4xxx_sem_spinlock(scsi_qla_host_t *ha, u32 sem_mask, u32 sem_bits) ;

/*
 * Defined in  ql4_dbg.c
 */
extern void qla4xxx_dump_buffer(uint8_t *, uint32_t);

#endif /* _QLA4x_GBL_H */
