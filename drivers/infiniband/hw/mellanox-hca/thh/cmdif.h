/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef __cmdif_h
#define __cmdif_h

#include <mtl_types.h>
#include <hh.h>
#include <cmd_types.h>
#include <tavor_if_defs.h>

/* #define SIMULATE_HALT_HCA  1 */

#define THH_CMDIF_INVALID_HANDLE 0xFFFFFFFF



/******************************************************************************
 *  Function: THH_cmd_create
 *
 *  Description: Create a THH_cmd object
 *
 *  Parameters:
 *    hw_ver(IN) hardware version
 *    cr_base(IN) physical address of the cr-space
 *    uar0_base(IN) base address of uar0 for posting commands
 *    cmd_if_p(OUT) handle to create THH_cmd_t object
 *    inf_timeout(IN) when TRUE the cmdif will wait infinitely for the completion of a command
 *    num_cmds_outs(IN) number of commands in the air requested by the user (the real
 *                      value may be less depending on the value returned by FW)
 *
 *  Returns:
 *
 *****************************************************************************/
HH_ret_t THH_cmd_create(THH_hob_t hob, u_int32_t hw_ver, MT_phys_addr_t cr_base, MT_phys_addr_t uar0_base, THH_cmd_t *cmd_if_p,
                        MT_bool inf_timeout, u_int32_t num_cmds_outs);


/******************************************************************************
 *  Function: THH_cmd_destroy
 *
 *  Description: Destroy the THH_cmd object
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *
 *  Returns:
 *
 *****************************************************************************/
HH_ret_t THH_cmd_destroy(THH_cmd_t cmd_if);



/******************************************************************************
 *  Function: THH_cmd_set_fw_props
 *
 *  Description: set params set by QUERY_FW command
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *    fw_props_p(IN) pointer to queried fw props
 *
 *  Returns:
 *
 *****************************************************************************/
THH_cmd_status_t THH_cmd_set_fw_props(THH_cmd_t cmd_if, THH_fw_props_t *fw_props);

/******************************************************************************
 *  Function: THH_cmd_set_eq
 *
 *  Description: Enable the command interfcae object to work with events
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *
 *  Returns:
 *
 *****************************************************************************/
HH_ret_t THH_cmd_set_eq(THH_cmd_t cmd_if);


/******************************************************************************
 *  Function: THH_cmd_clr_eq
 *
 *  Description: inform the object to stop reporting completions to the EQ
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *
 *  Returns:
 *
 *****************************************************************************/
HH_ret_t THH_cmd_clr_eq(THH_cmd_t cmd_if);


/******************************************************************************
 *  Function: THH_cmd_eventh
 *
 *  Description: This function is called whenever a command was completed (when
 *               working with events)
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *    result_hcr_image_p(IN) - the (big-endian) HCR image of the command completion EQE
 *
 *  Returns:
 *
 *****************************************************************************/
void THH_cmd_eventh(THH_cmd_t cmd_if, u_int32_t *result_hcr_image_p);


/******************************************************************************
 *  Function: THH_cmd_assign_ddrmm
 *
 *  Description: Assign memory manager to be used by this object for allocating
 *               memory frm DDR
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *    ddrmm(IN) handle of assigned ddr memory manager
 *
 *  Returns:
 *
 *****************************************************************************/
HH_ret_t THH_cmd_assign_ddrmm(THH_cmd_t cmd_if, THH_ddrmm_t ddrmm);


/******************************************************************************
 *  Function: THH_cmd_revoke_ddrmm
 *
 *  Description: revoke the associated memory manager
 *
 *  Parameters:
 *    cmd_if(IN) handle of the THH_cmd_t object
 *
 *  Returns:
 *
 *****************************************************************************/
HH_ret_t THH_cmd_revoke_ddrmm(THH_cmd_t cmd_if);


/*****************************************************************************
*                                                                            *
*                           COMMAND FUNCTIONS                                *
*                                                                            *
******************************************************************************/

#ifdef IN
#undef IN
#endif
#define IN


#ifdef OUT
#undef OUT
#endif
#define OUT

/* System commands */
THH_cmd_status_t THH_cmd_SYS_EN(IN THH_cmd_t cmd_if);
THH_cmd_status_t THH_cmd_SYS_DIS(IN THH_cmd_t cmd_if);

/* General queries */
THH_cmd_status_t THH_cmd_QUERY_DEV_LIM(IN THH_cmd_t cmd_if, OUT THH_dev_lim_t *dev_lim);
THH_cmd_status_t THH_cmd_QUERY_FW(IN THH_cmd_t cmd_if, OUT THH_fw_props_t *fw_props);
THH_cmd_status_t THH_cmd_QUERY_DDR(IN THH_cmd_t cmd_if, OUT THH_ddr_props_t *ddr_props);
THH_cmd_status_t THH_cmd_QUERY_ADAPTER(IN THH_cmd_t cmd_if, OUT THH_adapter_props_t *adapter_props);

/* HCA initialization and maintenance commands */
THH_cmd_status_t THH_cmd_INIT_HCA(IN THH_cmd_t cmd_if, IN THH_hca_props_t *hca_props);
THH_cmd_status_t THH_cmd_INIT_IB(IN THH_cmd_t cmd_if, IN IB_port_t port,
                                 IN THH_port_init_props_t *port_init_props);
THH_cmd_status_t THH_cmd_QUERY_HCA(IN THH_cmd_t cmd_if, OUT THH_hca_props_t *hca_props);
THH_cmd_status_t THH_cmd_SET_IB(IN THH_cmd_t cmd_if, IN IB_port_t port,
                                    IN THH_set_ib_props_t *port_props);
THH_cmd_status_t THH_cmd_CLOSE_IB(IN THH_cmd_t cmd_if, IN IB_port_t port);
#ifdef SIMULATE_HALT_HCA
THH_cmd_status_t THH_cmd_CLOSE_HCA(IN THH_cmd_t cmd_if);
#else
THH_cmd_status_t THH_cmd_CLOSE_HCA(IN THH_cmd_t cmd_if, MT_bool do_halt);
#endif
/* TPT commands */
THH_cmd_status_t THH_cmd_SW2HW_MPT(IN THH_cmd_t cmd_if, IN THH_mpt_index_t mpt_index,
                                   IN THH_mpt_entry_t *mpt_entry);
THH_cmd_status_t THH_cmd_QUERY_MPT(IN THH_cmd_t cmd_if, IN THH_mpt_index_t mpt_index,
                                   OUT THH_mpt_entry_t *mpt_entry);
THH_cmd_status_t THH_cmd_HW2SW_MPT(IN THH_cmd_t cmd_if, IN THH_mpt_index_t mpt_index,
                                   OUT THH_mpt_entry_t *mpt_entry);
THH_cmd_status_t THH_cmd_READ_MTT(IN THH_cmd_t cmd_if, IN u_int64_t mtt_pa, IN MT_size_t num_elems,
                                  OUT THH_mtt_entry_t *mtt_entry);
THH_cmd_status_t THH_cmd_WRITE_MTT(IN THH_cmd_t cmd_if, IN u_int64_t mtt_pa,  IN MT_size_t num_elems,
                                   IN THH_mtt_entry_t *mtt_entry);
THH_cmd_status_t THH_cmd_SYNC_TPT(IN THH_cmd_t cmd_if);

/* EQ commands */
THH_cmd_status_t THH_cmd_MAP_EQ(IN THH_cmd_t cmd_if, IN THH_eqn_t eqn, IN u_int64_t event_mask);
THH_cmd_status_t THH_cmd_SW2HW_EQ(IN THH_cmd_t cmd_if, IN THH_eqn_t eqn, IN THH_eqc_t *eq_context);
THH_cmd_status_t THH_cmd_HW2SW_EQ(IN THH_cmd_t cmd_if, IN THH_eqn_t eqn, OUT THH_eqc_t *eq_context);
THH_cmd_status_t THH_cmd_QUERY_EQ(IN THH_cmd_t cmd_if, IN THH_eqn_t eqn, OUT THH_eqc_t *eq_context);


/* CQ commands */
THH_cmd_status_t THH_cmd_SW2HW_CQ(IN THH_cmd_t cmd_if, IN HH_cq_hndl_t cqn,
                                  IN THH_cqc_t *cq_context);
THH_cmd_status_t THH_cmd_HW2SW_CQ(IN THH_cmd_t cmd_if, IN HH_cq_hndl_t cqn,
                                  OUT THH_cqc_t *cq_context);
THH_cmd_status_t THH_cmd_QUERY_CQ(IN THH_cmd_t cmd_if, IN HH_cq_hndl_t cqn,
                                  IN THH_cqc_t *cq_context);
THH_cmd_status_t THH_cmd_RESIZE_CQ(THH_cmd_t cmd_if, HH_cq_hndl_t cqn, 
                                   u_int64_t start_address, u_int32_t l_key, u_int8_t log_cq_size,
                                   u_int32_t *new_producer_index_p);
/* if given new_producer_index_p==NULL then opcode_modifier=1 (fixed resize CQ - FM issue #17002) */

/* QP/EE commands */
THH_cmd_status_t THH_cmd_MODIFY_QP(IN THH_cmd_t cmd_if, IN IB_wqpn_t qpn,
                                   IN THH_qpee_transition_t trans,
                                   IN THH_qpee_context_t *qp_context,
                                   IN u_int32_t           optparammask);
THH_cmd_status_t THH_cmd_MODIFY_EE(IN THH_cmd_t cmd_if, IN IB_eecn_t eecn,
                                   IN THH_qpee_transition_t trans,
                                   IN THH_qpee_context_t *ee_context,
                                   IN u_int32_t           optparammask);
THH_cmd_status_t THH_cmd_QUERY_QP(IN THH_cmd_t cmd_if, IN IB_wqpn_t qpn,
                                  OUT THH_qpee_context_t *qp_context);
THH_cmd_status_t THH_cmd_QUERY_EE(IN THH_cmd_t cmd_if, IN IB_eecn_t eecn,
                                  OUT THH_qpee_context_t *ee_context);
#if defined(MT_SUSPEND_QP)
THH_cmd_status_t THH_cmd_SUSPEND_QP(THH_cmd_t cmd_if,  u_int32_t qpn, MT_bool suspend_flag);
#endif

/* Special QP commands */
THH_cmd_status_t THH_cmd_CONF_SPECIAL_QP(IN THH_cmd_t cmd_if, IN VAPI_special_qp_t qp_type,
                                         IN IB_wqpn_t base_qpn);
THH_cmd_status_t THH_cmd_MAD_IFC(IN THH_cmd_t cmd_if, 
                                 IN MT_bool mkey_validate, 
                                 IN IB_lid_t slid, /* SLID is ignored if mkey_validate is false */
                                 IN IB_port_t port,
                                 IN void *mad_in, 
                                 OUT void *mad_out);

/* SRQ commands */

THH_cmd_status_t THH_cmd_SW2HW_SRQ(IN THH_cmd_t cmd_if,
                                   IN u_int32_t srqn,         /* SRQ number/index */
                                   IN THH_srq_context_t *srqc_p);/* SRQ context   */

THH_cmd_status_t THH_cmd_HW2SW_SRQ(IN THH_cmd_t cmd_if,
                                   IN u_int32_t srqn,          /* SRQ number/index */
                               OUT THH_srq_context_t *srqc_p);/* SRQ context (NULL for no output)*/

THH_cmd_status_t THH_cmd_QUERY_SRQ(IN THH_cmd_t cmd_if,
                                   IN u_int32_t srqn,          /* SRQ number/index */
                                   OUT THH_srq_context_t *srqc_p);/* SRQ context   */

/* Multicast groups commands */
THH_cmd_status_t THH_cmd_READ_MGM(IN THH_cmd_t cmd_if, IN u_int32_t mcg_index,
                                  IN MT_size_t  max_qp_per_mcg, OUT THH_mcg_entry_t *mcg_entry);
THH_cmd_status_t THH_cmd_WRITE_MGM(IN THH_cmd_t cmd_if, IN u_int32_t mcg_index,
                                   IN MT_size_t  max_qp_per_mcg, IN THH_mcg_entry_t *mcg_entry);
THH_cmd_status_t THH_cmd_MGID_HASH(IN THH_cmd_t cmd_if, IN IB_gid_t mgid, OUT THH_mcg_hash_t *hash_val);

/* fatal error notification */
THH_cmd_status_t THH_cmd_notify_fatal(IN THH_cmd_t cmd_if, IN THH_fatal_err_t fatal_err);
THH_cmd_status_t THH_cmd_handle_fatal(IN THH_cmd_t cmd_if);

const char *str_THH_cmd_status_t(THH_cmd_status_t status);
const char *cmd_str(tavor_if_cmd_t opcode);

#undef IN
#undef OUT

#endif /* __cmdif_h */
