/* 
 * 
 * linux/drivers/s390/scsi/zfcp_ext.h
 * 
 * FCP adapter driver for IBM eServer zSeries 
 * 
 * Copyright 2002 IBM Corporation 
 * Author(s): Martin Peschke <mpeschke@de.ibm.com> 
 *            Raimund Schroeder <raimund.schroeder@de.ibm.com> 
 *            Aron Zeh <arzeh@de.ibm.com> 
 *            Wolfgang Taphorn <taphorn@de.ibm.com> 
 *            Stefan Bader <stefan.bader@de.ibm.com> 
 *            Heiko Carstens <heiko.carstens@de.ibm.com> 
 * 
 * This program is free software; you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version. 
 * 
 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * 
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */

#ifndef ZFCP_EXT_H
#define ZFCP_EXT_H
/* this drivers version (do not edit !!! generated and updated by cvs) */
#define ZFCP_EXT_REVISION "$Revision: 1.33 $"

#ifdef __KERNEL__

#include "zfcp_def.h"

extern struct zfcp_data zfcp_data;

/******************************** SYSFS  *************************************/
extern int  zfcp_sysfs_driver_create_files(struct device_driver *);
extern void zfcp_sysfs_driver_remove_files(struct device_driver *);
extern int  zfcp_sysfs_adapter_create_files(struct device *);
extern void zfcp_sysfs_adapter_remove_files(struct device *);
extern int  zfcp_sysfs_port_create_files(struct device *, u32);
extern int  zfcp_sysfs_unit_create_files(struct device *);
extern void zfcp_sysfs_port_release(struct device *);
extern int  zfcp_sysfs_port_shutdown(struct zfcp_port *);
extern void zfcp_sysfs_unit_release(struct device *);

/**************************** CONFIGURATION  *********************************/
extern struct zfcp_unit *zfcp_get_unit_by_lun(struct zfcp_port *,
					      fcp_lun_t fcp_lun);
extern struct zfcp_port *zfcp_get_port_by_wwpn(struct zfcp_adapter *,
					       wwn_t wwpn);
extern struct zfcp_adapter *zfcp_adapter_enqueue(struct ccw_device *);
extern void   zfcp_adapter_dequeue(struct zfcp_adapter *);
extern struct zfcp_port *zfcp_port_enqueue(struct zfcp_adapter *, wwn_t, u32);
extern void   zfcp_port_dequeue(struct zfcp_port *);
extern struct zfcp_unit *zfcp_unit_enqueue(struct zfcp_port *, fcp_lun_t);
extern void   zfcp_unit_dequeue(struct zfcp_unit *);

/******************************* S/390 IO ************************************/
extern int  zfcp_ccw_register(void);
extern void zfcp_ccw_unregister(void);

extern int  zfcp_initialize_with_0copy(struct zfcp_adapter *);
extern void zfcp_qdio_zero_sbals(struct qdio_buffer **, int, int);
extern int  zfcp_qdio_allocate(struct zfcp_adapter *);
extern int  zfcp_qdio_allocate_queues(struct zfcp_adapter *);
extern void zfcp_qdio_free_queues(struct zfcp_adapter *);
extern int  zfcp_qdio_determine_pci(struct zfcp_qdio_queue *,
				    struct zfcp_fsf_req *);
extern int  zfcp_qdio_reqid_check(struct zfcp_adapter *, void *);

/******************************** FSF ****************************************/
extern int  zfcp_fsf_open_port(struct zfcp_erp_action *);
extern int  zfcp_fsf_close_port(struct zfcp_erp_action *);
extern int  zfcp_fsf_close_physical_port(struct zfcp_erp_action *);

extern int  zfcp_fsf_open_unit(struct zfcp_erp_action *);
extern int  zfcp_fsf_close_unit(struct zfcp_erp_action *);

extern int  zfcp_fsf_exchange_config_data(struct zfcp_erp_action *);
extern void zfcp_fsf_scsi_er_timeout_handler(unsigned long);
extern int  zfcp_fsf_req_dismiss_all(struct zfcp_adapter *);
extern int  zfcp_fsf_status_read(struct zfcp_adapter *, int);
extern int  zfcp_fsf_req_create(struct zfcp_adapter *,u32, unsigned long *,
				int, struct zfcp_fsf_req **);
extern void zfcp_fsf_req_free(struct zfcp_fsf_req *);
extern int  zfcp_fsf_send_generic(struct zfcp_fsf_req *, unsigned char,
				  unsigned long *, struct timer_list *);
extern int  zfcp_fsf_req_wait_and_cleanup(struct zfcp_fsf_req *, int, u32 *);
extern int  zfcp_fsf_send_fcp_command_task(struct zfcp_adapter *,
					   struct zfcp_unit *, Scsi_Cmnd *,
					   int);
extern int  zfcp_fsf_req_complete(struct zfcp_fsf_req *);
extern void zfcp_fsf_incoming_els(struct zfcp_fsf_req *);
extern void zfcp_fsf_req_cleanup(struct zfcp_fsf_req *);
extern struct zfcp_fsf_req *zfcp_fsf_send_fcp_command_task_management(
	struct zfcp_adapter *, struct zfcp_unit *, u8, int);
extern struct zfcp_fsf_req *zfcp_fsf_abort_fcp_command(
	unsigned long, struct zfcp_adapter *, struct zfcp_unit *, int);

/******************************** FCP ****************************************/
extern int  zfcp_nameserver_enqueue(struct zfcp_adapter *);
extern int  zfcp_nameserver_request(struct zfcp_erp_action *);
extern void zfcp_fsf_els_processing(struct zfcp_fsf_req *);

/******************************* SCSI ****************************************/
extern int  zfcp_adapter_scsi_register(struct zfcp_adapter *);
extern void zfcp_adapter_scsi_unregister(struct zfcp_adapter *);
extern void zfcp_scsi_block_requests(struct Scsi_Host *);
extern void zfcp_scsi_insert_into_fake_queue(struct zfcp_adapter *,
					     Scsi_Cmnd *);
extern void zfcp_scsi_process_and_clear_fake_queue(unsigned long);
extern int  zfcp_create_sbals_from_sg(struct zfcp_fsf_req *,
				     Scsi_Cmnd *, char, int, int);
extern void zfcp_set_fcp_dl(struct fcp_cmnd_iu *, fcp_dl_t);
extern char *zfcp_get_fcp_rsp_info_ptr(struct fcp_rsp_iu *);
extern void set_host_byte(u32 *, char);
extern void set_driver_byte(u32 *, char);
extern char *zfcp_get_fcp_sns_info_ptr(struct fcp_rsp_iu *);
extern void zfcp_fsf_start_scsi_er_timer(struct zfcp_adapter *);
extern fcp_dl_t zfcp_get_fcp_dl(struct fcp_cmnd_iu *);

/******************************** ERP ****************************************/
extern void zfcp_erp_modify_adapter_status(struct zfcp_adapter *, u32, int);
extern int  zfcp_erp_adapter_reopen(struct zfcp_adapter *, int);
extern int  zfcp_erp_adapter_shutdown(struct zfcp_adapter *, int);
extern int  zfcp_erp_adapter_shutdown_all(void);
extern void zfcp_erp_adapter_failed(struct zfcp_adapter *);

extern void zfcp_erp_modify_port_status(struct zfcp_port *, u32, int);
extern int  zfcp_erp_port_reopen(struct zfcp_port *, int);
extern int  zfcp_erp_port_shutdown(struct zfcp_port *, int);
extern int  zfcp_erp_port_forced_reopen(struct zfcp_port *, int);
extern void zfcp_erp_port_failed(struct zfcp_port *);
extern int  zfcp_erp_port_reopen_all(struct zfcp_adapter *, int);

extern void zfcp_erp_modify_unit_status(struct zfcp_unit *, u32, int);
extern int  zfcp_erp_unit_reopen(struct zfcp_unit *, int);
extern int  zfcp_erp_unit_shutdown(struct zfcp_unit *, int);
extern void zfcp_erp_unit_failed(struct zfcp_unit *);

extern void zfcp_erp_scsi_low_mem_buffer_timeout_handler(unsigned long);
extern int  zfcp_erp_thread_setup(struct zfcp_adapter *);
extern int  zfcp_erp_thread_kill(struct zfcp_adapter *);
extern int  zfcp_erp_wait(struct zfcp_adapter *);
extern void zfcp_erp_fsf_req_handler(struct zfcp_fsf_req *);

/******************************** AUX ****************************************/
extern void zfcp_cmd_dbf_event_fsf(const char *, struct zfcp_fsf_req *,
				   void *, int);
extern void zfcp_cmd_dbf_event_scsi(const char *, Scsi_Cmnd *);
extern void zfcp_in_els_dbf_event(struct zfcp_adapter *, const char *,
				  struct fsf_status_read_buffer *, int);
#ifdef ZFCP_STAT_REQSIZES
extern int  zfcp_statistics_inc(struct list_head *, u32);
#endif
#endif	/* __KERNEL__ */
#endif	/* ZFCP_EXT_H */
