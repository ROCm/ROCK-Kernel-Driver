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

#ifndef _H_LPFC_CRTN
#define _H_LPFC_CRTN

#include "elx_sli.h"
#include "elx_scsi.h"
#include "elx_logmsg.h"
#include "lpfc_ioctl.h"
#include "lpfc_ip.h"
#include "lpfc_diag.h"

/* For lpfc_mbox.c */
void lpfc_dump_mem(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_read_nv(elxHBA_t *, ELX_MBOXQ_t *);
int lpfc_read_la(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_clear_la(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_config_link(elxHBA_t *, ELX_MBOXQ_t *);
int lpfc_read_sparam(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_read_config(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_set_slim(elxHBA_t *, ELX_MBOXQ_t *, uint32_t, uint32_t);
void lpfc_config_farp(elxHBA_t *, ELX_MBOXQ_t *);
int lpfc_reg_login(elxHBA_t *, uint32_t, uint8_t *, ELX_MBOXQ_t *, uint32_t);
void lpfc_unreg_login(elxHBA_t *, uint32_t, ELX_MBOXQ_t *);
void lpfc_unreg_did(elxHBA_t *, uint32_t, ELX_MBOXQ_t *);
void lpfc_init_link(elxHBA_t *, ELX_MBOXQ_t *, uint32_t, uint32_t);
uint32_t *lpfc_config_pcb_setup(elxHBA_t *);
int lpfc_read_rpi(elxHBA_t *, uint32_t, ELX_MBOXQ_t *, uint32_t);

/* For lpfc_hbadisc.c */
int lpfc_linkdown(elxHBA_t *);
int lpfc_linkup(elxHBA_t *);
void lpfc_mbx_cmpl_read_la(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_config_link(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_read_sparam(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_clear_la(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_reg_login(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_fabric_reg_login(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_ns_reg_login(elxHBA_t *, ELX_MBOXQ_t *);
void lpfc_mbx_cmpl_fdmi_reg_login(elxHBA_t *, ELX_MBOXQ_t *);
int lpfc_nlp_bind(elxHBA_t *, LPFC_BINDLIST_t *);
int lpfc_nlp_plogi(elxHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_adisc(elxHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_unmapped(elxHBA_t *, LPFC_NODELIST_t *);
int lpfc_nlp_mapped(struct elxHBA *, LPFC_NODELIST_t *, LPFC_BINDLIST_t *);
void *lpfc_set_disctmo(elxHBA_t *);
int lpfc_can_disctmo(elxHBA_t *);
int lpfc_driver_abort(elxHBA_t *, LPFC_NODELIST_t *);
int lpfc_no_rpi(elxHBA_t *, LPFC_NODELIST_t *);
int lpfc_freenode(elxHBA_t *, LPFC_NODELIST_t *);
LPFC_NODELIST_t *lpfc_findnode_did(elxHBA_t *, uint32_t, uint32_t);
LPFC_NODELIST_t *lpfc_findnode_scsiid(elxHBA_t *, uint32_t);
LPFC_NODELIST_t *lpfc_findnode_wwpn(elxHBA_t *, uint32_t, NAME_TYPE *);
LPFC_NODELIST_t *lpfc_findnode_wwnn(elxHBA_t *, uint32_t, NAME_TYPE *);
void lpfc_disc_list_loopmap(elxHBA_t *);
void lpfc_disc_start(elxHBA_t *);
void lpfc_disc_flush_list(elxHBA_t *);
void lpfc_disc_timeout(elxHBA_t *, void *, void *);
void lpfc_linkdown_timeout(elxHBA_t *, void *, void *);
void lpfc_nodev_timeout(elxHBA_t *, void *, void *);
ELXSCSILUN_t *lpfc_find_lun(elxHBA_t *, uint32_t, uint64_t, int);
ELX_SCSI_BUF_t *lpfc_build_scsi_cmd(elxHBA_t *, LPFC_NODELIST_t *, uint32_t,
				    uint64_t);
int lpfc_disc_issue_rptlun(elxHBA_t *, LPFC_NODELIST_t *);
void lpfc_set_failmask(elxHBA_t *, LPFC_NODELIST_t *, uint32_t, uint32_t);

/* These functions implement node hash table hashed on RPIs */
LPFC_NODELIST_t *lpfc_findnode_rpi(elxHBA_t * phba, uint16_t rpi);
LPFC_NODELIST_t *lpfc_findnode_remove_rpi(elxHBA_t * phba, uint16_t rpi);
void lpfc_addnode_rpi(elxHBA_t * phba, LPFC_NODELIST_t * ndlp, uint16_t rpi);
LPFC_NODELIST_t *lpfc_removenode_rpihash(elxHBA_t * phba,
					 LPFC_NODELIST_t * ndlp);

/* For lpfc_nportdisc.c */
int lpfc_disc_state_machine(elxHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);
uint32_t lpfc_disc_nodev(elxHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);
uint32_t lpfc_disc_neverdev(elxHBA_t *, LPFC_NODELIST_t *, void *, uint32_t);
/* UNUSED_NODE state */
uint32_t lpfc_rcv_plogi_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_els_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_logo_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_els_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_device_rm_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_add_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_device_unk_unused_node(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
/* PLOGI_ISSUE state */
uint32_t lpfc_rcv_plogi_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prli_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_logo_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_els_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_cmpl_plogi_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_cmpl_prli_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_logo_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_adisc_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_device_rm_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_unk_plogi_issue(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
/* REG_LOGIN_ISSUE state */
uint32_t lpfc_rcv_plogi_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_rcv_prli_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				      void *, uint32_t);
uint32_t lpfc_rcv_logo_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				      void *, uint32_t);
uint32_t lpfc_rcv_padisc_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_rcv_prlo_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				      void *, uint32_t);
uint32_t lpfc_cmpl_plogi_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_cmpl_prli_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_cmpl_logo_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_cmpl_adisc_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_reglogin_issue(elxHBA_t *,
					   LPFC_NODELIST_t *, void *, uint32_t);
uint32_t lpfc_device_rm_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_device_unk_reglogin_issue(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
/* PRLI_ISSUE state */
uint32_t lpfc_rcv_plogi_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_prli_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_logo_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_padisc_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prlo_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_cmpl_plogi_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_prli_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_logo_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_adisc_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_device_rm_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_device_add_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_unk_prli_issue(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
/* PRLI_COMPL state */
uint32_t lpfc_rcv_plogi_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_prli_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_logo_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_rcv_padisc_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prlo_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				  void *, uint32_t);
uint32_t lpfc_cmpl_logo_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_adisc_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				       void *, uint32_t);
uint32_t lpfc_device_rm_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_device_add_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_unk_prli_compl(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
/* MAPPED_NODE state */
uint32_t lpfc_rcv_plogi_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_rcv_prli_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_logo_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_rcv_padisc_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_rcv_prlo_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				   void *, uint32_t);
uint32_t lpfc_cmpl_logo_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_cmpl_adisc_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_cmpl_reglogin_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
					void *, uint32_t);
uint32_t lpfc_device_rm_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				    void *, uint32_t);
uint32_t lpfc_device_add_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
uint32_t lpfc_device_unk_mapped_node(elxHBA_t *, LPFC_NODELIST_t *,
				     void *, uint32_t);
int lpfc_check_sparm(elxHBA_t *, LPFC_NODELIST_t *, SERV_PARM *, uint32_t);
int lpfc_geportname(NAME_TYPE *, NAME_TYPE *);
uint32_t lpfc_add_bind(elxHBA_t * phba, uint8_t bind_type,
		       void *bind_id, uint32_t scsi_id);
uint32_t lpfc_del_bind(elxHBA_t * phba, uint8_t bind_type,
		       void *bind_id, uint32_t scsi_id);

/* For lpfc_els.c */
int lpfc_initial_flogi(elxHBA_t *);
int lpfc_issue_els_flogi(elxHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_plogi(elxHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_prli(elxHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_adisc(elxHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_logo(elxHBA_t *, LPFC_NODELIST_t *, uint8_t);
int lpfc_issue_els_scr(elxHBA_t *, uint32_t, uint8_t);
int lpfc_issue_els_farp(elxHBA_t *, uint8_t *, LPFC_FARP_ADDR_TYPE);
int lpfc_issue_els_farpr(elxHBA_t *, uint32_t, uint8_t);
ELX_IOCBQ_t *lpfc_prep_els_iocb(elxHBA_t *, uint8_t expectRsp,
				uint16_t, uint8_t, LPFC_NODELIST_t *, uint32_t);
int lpfc_els_free_iocb(elxHBA_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_flogi(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_plogi(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_prli(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_adisc(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_logo(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_cmd(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_acc(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_els_logo_acc(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
int lpfc_els_rsp_acc(elxHBA_t *, uint32_t, ELX_IOCBQ_t *,
		     LPFC_NODELIST_t *, ELX_MBOXQ_t *);
int lpfc_els_rsp_reject(elxHBA_t *, uint32_t, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rsp_adisc_acc(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_rsp_prli_acc(elxHBA_t *, ELX_IOCBQ_t *, LPFC_NODELIST_t *);
int lpfc_els_retry(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_els_retry_delay(elxHBA_t *, void *, void *);
void lpfc_els_unsol_event(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
int lpfc_els_chk_latt(elxHBA_t *, ELX_IOCBQ_t *);
int lpfc_els_handle_rscn(elxHBA_t *);
void lpfc_more_adisc(elxHBA_t *);
void lpfc_more_plogi(elxHBA_t *);
int lpfc_els_flush_rscn(elxHBA_t *);
void lpfc_els_flush_cmd(elxHBA_t *);
int lpfc_rscn_payload_check(elxHBA_t *, uint32_t);
void lpfc_els_timeout_handler(elxHBA_t * phba, void *arg1, void *arg2);

/* For lpfc_ipport.c */
void lpfc_ip_unsol_event(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
LPFC_IP_BUF_t *lpfc_get_ip_buf(elxHBA_t *);
void lpfc_free_ip_buf(LPFC_IP_BUF_t *);
int lpfc_ip_post_buffer(elxHBA_t *, ELX_SLI_RING_t *, int);
int lpfc_ip_xmit(LPFC_IP_BUF_t *);
LPFC_NODELIST_t *lpfc_ip_find_device(LPFC_IP_BUF_t *, elxHBA_t **);
int lpfc_ip_create_xri(elxHBA_t *, LPFC_IP_BUF_t *, LPFC_NODELIST_t *);
void lpfc_ip_xri_cmpl(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
int lpfc_ip_flush_iocb(elxHBA_t *, ELX_SLI_RING_t *, LPFC_NODELIST_t *,
		       LPFC_IP_FLUSH_EVENT);
void lpfc_ipfarp_timeout(elxHBA_t *, void *, void *);
void lpfc_ip_xri_timeout(elxHBA_t *, void *, void *);

/* For lpfc_ct.c */
void lpfc_ct_unsol_event(elxHBA_t *, ELX_SLI_RING_t *, ELX_IOCBQ_t *);
int lpfc_ns_cmd(elxHBA_t *, LPFC_NODELIST_t *, int);
int lpfc_ct_cmd(elxHBA_t *, DMABUF_t *, DMABUF_t *,
		LPFC_NODELIST_t *, void (*cmpl) (struct elxHBA *,
						 ELX_IOCBQ_t *, ELX_IOCBQ_t *));
int lpfc_free_ct_rsp(elxHBA_t *, DMABUF_t *);
int lpfc_ns_rsp(elxHBA_t *, DMABUF_t *, uint32_t);
int lpfc_issue_ct_rsp(elxHBA_t *, uint32_t, DMABUF_t *, DMABUFEXT_t *);
int lpfc_gen_req(elxHBA_t *, DMABUF_t *, DMABUF_t *, DMABUF_t *,
		 void (*cmpl) (struct elxHBA *, ELX_IOCBQ_t *, ELX_IOCBQ_t *),
		 uint32_t, uint32_t, uint32_t, uint32_t);
void lpfc_cmpl_ct_cmd_gid_ft(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_ct_cmd_rft_id(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_ct_cmd_rnn_id(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_cmpl_ct_cmd_rsnn_nn(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
int lpfc_fdmi_cmd(elxHBA_t *, LPFC_NODELIST_t *, int);
void lpfc_cmpl_ct_cmd_fdmi(elxHBA_t *, ELX_IOCBQ_t *, ELX_IOCBQ_t *);
void lpfc_fdmi_tmo(elxHBA_t *, void *, void *);

/* For lpfc_init.c */
int lpfc_config_port_prep(elxHBA_t *);
int lpfc_config_port_post(elxHBA_t *);
int lpfc_hba_down_prep(elxHBA_t *);
void lpfc_handle_eratt(elxHBA_t *, uint32_t);
void lpfc_handle_latt(elxHBA_t *);
int lpfc_post_buffer(elxHBA_t *, ELX_SLI_RING_t *, int, int);
void lpfc_cleanup(elxHBA_t *, uint32_t);
int lpfc_online(elxHBA_t *);
int lpfc_offline(elxHBA_t *);
int lpfc_scsi_free(elxHBA_t *);
int lpfc_parse_binding_entry(elxHBA_t *, uint8_t *, uint8_t *,
			     int, int, int, unsigned int *, int, int *);

void fcptst(elxHBA_t *, void *, void *);
void iptst(elxHBA_t *, void *, void *);

/* For lpfc_ioctl.c */
int lpfc_diag_ioctl(elxHBA_t *, ELXCMDINPUT_t *);
void lpfc_decode_firmware_rev(elxHBA_t *, char *, int);
int lpfc_sleep(elxHBA_t *, fcEVTHDR_t *);
void lpfc_wakeup(elxHBA_t *, fcEVTHDR_t *);
int lpfc_read_flash(elxHBA_t * phba, ELXCMDINPUT_t * cip, uint8_t * data);
int lpfc_write_flash(elxHBA_t * phba, ELXCMDINPUT_t * cip, uint8_t * data);
uint8_t *lpfc_get_lpfchba_info(elxHBA_t *, uint8_t *);
int lpfc_fcp_abort(elxHBA_t *, int, int, int);
int dfc_put_event(elxHBA_t *, uint32_t, uint32_t, void *, void *);
int dfc_hba_put_event(elxHBA_t *, uint32_t, uint32_t, uint32_t, uint32_t,
		      uint32_t);
void lpfc_get_hba_model_desc(elxHBA_t *, uint8_t *, uint8_t *);
void lpfc_get_hba_SymbNodeName(elxHBA_t *, uint8_t *);

int lpfc_sli_setup(elxHBA_t *);
void lpfc_DELAYMS(elxHBA_t *, int);
void lpfc_slim_access(elxHBA_t *);
uint32_t lpfc_read_HA(elxHBA_t *);
uint32_t lpfc_read_CA(elxHBA_t *);
uint32_t lpfc_read_hbaregs_plus_offset(elxHBA_t *, uint32_t);
uint32_t lpfc_read_HS(elxHBA_t *);
uint32_t lpfc_read_HC(elxHBA_t *);
void lpfc_write_HA(elxHBA_t *, uint32_t);
void lpfc_write_CA(elxHBA_t *, uint32_t);
void lpfc_write_hbaregs_plus_offset(elxHBA_t *, uint32_t, uint32_t);
void lpfc_write_HS(elxHBA_t *, uint32_t);
void lpfc_write_HC(elxHBA_t *, uint32_t);
int lpfc_ip_prep_io(elxHBA_t *, LPFC_IP_BUF_t *);
char *lpfc_get_OsNameVersion(int);
int lpfc_utsname_nodename_check(void);
int lpfc_ip_unprep_io(elxHBA_t *, LPFC_IP_BUF_t *, uint32_t free_msg);
void lpfc_ip_timeout_handler(elxHBA_t *, void *, void *);
void *fc_get_cfg_param(int, int);
/* For lpfc_scsiport.c */
void lpfc_qthrottle_up(elxHBA_t *, void *, void *);
void lpfc_npr_timeout(elxHBA_t *, void *, void *);
int lpfc_scsi_hba_reset(elxHBA_t *, ELX_SCSI_BUF_t *);
void lpfc_scsi_issue_inqsn(elxHBA_t *, void *, void *);
void lpfc_scsi_issue_inqp0(elxHBA_t *, void *, void *);
void lpfc_scsi_timeout_handler(elxHBA_t *, void *, void *);

#endif				/* _H_LPFC_CRTN */
