/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP2x00 device driver for Linux 2.6.x
* Copyright (C) 2003 QLogic Corporation
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
******************************************************************************
* Global include file.
******************************************************************************/


#ifndef __QLA_GBL_H
#define	__QLA_GBL_H

/*
 * Global Data in FW files.
 */
extern unsigned char  fw2100tp_version[];
extern unsigned char  fw2100tp_version_str[];
extern unsigned short fw2100tp_addr01;
extern unsigned short fw2100tp_code01[];
extern unsigned short fw2100tp_length01;

extern unsigned char  fw2200tp_version[];
extern unsigned char  fw2200tp_version_str[];
extern unsigned short fw2200tp_addr01;
extern unsigned short fw2200tp_code01[];
extern unsigned short fw2200tp_length01;

extern unsigned char  fw2300tpx_version[];
extern unsigned char  fw2300tpx_version_str[];
extern unsigned short fw2300tpx_addr01;
extern unsigned short fw2300tpx_code01[];
extern unsigned short fw2300tpx_length01;

#if defined(ISP2322)
extern unsigned char  fw2322tpx_version[];
extern unsigned char  fw2322tpx_version_str[];
extern unsigned short fw2322tpx_addr01;
extern unsigned short fw2322tpx_code01[];
extern unsigned short fw2322tpx_length01;
extern unsigned long rseqtpx_code_addr01;
extern unsigned short rseqtpx_code01[];
extern unsigned short rseqtpx_code_length01;
extern unsigned long xseqtpx_code_addr01;
extern unsigned short xseqtpx_code01[];
extern unsigned short xseqtpx_code_length01;
#endif

/*
 * Global Function Prototypes in qla_init.c source file.
 */
extern int qla2x00_initialize_adapter(scsi_qla_host_t *);
extern fc_port_t *qla2x00_alloc_fcport(scsi_qla_host_t *, int);

extern int qla2x00_loop_resync(scsi_qla_host_t *);

extern int qla2x00_find_new_loop_id(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_fabric_login(scsi_qla_host_t *, fc_port_t *, uint16_t *);
extern int qla2x00_local_device_login(scsi_qla_host_t *, uint16_t);

extern void qla2x00_restart_queues(scsi_qla_host_t *, uint8_t);

extern void qla2x00_rescan_fcports(scsi_qla_host_t *);

extern void qla2x00_tgt_free(scsi_qla_host_t *ha, uint16_t t);
extern os_tgt_t *qla2x00_tgt_alloc(scsi_qla_host_t *, uint16_t);
extern os_lun_t * qla2x00_lun_alloc(scsi_qla_host_t *, uint16_t, uint16_t);

extern int qla2x00_abort_isp(scsi_qla_host_t *);


/*
 * Global Data in qla_os.c source file.
 */
extern char qla2x00_version_str[];
extern unsigned long qla2x00_verbose;
extern unsigned long qla2x00_reinit;
extern unsigned long qla2x00_req_dmp;

extern int num_hosts;
extern int apiHBAInstance;

extern struct _qla2x00stats qla2x00_stats;
extern char *ql2xdevconf;
extern int ql2xretrycount;
extern int qla2xenbinq;
extern int ql2xlogintimeout;
extern int qlport_down_retry;
extern int ql2xmaxqdepth;
extern int displayConfig;
extern int ql2xplogiabsentdevice;
#if defined(ISP2300)
extern int ql2xintrdelaytimer;
#endif

extern int ql2xfailover;

extern int ConfigRequired;
extern int recoveryTime;
extern int failbackTime;

extern int Bind;
extern int ql2xsuspendcount;
extern int qla2x00_retryq_dmp;
#if defined(MODULE)
extern char *ql2xopts;
#endif
extern struct list_head qla_hostlist;
extern rwlock_t qla_hostlist_lock;

extern char *qla2x00_get_fw_version_str(struct scsi_qla_host *, char *);

extern int qla2x00_queuecommand(struct scsi_cmnd *,
    void (*)(struct scsi_cmnd *));

extern int __qla2x00_suspend_lun(scsi_qla_host_t *, os_lun_t *, int, int, int);

extern void qla2x00_done(scsi_qla_host_t *);
extern void qla2x00_next(scsi_qla_host_t *);
extern void qla2x00_flush_failover_q(scsi_qla_host_t *, os_lun_t *);
extern void qla2x00_reset_lun_fo_counts(scsi_qla_host_t *, os_lun_t *);

extern int qla2x00_check_tgt_status(scsi_qla_host_t *, struct scsi_cmnd *);
extern int qla2x00_check_port_status(scsi_qla_host_t *, fc_port_t *);

extern void qla2x00_extend_timeout(struct scsi_cmnd *, int);
extern srb_t * qla2x00_get_new_sp (scsi_qla_host_t *ha);

extern void qla2x00_mark_device_lost(scsi_qla_host_t *, fc_port_t *, int);
extern void qla2x00_mark_all_devices_lost(scsi_qla_host_t *);

extern int qla2x00_get_prop_xstr(scsi_qla_host_t *, char *, uint8_t *, int);

extern void qla2x00_abort_queues(scsi_qla_host_t *, uint8_t);

extern void qla2x00_blink_led(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_iocb.c source file.
 */
extern request_t *qla2x00_req_pkt(scsi_qla_host_t *);
extern request_t *qla2x00_ms_req_pkt(scsi_qla_host_t *, srb_t *);
extern void qla2x00_isp_cmd(scsi_qla_host_t *);

extern uint16_t qla2x00_calc_iocbs_32(uint16_t);
extern uint16_t qla2x00_calc_iocbs_64(uint16_t);
extern void qla2x00_build_scsi_iocbs_32(srb_t *, cmd_entry_t *, uint16_t);
extern void qla2x00_build_scsi_iocbs_64(srb_t *, cmd_entry_t *, uint16_t);
extern int qla2x00_start_scsi(srb_t *sp);
int qla2x00_marker(scsi_qla_host_t *, uint16_t, uint16_t, uint8_t);
int __qla2x00_marker(scsi_qla_host_t *, uint16_t, uint16_t, uint8_t);

/*
 * Global Function Prototypes in qla_mbx.c source file.
 */
extern int
qla2x00_mailbox_command(scsi_qla_host_t *, mbx_cmd_t *);

extern int
qla2x00_load_ram(scsi_qla_host_t *, dma_addr_t, uint16_t, uint16_t);

extern int
qla2x00_load_ram_ext(scsi_qla_host_t *, dma_addr_t, uint32_t, uint16_t);

extern int
qla2x00_execute_fw(scsi_qla_host_t *);

extern void
qla2x00_get_fw_version(scsi_qla_host_t *, uint16_t *,
    uint16_t *, uint16_t *, uint16_t *);

extern int
qla2x00_get_fw_options(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_set_fw_options(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_read_ram_word(scsi_qla_host_t *, uint16_t, uint16_t *);
extern int
qla2x00_write_ram_word(scsi_qla_host_t *, uint16_t, uint16_t);
extern int
qla2x00_write_ram_word_ext(scsi_qla_host_t *, uint32_t, uint16_t);

extern int
qla2x00_mbx_reg_test(scsi_qla_host_t *);

extern int
qla2x00_verify_checksum(scsi_qla_host_t *);

extern int
qla2x00_issue_iocb(scsi_qla_host_t *, void *, dma_addr_t, size_t);

extern int
qla2x00_abort_command(scsi_qla_host_t *, srb_t *);

extern int
qla2x00_abort_device(scsi_qla_host_t *, uint16_t, uint16_t);

#if USE_ABORT_TGT
extern int
qla2x00_abort_target(fc_port_t *fcport);
#endif

extern int
qla2x00_target_reset(scsi_qla_host_t *, uint16_t, uint16_t);

extern int
qla2x00_get_adapter_id(scsi_qla_host_t *, uint16_t *, uint8_t *, uint8_t *,
    uint8_t *, uint16_t *);

extern int
qla2x00_get_retry_cnt(scsi_qla_host_t *, uint8_t *, uint8_t *, uint16_t *);

extern int
qla2x00_loopback_test(scsi_qla_host_t *, INT_LOOPBACK_REQ *, uint16_t *);

extern int
qla2x00_echo_test(scsi_qla_host_t *, INT_LOOPBACK_REQ *, uint16_t *);

extern int
qla2x00_init_firmware(scsi_qla_host_t *, uint16_t);

extern int
qla2x00_get_port_database(scsi_qla_host_t *, fc_port_t *, uint8_t);

extern int
qla2x00_get_firmware_state(scsi_qla_host_t *, uint16_t *);

extern int
qla2x00_get_port_name(scsi_qla_host_t *, uint16_t, uint8_t *, uint8_t);

extern uint8_t
qla2x00_get_link_status(scsi_qla_host_t *, uint8_t, void *, uint16_t *);

extern int
qla2x00_lip_reset(scsi_qla_host_t *);

extern int
qla2x00_send_sns(scsi_qla_host_t *, dma_addr_t, uint16_t, size_t);

extern int
qla2x00_login_fabric(scsi_qla_host_t *, uint16_t, uint8_t, uint8_t, uint8_t,
    uint16_t *, uint8_t);

extern int
qla2x00_login_local_device(scsi_qla_host_t *, uint16_t, uint16_t *, uint8_t);

extern int
qla2x00_fabric_logout(scsi_qla_host_t *ha, uint16_t loop_id);

extern int
qla2x00_full_login_lip(scsi_qla_host_t *ha);

extern int
qla2x00_get_id_list(scsi_qla_host_t *, void *, dma_addr_t, uint16_t *);

#if 0 /* not yet needed */
extern int
qla2x00_dump_ram(scsi_qla_host_t *, uint32_t, dma_addr_t, uint32_t);
#endif

extern int
qla2x00_lun_reset(scsi_qla_host_t *, uint16_t, uint16_t);

extern int
qla2x00_send_rnid_mbx(scsi_qla_host_t *, uint16_t, uint8_t, dma_addr_t,
    size_t, uint16_t *);

extern int
qla2x00_set_rnid_params_mbx(scsi_qla_host_t *, dma_addr_t, size_t, uint16_t *);

extern int
qla2x00_get_rnid_params_mbx(scsi_qla_host_t *, dma_addr_t, size_t, uint16_t *);

extern int
qla2x00_get_resource_cnts(scsi_qla_host_t *, uint16_t *, uint16_t *, uint16_t *,
    uint16_t *);

#if defined(QL_DEBUG_LEVEL_3)
extern int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map);
#endif

/*
 * Global Data in qla_fo.c source file.
 */
extern SysFoParams_t qla_fo_params;

/*
 * Global Function Prototypes in qla_fo.c source file.
 */
extern scsi_qla_host_t *qla2x00_get_hba(unsigned long);
extern uint32_t qla2x00_send_fo_notification(fc_lun_t *fclun_p, fc_lun_t *olun_p);
extern void qla2x00_fo_init_params(scsi_qla_host_t *ha);
extern uint8_t qla2x00_fo_enabled(scsi_qla_host_t *ha, int instance);

/*
 * Global Data in qla_cfg.c source file.
 */
extern mp_host_t  *mp_hosts_base;
extern uint8_t   mp_config_required;
/*
 * Global Function Prototypes in qla_cfg.c source file.
 */
extern mp_host_t * qla2x00_cfg_find_host(scsi_qla_host_t *);
extern uint8_t qla2x00_is_portname_in_device(mp_device_t *, uint8_t *);
extern int qla2x00_cfg_init (scsi_qla_host_t *ha);
extern int qla2x00_cfg_path_discovery(scsi_qla_host_t *ha);
extern int qla2x00_cfg_event_notify(scsi_qla_host_t *ha, uint32_t i_type);
extern fc_lun_t *qla2x00_cfg_failover(scsi_qla_host_t *ha, fc_lun_t *fp,
					      os_tgt_t *tgt, srb_t *sp);
extern int qla2x00_cfg_get_paths( EXT_IOCTL *, FO_GET_PATHS *, int);
extern int qla2x00_cfg_set_current_path( EXT_IOCTL *,
			FO_SET_CURRENT_PATH *, int);
extern void qla2x00_fo_properties(scsi_qla_host_t *ha);
extern mp_host_t * qla2x00_add_mp_host(uint8_t *);
extern void qla2x00_cfg_mem_free(scsi_qla_host_t *ha);
extern mp_host_t * qla2x00_alloc_host(scsi_qla_host_t *);
extern uint8_t qla2x00_fo_check(scsi_qla_host_t *ha, srb_t *sp);
extern mp_path_t *qla2x00_find_path_by_name(mp_host_t *, mp_path_list_t *,
			uint8_t *name);
extern int qla2x00_is_fcport_in_config(scsi_qla_host_t *, fc_port_t *);

/*
 * Global Function Prototypes in qla_cfgln.c source file.
 */
extern void qla2x00_cfg_build_path_tree( scsi_qla_host_t *ha);
extern uint8_t qla2x00_update_mp_device(mp_host_t *,
    fc_port_t  *, uint16_t, uint16_t);
extern void qla2x00_cfg_display_devices(void);

/*
 * Global Function Prototypes in qla_xioctl.c source file.
 */
extern void qla2x00_enqueue_aen(scsi_qla_host_t *, uint16_t, void *);
extern int qla2x00_fo_ioctl(scsi_qla_host_t *, int, EXT_IOCTL *, int);
extern int qla2x00_fo_missing_port_summary(scsi_qla_host_t *,
    EXT_DEVICEDATAENTRY *, void *, uint32_t, uint32_t *, uint32_t *);
extern int qla2x00_alloc_ioctl_mem(scsi_qla_host_t *);
extern void qla2x00_free_ioctl_mem(scsi_qla_host_t *);
extern int qla2x00_get_ioctl_scrap_mem(scsi_qla_host_t *, void **, uint32_t);
extern void qla2x00_free_ioctl_scrap_mem(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_inioctl.c source file.
 */
extern int qla2x00_read_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
extern int qla2x00_update_nvram(scsi_qla_host_t *, EXT_IOCTL *, int);
extern int qla2x00_write_nvram_word(scsi_qla_host_t *, uint8_t, uint16_t);
extern int qla2x00_send_loopback(scsi_qla_host_t *, EXT_IOCTL *, int);
extern int qla2x00_read_option_rom(scsi_qla_host_t *, EXT_IOCTL *, int);
extern int qla2x00_update_option_rom(scsi_qla_host_t *, EXT_IOCTL *, int);


/*
 * Global Function Prototypes in qla_isr.c source file.
 */
extern irqreturn_t qla2x00_intr_handler(int, void *, struct pt_regs *);
extern void qla2x00_process_response_queue(struct scsi_qla_host *);


/*
 * Global Function Prototypes in qla_sup.c source file.
 */

extern uint16_t qla2x00_get_nvram_word(scsi_qla_host_t *, uint32_t);
extern void qla2x00_nv_write(scsi_qla_host_t *, uint16_t);
extern void qla2x00_nv_deselect(scsi_qla_host_t *);
extern void qla2x00_flash_enable(scsi_qla_host_t *);
extern void qla2x00_flash_disable(scsi_qla_host_t *);
extern uint8_t qla2x00_read_flash_byte(scsi_qla_host_t *, uint32_t);
extern uint8_t qla2x00_get_flash_manufacturer(scsi_qla_host_t *);
extern uint16_t qla2x00_get_flash_version(scsi_qla_host_t *);
extern uint16_t qla2x00_get_flash_image(scsi_qla_host_t *, uint8_t *);
extern uint16_t qla2x00_set_flash_image(scsi_qla_host_t *, uint8_t *);

/*
 * Global Function Prototypes in qla_vendor.c source file.
 */
void qla2x00_set_vend_direction(scsi_qla_host_t *, struct scsi_cmnd *,
    cmd_entry_t *);

/*
 * Global Function Prototypes in qla_dbg.c source file.
 */
extern void qla2x00_fw_dump(scsi_qla_host_t *, int);
extern void qla2x00_ascii_fw_dump(scsi_qla_host_t *);
extern void qla2x00_dump_regs(scsi_qla_host_t *);
extern void qla2x00_dump_buffer(uint8_t *, uint32_t);
extern void qla2x00_print_scsi_cmd(struct scsi_cmnd *);
extern void qla2x00_print_q_info(struct os_lun *);

/*
 * Global Function Prototypes in qla_ip.c source file.
 */
extern int qla2x00_ip_initialize(scsi_qla_host_t *);
extern int qla2x00_update_ip_device_data(scsi_qla_host_t *, fc_port_t *);
extern void qla2x00_ip_send_complete(scsi_qla_host_t *, uint32_t, uint16_t);
extern void qla2x00_ip_receive(scsi_qla_host_t *, sts_entry_t *);
extern void qla2x00_ip_receive_fastpost(scsi_qla_host_t *, uint16_t);
extern void qla2x00_ip_mailbox_iocb_done(scsi_qla_host_t *, struct mbx_entry *);

/*
 * Global Function Prototypes in qla_gs.c source file.
 */
extern int qla2x00_ga_nxt(scsi_qla_host_t *, fc_port_t *);
extern int qla2x00_gid_pt(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gpn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gnn_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_gft_id(scsi_qla_host_t *, sw_info_t *);
extern int qla2x00_rft_id(scsi_qla_host_t *);
extern int qla2x00_rff_id(scsi_qla_host_t *);
extern int qla2x00_rnn_id(scsi_qla_host_t *);
extern int qla2x00_rsnn_nn(scsi_qla_host_t *);

/*
 * Global Function Prototypes in qla_rscn.c source file.
 */

#if defined(ISP2300)
extern fc_port_t *qla2x00_alloc_rscn_fcport(scsi_qla_host_t *, int);
extern int qla2x00_handle_port_rscn(scsi_qla_host_t *, uint32_t, fc_port_t *,
    int);
extern void qla2x00_process_iodesc(scsi_qla_host_t *, struct mbx_entry *);
extern void qla2x00_cancel_io_descriptors(scsi_qla_host_t *);
#endif

#endif /* _QLA_GBL_H */
