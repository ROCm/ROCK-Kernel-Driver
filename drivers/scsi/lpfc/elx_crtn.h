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

#ifndef _H_ELX_CRTN
#define _H_ELX_CRTN

void elx_read_rev(elxHBA_t *, ELX_MBOXQ_t *);
void elx_config_ring(elxHBA_t *, int, ELX_MBOXQ_t *);
int elx_config_port(elxHBA_t *, ELX_MBOXQ_t *);
void elx_mbox_put(elxHBA_t *, ELX_MBOXQ_t *);
ELX_MBOXQ_t *elx_mbox_get(elxHBA_t *);

DMABUF_t *elx_mem_alloc_dmabuf(elxHBA_t *, uint32_t);
DMABUFEXT_t *elx_mem_alloc_dmabufext(elxHBA_t *, uint32_t);
int elx_mem_alloc(elxHBA_t *);
int elx_mem_free(elxHBA_t *);
void *elx_mem_get(elxHBA_t *, int);
uint8_t *elx_mem_put(elxHBA_t *, int, uint8_t *);

int elx_sli_hba_setup(elxHBA_t *);
int elx_sli_hba_down(elxHBA_t *);
int elx_sli_ring_map(elxHBA_t *);
int elx_sli_intr(elxHBA_t *);
int elx_sli_issue_mbox(elxHBA_t *, ELX_MBOXQ_t *, uint32_t);
void elx_mbox_abort(elxHBA_t *);
int elx_sli_issue_iocb(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *, uint32_t);
int elx_sli_resume_iocb(elxHBA_t *, ELX_SLI_RING_t *);
int elx_sli_brdreset(elxHBA_t *);
int elx_sli_setup(elxHBA_t *);
void elx_sli_pcimem_bcopy(uint32_t *, uint32_t *, uint32_t);
int elx_sli_ringpostbuf_put(elxHBA_t *, ELX_SLI_RING_t *, DMABUF_t *);
DMABUF_t *elx_sli_ringpostbuf_get(elxHBA_t *, ELX_SLI_RING_t *, elx_dma_addr_t);
uint32_t elx_sli_next_iotag(elxHBA_t *, ELX_SLI_RING_t *);
int elx_sli_abort_iocb(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
int elx_sli_issue_abort_iotag32(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
int elx_sli_abort_iocb_ring(elxHBA_t *, ELX_SLI_RING_t *, uint32_t);
int elx_sli_abort_iocb_ctx(elxHBA_t *, ELX_SLI_RING_t *, uint32_t);
int elx_sli_abort_iocb_context1(elxHBA_t *, ELX_SLI_RING_t *, void *);
int elx_sli_abort_iocb_lun(elxHBA_t *, ELX_SLI_RING_t *, uint16_t, uint64_t);
int elx_sli_abort_iocb_tgt(elxHBA_t *, ELX_SLI_RING_t *, uint16_t);
int elx_sli_abort_iocb_hba(elxHBA_t *, ELX_SLI_RING_t *);

int elx_log_chk_msg_disabled(int, msgLogDef *);
int elx_printf(void *, ...);
int elx_printf_log(int, msgLogDef *, void *, ...);
int elx_str_sprintf(void *, void *, ...);
int elx_str_atox(elxHBA_t *, int, int, char *, char *);
int elx_str_ctox(uint8_t);
char *elx_str_cpy(char *, char *);
int elx_str_ctoh(uint8_t);
int elx_str_itos(int, uint8_t *, int);
int elx_str_isdigit(int);
int elx_str_ncmp(char *, char *, int);
int elx_is_digit(int);
int elx_str_len(char *);

int elx_fmtout(uint8_t * ostr, uint8_t * control, va_list inarg);

int elx_clk_can(elxHBA_t *, ELXCLOCK_t *);
unsigned long elx_clk_rem(elxHBA_t *, ELXCLOCK_t *);
unsigned long elx_clk_res(elxHBA_t *, unsigned long, ELXCLOCK_t *);
ELXCLOCK_t *elx_clk_set(elxHBA_t *, unsigned long,
			void (*func) (elxHBA_t *, void *, void *), void *,
			void *);
void elx_timer(void *);
void elx_clock_deque(ELXCLOCK_t *);
void elx_clock_init(void);

/* For Operating System Specific support */
uint8_t *elx_malloc(elxHBA_t *, struct mbuf_info *);
void elx_free(elxHBA_t *, struct mbuf_info *);
uint32_t elx_hba_init(elxHBA_t *, ELX_MBOXQ_t *);
int elx_print(char *, void *, void *);
int elx_printf_log_msgblk(int, msgLogDef *, char *);
void elx_sli_wake_iocb_wait(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
int elx_sli_issue_iocb_wait(elxHBA_t *, ELX_SLI_RING_t *,
			    ELX_IOCBQ_t *, uint32_t, ELX_IOCBQ_t *, uint32_t);
int elx_sli_issue_mbox_wait(elxHBA_t *, ELX_MBOXQ_t *, uint32_t);
void elx_sli_wake_mbox_wait(elxHBA_t *, ELX_MBOXQ_t *);
int elx_sleep(elxHBA_t *, void *, long tmo);
void elx_wakeup(elxHBA_t *, void *);

int elx_os_prep_io(elxHBA_t *, ELX_SCSI_BUF_t *);
ELX_SCSI_BUF_t *elx_get_scsi_buf(elxHBA_t *);
void elx_free_scsi_buf(ELX_SCSI_BUF_t *);
ELXSCSILUN_t *elx_find_lun_device(ELX_SCSI_BUF_t *);
void elx_map_fcp_cmnd_to_bpl(elxHBA_t *, ELX_SCSI_BUF_t *);
void elx_free_scsi_cmd(ELX_SCSI_BUF_t *);
uint32_t elx_os_timeout_transform(elxHBA_t *, uint32_t);
void elx_os_return_scsi_cmd(elxHBA_t *, ELX_SCSI_BUF_t *);
int elx_scsi_cmd_start(ELX_SCSI_BUF_t *);
int elx_scsi_prep_task_mgmt_cmd(elxHBA_t *, ELX_SCSI_BUF_t *, uint8_t);
int elx_scsi_cmd_abort(elxHBA_t *, ELX_SCSI_BUF_t *);
int elx_scsi_lun_reset(ELX_SCSI_BUF_t *, elxHBA_t *, uint32_t,
		       uint32_t, uint64_t, uint32_t);
int elx_scsi_tgt_reset(ELX_SCSI_BUF_t *, elxHBA_t *, uint32_t,
		       uint32_t, uint32_t);
int elx_scsi_hba_reset(elxHBA_t *, uint32_t);
void elx_qfull_retry(elxHBA_t *, void *, void *);
void elx_scsi_lower_lun_qthrottle(elxHBA_t *, ELX_SCSI_BUF_t *);

void elx_sched_init_hba(elxHBA_t *, uint16_t);
void elx_sched_target_init(ELXSCSITARGET_t *, uint16_t);
void elx_sched_lun_init(ELXSCSILUN_t *, uint16_t);
void elx_sched_submit_command(elxHBA_t *, ELX_SCSI_BUF_t *);
void elx_sched_queue_command(elxHBA_t *, ELX_SCSI_BUF_t *);
void elx_sched_add_target_to_ring(elxHBA_t *, ELXSCSITARGET_t *);
void elx_sched_remove_target_from_ring(elxHBA_t *, ELXSCSITARGET_t *);
void elx_sched_add_lun_to_ring(elxHBA_t *, ELXSCSILUN_t *);
void elx_sched_remove_lun_from_ring(elxHBA_t *, ELXSCSILUN_t *);
int elx_sli_issue_iocb_wait_high_priority(elxHBA_t * phba,
					  ELX_SLI_RING_t * pring,
					  ELX_IOCBQ_t * piocb, uint32_t flag,
					  ELX_IOCBQ_t * prspiocbq,
					  uint32_t timeout);
void elx_sched_service_high_priority_queue(struct elxHBA *hba);
void elx_sli_wake_iocb_high_priority(elxHBA_t * phba, ELX_IOCBQ_t * queue1,
				     ELX_IOCBQ_t * queue2);

#endif				/* _H_ELX_CRTN */
