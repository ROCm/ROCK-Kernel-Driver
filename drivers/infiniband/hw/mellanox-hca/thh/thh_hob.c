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
 
#include <mtl_common.h>
#include <cr_types.h>
#include <MT23108_PRM.h>
#include <vapi.h>
#include <vapi_common.h>
#include <hh.h>
#include <thh_hob_priv.h>
#include <sm_mad.h>
#include <tlog2.h>
#include <thh_default_profile.h>
#include <thhul_pdm.h>
#include <thh_srqm.h>

#define TEST_RETURN_FATAL(hob)  if ((hob->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {          \
                           MTL_ERROR1(MT_FLFMT("%s: Device in FATAL state"), __func__);  \
                           return HH_EFATAL;                               \
                           }
#define TEST_CMD_FATAL(ret)  if (ret == THH_CMD_STAT_EFATAL) {                          \
                           MTL_ERROR1(MT_FLFMT("%s: cmdif returned FATAL"), __func__);  \
                           return HH_EFATAL;                                            \
                           }
#define FREE_RSRC_RET(hob)    rc = ((have_fatal == TRUE) ? HH_OK : rc) ; return rc
#define DECLARE_FATAL_VARS    MT_bool  have_fatal=FALSE
#define WAIT_IF_FATAL(hob)    THH_hob_wait_if_fatal(hob,&have_fatal);
#define IB_MAX_MESSAGE_SIZE          (1 << 31)
#define CMD_EQ_SIZE 250 /* to be smaller then 8K - so it's EQ can get into physical memory */

#if 4 <= MAX_DEBUG
#define THH_PRINT_PROFILE(a)      THH_print_profile(a)
#define THH_PRINT_USR_PROFILE(a)  THH_print_usr_profile(a)
#else
#define THH_PRINT_PROFILE(a)      
#define THH_PRINT_USR_PROFILE(a)
#endif

/* global reference here for cmdif when putting Outbox in DDR memory*/
#ifdef DEBUG_MEM_OV
#define CMDIF_SIZE_IN_DDR   0x100000    /* allocate 1M in DDR memory*/ 
MT_phys_addr_t cmdif_dbg_ddr; /* address in ddr used for out params in debug mode */
#endif


#define THH_WATERMARK 1024 /* 1K*/

#define GET_DDR_ADDR(phys_addr,hide_ddr,ddr_base)   ((u_int64_t) ((((hide_ddr) == TRUE)&&(sizeof(MT_phys_addr_t)<=4)) ? \
                                  (((u_int64_t)(phys_addr)) | ((ddr_base) & MAKE_ULONGLONG(0xFFFFFFFF00000000))) : phys_addr))
                                                      
static HH_ret_t THH_hob_query_struct_init(THH_hob_t  hob, MT_bool have_usr_profile, VAPI_hca_cap_t *hca_cap_p);
static HH_ret_t   THH_hob_halt_hca(/*IN*/ THH_hob_t hob);


static void THH_dummy_async_event(HH_hca_hndl_t hca_hndl, HH_event_record_t  *event_rec_p, void* ptr)
{
  /* TBD : This should be an error */
  MTL_TRACE1("THH_dummy_async_event: called for devices %s with event type 0x%x",
             hca_hndl->dev_desc, event_rec_p->etype);
  return;
}

static void THH_dummy_comp_event(HH_hca_hndl_t hca_hndl, HH_cq_hndl_t cq_num, void* ptr)
{
  /* TBD : This should be an error */
  MTL_TRACE1("THH_dummy_comp_event: called for device %s and  cq num 0x%x",
                          hca_hndl->dev_desc, cq_num);
  return;
}

int THH_hob_fatal_err_thread(void  *arg);

/******************************************************************************
******************************************************************************
************************  INTERNAL FUNCTIONS *********************************
******************************************************************************
******************************************************************************/
#if 4 <= MAX_DEBUG
static void THH_print_profile(THH_profile_t *profile) 
{
    MTL_DEBUG1("Profile printout\n");

    MTL_DEBUG1("        ddr_alloc_vec_size = "SIZE_T_DFMT"\n",profile->ddr_alloc_vec_size);
    MTL_DEBUG1("        ddr_size = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->ddr_size,profile->ddr_size );

    MTL_DEBUG1("        ddr_size_code = %d\n", profile->ddr_size_code);
    
    MTL_DEBUG1("        num_external_mem_regions = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->num_external_mem_regions,profile->num_external_mem_regions );
    MTL_DEBUG1("        num_mem_windows = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->num_mem_windows,profile->num_mem_windows);

    MTL_DEBUG1("        log2_max_qps = "SIZE_T_DFMT"\n", profile->log2_max_qps);
    MTL_DEBUG1("        max_num_qps = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->max_num_qps,profile->max_num_qps);
    MTL_DEBUG1("        log2_max_cqs = "SIZE_T_DFMT"\n", profile->log2_max_cqs);
    MTL_DEBUG1("        max_num_cqs = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->max_num_cqs,profile->max_num_cqs);
    MTL_DEBUG1("        max_num_pds = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->max_num_pds,profile->max_num_pds);

    MTL_DEBUG1("        log2_max_mpt_entries = "SIZE_T_DFMT"\n", profile->log2_max_mpt_entries);
    MTL_DEBUG1("        log2_max_mtt_entries = "SIZE_T_DFMT"\n", profile->log2_max_mtt_entries);
    MTL_DEBUG1("        log2_mtt_segs_per_region = "SIZE_T_DFMT"\n", profile->log2_mtt_segs_per_region);
    MTL_DEBUG1("        log2_mtt_entries_per_seg = "SIZE_T_DFMT"\n", profile->log2_mtt_entries_per_seg);

    
    MTL_DEBUG1("        log2_max_uar = "SIZE_T_DFMT"\n", profile->log2_max_uar);
    MTL_DEBUG1("        log2_uar_pg_size = %d\n", profile->log2_uar_pg_size);
    
    MTL_DEBUG1("        log2_wqe_ddr_space_per_qp = "SIZE_T_DFMT"\n",profile->log2_wqe_ddr_space_per_qp);

    MTL_DEBUG1("        use_priv_udav  = %s\n", (profile->use_priv_udav ? "TRUE" : "FALSE"));
    MTL_DEBUG1("        max_priv_udavs = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
               profile->max_priv_udavs,profile->max_priv_udavs);
    
    MTL_DEBUG1("        log2_max_mcgs = "SIZE_T_DFMT"\n", profile->log2_max_mcgs);
    MTL_DEBUG1("        qps_per_mcg = "SIZE_T_DFMT"\n", profile->qps_per_mcg);
    MTL_DEBUG1("        log2_mcg_entry_size = "SIZE_T_DFMT"\n", profile->log2_mcg_entry_size);
    MTL_DEBUG1("        log2_mcg_hash_size = "SIZE_T_DFMT"\n",profile->log2_mcg_hash_size);


    MTL_DEBUG1("        log2_max_eecs = "SIZE_T_DFMT"\n",profile->log2_max_eecs);

    MTL_DEBUG1("        log2_max_eqs = %d\n", profile->log2_max_eqs);
	return;
}

static void THH_print_usr_profile(EVAPI_hca_profile_t *profile) 
{
    MTL_DEBUG1("User Profile printout\n");

    MTL_DEBUG1("        num_qp = %d\n",profile->num_qp);
    MTL_DEBUG1("        num_cq = %d\n",profile->num_cq);
    MTL_DEBUG1("        num_pd = %d\n",profile->num_pd);
    MTL_DEBUG1("        num_mr = %d\n",profile->num_mr);
    MTL_DEBUG1("        num_mw = %d\n",profile->num_mw);
    MTL_DEBUG1("        max_qp_ous_rd_atom = %d\n",profile->max_qp_ous_rd_atom);
    MTL_DEBUG1("        max_mcg = %d\n",profile->max_mcg);
    MTL_DEBUG1("        qp_per_mcg = %d\n",profile->qp_per_mcg);
    MTL_DEBUG1("        require = %s\n",(profile->require == 0) ? "FALSE" : "TRUE");
	return;
}
#endif

static const char* THH_get_ddr_allocation_string (u_int32_t index)
{
    switch(index) {
        case 0: return "mtt sz";
        case 1: return "mpt sz";
        case 2: return "qpc sz";
        case 3: return "eqpc sz";
        case 4: return "srqc sz";
        case 5: return "cqc sz";
        case 6: return "rdb sz";
        case 7: return "uar scratch sz";
        case 8: return "eqc sz";
        case 9: return "mcg sz";
        case 10: return "eec sz";
        case 11: return "eeec sz";
    #if 0
        case 12: return "wqe pool sz";
        case 13: return "uplink qp sz";
        case 14: return "uplink mem sz";
    #endif
        default: return "UNKNOWN";

    }
}


/*
 * THH_get_ddr_size_code
 *
 */
static void THH_print_hw_props(THH_hw_props_t   *hw_props_p) 
{
    MTL_DEBUG4("%s:        cr_base = " PHYS_ADDR_FMT "\n", __func__, hw_props_p->cr_base);
//    MTL_DEBUG4("%s:        ddr_base = " PHYS_ADDR_FMT "\n", __func__hw_props_p->ddr_base);
    MTL_DEBUG4("%s:        uar_base = " PHYS_ADDR_FMT "\n", __func__, hw_props_p->uar_base);
    MTL_DEBUG4("%s:        device_id = 0x%x\n", __func__, hw_props_p->device_id);
    MTL_DEBUG4("%s:        pci_vendor_id = 0x%x\n", __func__, hw_props_p->pci_vendor_id);
    MTL_DEBUG4("%s:        intr_pin = 0x%x\n", __func__, hw_props_p->interrupt_props.intr_pin);
#ifndef __DARWIN__
    //MOSAL_IRQ_ID_t does not have to be integer
    MTL_DEBUG4("%s:        irq = 0x%x\n", __func__, hw_props_p->interrupt_props.irq);
#endif  /* not defined __DARWIN__ */
    MTL_DEBUG4("%s:        bus = %d\n", __func__, hw_props_p->bus);
    MTL_DEBUG4("%s:        dev_func = 0x%x\n", __func__, hw_props_p->dev_func);
}

/*
 * THH_get_ddr_size_code
 *
 */
static THH_ddr_size_enum_t THH_get_ddr_size_code(MT_size_t    ddr_size) 
{
    MTL_DEBUG4("THH_get_ddr_size_code: ddr size = "SIZE_T_FMT"\n", ddr_size);
    if (ddr_size < (1UL<<25)) {
        return THH_DDR_SIZE_32M;
    } else if (ddr_size < (1UL<<26)) {
        return THH_DDR_SIZE_64M;
    } else if (ddr_size < (1UL<<27)) {
        return THH_DDR_SIZE_128M;
    } else if (ddr_size < (1UL<<28)) {
        return THH_DDR_SIZE_256M;
    } else if (ddr_size < (1UL<<29)) {
        return THH_DDR_SIZE_512M;
    } else if (ddr_size < (1UL<<30)) {
        return THH_DDR_SIZE_1024M;
    } else if (ddr_size < (1UL<<31)) {
        return THH_DDR_SIZE_2048M;
    } else if (ddr_size < 0xFFFFFFFF) {
        return THH_DDR_SIZE_4096M;
    } else {
        return THH_DDR_SIZE_BIG;
    }
}
#ifndef __DARWIN__
//TODO: all this code is OS dependent!
//Must work with the pointer to PCI device

/*  
 *  read_pci_config -- reads all configuration registers for given device
 *  except for skipping regs 22 and 23
 */
static HH_ret_t read_pci_config(MOSAL_pci_dev_t pci_dev, u_int32_t *config) 
{
    u_int8_t offset = 0;
    HH_ret_t  rc = HH_OK;

    for (offset = 0; offset < 64; offset += 4) {
      if (offset == 22 || offset == 23) {
          continue;
      }
      rc = MOSAL_PCI_read_config(pci_dev, offset, 4, config);
      if (rc != MT_OK) {
        return HH_ERR;
      }
      config++;
    }
    return HH_OK;
}
static HH_ret_t write_pci_config(MOSAL_pci_dev_t pci_dev, u_int32_t *config) 
{
    u_int8_t offset = 0;
    HH_ret_t  rc = HH_OK;

    for (offset = 0; offset < 64; offset += 4) {
      if (offset == 22 || offset == 23) {
          continue;
      }
      rc = MOSAL_PCI_write_config(pci_dev, offset, 4, *config);
      if (rc != MT_OK) {
          return HH_ERR;
      }
      config++;
    }
    return HH_OK;
}
/******************************************************************************
 *  Function:     THH_hob_get_pci_br_config
 *
 *  Description:  Gets p2p bridge configuration for this hca's bridge  
 *
 *  input:
 *                hob
 *  output: 
 *                ack_timeout_p 
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. This function is used
 *                in pre-calculation of VAPI_query_hca values (at open_hca time).
 *
 *****************************************************************************/
static HH_ret_t  THH_hob_get_pci_br_config(THH_hob_t  hob)
{
    call_result_t  rc;
    u_int16_t      index=0;
    u_int8_t       bus;
    u_int8_t       dev_func;
    MOSAL_PCI_cfg_hdr_t cfg_hdr;
    MOSAL_pci_dev_t pci_dev = NULL;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    /* scan all bridges to find mellanox pci bridge which belongs to this hca */
    while (TRUE) {
      /*1. find device */
      rc = MOSAL_PCI_find_dev(hob->hw_props.pci_vendor_id, (hob->hw_props.device_id)+2,
                              pci_dev, &pci_dev, &bus, &dev_func);

      index++;
      if (rc != MT_OK) {
        MTL_DEBUG4(MT_FLFMT("%s: No more InfiniBridges."), __func__); 
        break;
      }
      MTL_DEBUG4(MT_FLFMT("%s: InfiniBridge %d: pci_find_device returned: bus=%d, dev_func=%d"), 
                 __func__, index, bus, dev_func);
      
      /*2. get pci header */
      rc = MOSAL_PCI_get_cfg_hdr(pci_dev, &cfg_hdr);
      if (rc != MT_OK) {
        MTL_ERROR4(MT_FLFMT("%s: Could not get header for device bus %d, dev_func 0x%x"),
                   __func__, bus, dev_func); 
        continue;
      }

      if ((cfg_hdr.type1.header_type & 0x7F) != MOSAL_PCI_HEADER_TYPE1) {
        MTL_DEBUG1(MT_FLFMT("%s: Wrong PCI header type (0x%02X). Should be type 1. Device ignored."),
                   __func__, cfg_hdr.type0.header_type);
        continue; 
      }

      /*3. check if this is our bridge */
      if (cfg_hdr.type1.sec_bus != hob->hw_props.bus) {
        MTL_DEBUG1(MT_FLFMT("%s: Not our bridge. bus = %d, dev_num=%d"),
                   __func__, bus, dev_func );
        continue; 
      }

      /* found our bridge.  Read and save its configuration */
      MTL_DEBUG1(MT_FLFMT("%s: found bridge. bus = %d, dev_num=%d"),
                 __func__, bus, dev_func );
      if (read_pci_config(pci_dev, hob->pci_bridge_info.config) != MT_OK) {
          return (HH_ERR);
      } else {
          hob->pci_bridge_info.bus = bus;
          hob->pci_bridge_info.dev_func = dev_func;
          hob->pci_bridge_info.is_valid = TRUE;
          hob->pci_bridge_info.pci_dev = pci_dev;
          return HH_OK;
      }
    }

    return HH_ERR; // did not find bridge
}
#endif /* not defined __DARWIN__ */

/******************************************************************************
 *  Function:     THH_hob_get_node_guid
 *
 *  Description:  Gets node GUID for this HCA.  
 *
 *  input:
 *                hob
 *                port_num - 1 or 2
 *  output: 
 *                node_guid - pointer to a GUID structure
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. This function is used
 *                in pre-calculation of VAPI_query_hca values (at open_hca time).
 *
 *****************************************************************************/
static HH_ret_t THH_hob_get_node_guid(THH_hob_t  hob,
                                     IB_guid_t   *node_guid)
{
  SM_MAD_NodeInfo_t  node_info;
  u_int8_t       *mad_frame_in;
  u_int8_t       *mad_frame_out;
  THH_cmd_status_t  cmd_ret;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  MTL_DEBUG4("==> THH_hob_get_node_guid\n");
  TEST_RETURN_FATAL(hob);

  mad_frame_in = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_in ) {
    return HH_EAGAIN;
  }
  mad_frame_out = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_out ) {
    FREE(mad_frame_in);
    return HH_EAGAIN;
  }
  memset(mad_frame_in, 0, sizeof(mad_frame_in));
  memset(mad_frame_out, 0, sizeof(mad_frame_out));

  /* get PKey table using MAD commands in THH_cmd object */
  /* First, build the MAD header */
  MADHeaderBuild(IB_CLASS_SMP, 
                    0,
                    IB_METHOD_GET,
                    IB_SMP_ATTRIB_NODEINFO,
                    (u_int32_t)   0,
                    &(mad_frame_in[0]));

  /* issue the query */
  cmd_ret = THH_cmd_MAD_IFC(hob->cmd, 0, 0, 1, &(mad_frame_in[0]), &(mad_frame_out[0]));
  if (cmd_ret != THH_CMD_STAT_OK) {
      TEST_CMD_FATAL(cmd_ret);
      MTL_ERROR2( "THH_hob_get_pkey_tbl: ERROR : Get Node Info command failed (%d) for port 1\n", cmd_ret);
      MTL_DEBUG4("<== THH_hob_get_node_guid. ERROR\n");
      FREE(mad_frame_out);
      FREE(mad_frame_in);
      return HH_EINVAL;
  }
  MadBufPrint(&(mad_frame_out[0]));
  NodeInfoMADToSt(&node_info, &(mad_frame_out[0]));
  NodeInfoPrint(&node_info);
  
//  guid = node_info.qwNodeGUID;
//  MTL_DEBUG4("THH_hob_get_node_guid: Node GUID = 0x%Lx\n", guid);
//  for (i = 7; i >= 0 ; --i) {
//     (*node_guid)[i] = (u_int8_t) (guid & 0x0FF);
//     guid >>= 8;
//  }
  memcpy((*node_guid), node_info.qwNodeGUID, sizeof(IB_guid_t));
  MTL_DEBUG4("<== THH_hob_get_node_guid\n");
  FREE(mad_frame_out);
  FREE(mad_frame_in);
  return HH_OK;
}

/* now obtained from DEV_LIMS */
#if 0
/******************************************************************************
 *  Function:     THH_hob_get_ack_timeout
 *
 *  Description:  Gets ack timeout for this HCA.  
 *
 *  input:
 *                hob
 *  output: 
 *                ack_timeout_p 
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. This function is used
 *                in pre-calculation of VAPI_query_hca values (at open_hca time).
 *
 *****************************************************************************/
static HH_ret_t  THH_hob_get_ack_timeout(
  THH_hob_t  hob,
  u_int8_t*  ack_timeout_p)
{
  SM_MAD_PortInfo_t  port_info;
  u_int8_t       *mad_frame_in;
  u_int8_t       *mad_frame_out;
  THH_cmd_status_t  cmd_ret;

  MTL_DEBUG4("ENTERING THH_hob_get_ack_timeout\n");

  mad_frame_in = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_in ) {
    return HH_EAGAIN;
  }
  mad_frame_out = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_out ) {
    FREE(mad_frame_in);
    return HH_EAGAIN;
  }
  memset(mad_frame_in, 0, sizeof(mad_frame_in));
  memset(mad_frame_out, 0, sizeof(mad_frame_out));

  /* get PortInfo for port 12 (first port) */
  /* First, build the MAD header */

  MADHeaderBuild(IB_CLASS_SMP, 
                    0,
                    IB_METHOD_GET,
                    IB_SMP_ATTRIB_PORTINFO,
                    (u_int32_t)   1,
                    &(mad_frame_in[0]));

  /* issue the query */
  cmd_ret = THH_cmd_MAD_IFC(hob->cmd, 0, 0, 1, &(mad_frame_in[0]), &(mad_frame_out[0]));
  if (cmd_ret != THH_CMD_STAT_OK) {
      TEST_CMD_FATAL(cmd_ret);
      MTL_ERROR2( "THH_hob_get_ack_timeout: ERROR : Get Port Info command failed (%d) for port 1\n", cmd_ret);
      FREE(mad_frame_out);
      FREE(mad_frame_in);
      return HH_ERR;
  }
  PortInfoMADToSt(&port_info, &(mad_frame_out[0]));
  PortInfoPrint(&port_info);

  *ack_timeout_p = port_info.cRespTimeValue;
  FREE(mad_frame_out);
  FREE(mad_frame_in);
  return HH_OK;
}
#endif

/******************************************************************************
 *  Function:     calculate_ddr_alloc_vec
 *
 *  Description:  Calculates sizes for DDR area allocation from profile
 *
 *  input:
 *                hob
 *                profile        -- pointer to data structure containing computation input
 *  output: 
 *                alloc_size_vec -- pointer to vector of allocation sizes to compute
 *                
 *  returns:
 *                HH_OK
 *
 *
 *****************************************************************************/
static void calculate_ddr_alloc_vec(/*IN*/ THH_hob_t     hob,
                                            /*IN*/ THH_profile_t *profile,  
                                            /*OUT*/THH_ddr_allocation_vector_t *alloc_size_vec)
{

    alloc_size_vec->log2_mtt_size = profile->log2_max_mtt_entries + THH_DDR_LOG2_MTT_ENTRY_SIZE; 
    alloc_size_vec->log2_mpt_size = profile->log2_max_mpt_entries + THH_DDR_LOG2_MPT_ENTRY_SIZE;   
    alloc_size_vec->log2_qpc_size = profile->log2_max_qps + THH_DDR_LOG2_QPC_ENTRY_SIZE;
    alloc_size_vec->log2_eqpc_size = profile->log2_max_qps + THH_DDR_LOG2_EQPC_ENTRY_SIZE; 
    alloc_size_vec->log2_srqc_size = hob->dev_lims.srq ? 
      profile->log2_max_srqs + THH_DDR_LOG2_SRQC_ENTRY_SIZE : THH_DDRMM_INVALID_SZ;
    alloc_size_vec->log2_cqc_size = profile->log2_max_cqs + THH_DDR_LOG2_CQC_ENTRY_SIZE;
    alloc_size_vec->log2_rdb_size = profile->log2_max_qps + profile->log2_inflight_rdma_per_qp + 
                                                        THH_DDR_LOG2_RDB_ENTRY_SIZE;
    alloc_size_vec->log2_uar_scratch_size = profile->log2_max_uar + THH_DDR_LOG2_UAR_SCR_ENTRY_SIZE;  
    alloc_size_vec->log2_eqc_size = profile->log2_max_eqs + THH_DDR_LOG2_EQC_ENTRY_SIZE; 
    if (THH_DEV_LIM_MCG_ENABLED(hob)) {
         alloc_size_vec->log2_mcg_size = profile->log2_max_mcgs + profile->log2_mcg_entry_size;
    } else {
        alloc_size_vec->log2_mcg_size = 0;
    }
    alloc_size_vec->log2_eec_size = profile->log2_max_eecs + THH_DDR_LOG2_EEC_ENTRY_SIZE; 
    alloc_size_vec->log2_eeec_size = profile->log2_max_eecs + THH_DDR_LOG2_EEEC_ENTRY_SIZE;    /* in-flight rdma */

    return;
}


/******************************************************************************
 *  Function:     THH_check_profile
 *
 *  Description:  Validates profile values against tavor max values, as obtained
 *                from GET_DEV_LIM query
 *
 *  Details:
 *
 *****************************************************************************/
static HH_ret_t THH_check_profile(THH_hob_t   hob)
{
    
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob->profile.log2_uar_pg_size < hob->dev_lims.log_pg_sz) {
        MTL_ERROR1("THH_calculate_default_profile:  log2 UAR page size(%u) is less than the Tavor minimum (%u)\n", 
                   hob->profile.log2_uar_pg_size, hob->dev_lims.log_pg_sz);
        return HH_EAGAIN;
    }
    hob->profile.log2_max_qps             = (hob->profile.log2_max_qps > hob->dev_lims.log_max_qp) ? 
                                                         hob->dev_lims.log_max_qp : hob->profile.log2_max_qps; 
/*    hob->profile.log2_max_mcgs            = (hob->profile.log2_max_mcgs > hob->dev_lims.log_max_mcg) ? 
                                                         hob->dev_lims.log_max_mcg : hob->profile.log2_max_mcgs; */


    /* this is to limit the number of QPs per MCG to 8 due to a bug
       must be fixed later */
    hob->profile.log2_max_mcgs            = (3 > hob->dev_lims.log_max_mcg) ? 
                                                         hob->dev_lims.log_max_mcg : 3;


    hob->profile.log2_max_eecs            = (hob->profile.log2_max_eecs > hob->dev_lims.log_max_ee) ? 
                                                         hob->dev_lims.log_max_ee : hob->profile.log2_max_eecs;
    hob->profile.log2_max_cqs             = (hob->profile.log2_max_cqs > hob->dev_lims.log_max_cq) ? 
                                                         hob->dev_lims.log_max_cq : hob->profile.log2_max_cqs;
    hob->profile.log2_max_uar             = (hob->profile.log2_max_uar > hob->dev_lims.uar_sz + 20UL - hob->profile.log2_uar_pg_size) ? 
                                                         hob->dev_lims.uar_sz + 20UL - hob->profile.log2_uar_pg_size : hob->profile.log2_max_uar;

    hob->profile.log2_max_eqs             = (hob->profile.log2_max_eqs > hob->dev_lims.log_max_eq) ? 
                                                         hob->dev_lims.log_max_eq : hob->profile.log2_max_eqs; 
    hob->profile.max_num_pds              = (hob->profile.max_num_pds > (1UL<<hob->dev_lims.log_max_pd)) ? 
                                                         (1UL<<hob->dev_lims.log_max_pd) : hob->profile.max_num_pds;  

    if (THH_DEV_LIM_MCG_ENABLED(hob)) {
        hob->profile.qps_per_mcg         = (hob->profile.qps_per_mcg > (MT_size_t) (1U<<hob->dev_lims.log_max_qp_mcg)) ? 
                                                   (MT_size_t) (1U<<hob->dev_lims.log_max_qp_mcg) : hob->profile.qps_per_mcg;
    }

    return HH_OK;
}


#define THH_PROFILE_CALC_QP_AT_MINIMUM     (1)
#define THH_PROFILE_CALC_CQ_AT_MINIMUM     (1 << 1)
#define THH_PROFILE_CALC_PD_AT_MINIMUM     (1 << 2)
#define THH_PROFILE_CALC_REG_AT_MINIMUM    (1 << 3)
#define THH_PROFILE_CALC_WIN_AT_MINIMUM    (1 << 4)
#define THH_PROFILE_CALC_ALL_AT_MINIMUM    (THH_PROFILE_CALC_QP_AT_MINIMUM | THH_PROFILE_CALC_CQ_AT_MINIMUM | \
                                            THH_PROFILE_CALC_PD_AT_MINIMUM | THH_PROFILE_CALC_REG_AT_MINIMUM | \
                                            THH_PROFILE_CALC_WIN_AT_MINIMUM )
                                            
static int check_profile_sanity(THH_hob_t   hob, EVAPI_hca_profile_t *user_profile, THH_profile_input_t *thh_profile)
{
    u_int64_t  tmp_calc;
        /* check for bad minimum values */
        if ((user_profile->num_qp == 0) || (user_profile->num_cq == 0) || (user_profile->num_pd == 0) ||
            (user_profile->num_mr == 0) || (user_profile->max_qp_ous_rd_atom == 0) ) {
            MTL_ERROR1(MT_FLFMT("profile: QPs or CQs or PDs or MRs or max_qp_ous_rd_atom equal to 0"));
            return 0;
        }
        if (user_profile->num_qp > (1U<<hob->dev_lims.log_max_qp)) {
            MTL_ERROR1(MT_FLFMT("profile: num QPs more than device limit(%d)"),
                       (1U<<hob->dev_lims.log_max_qp));
            return 0;
        } else if (user_profile->num_qp < 1) {
            MTL_ERROR1(MT_FLFMT("profile: num QPs must be at least 1"));
            return 0;
        }

        if (user_profile->num_cq > (1U<<hob->dev_lims.log_max_cq)) {
            MTL_ERROR1(MT_FLFMT("profile: num CQs more than device limit(%d)"),
                       (1U<<hob->dev_lims.log_max_cq));
            return 0;
        } else if (user_profile->num_cq < 1) {
            MTL_ERROR1(MT_FLFMT("profile: num CQs must be at least 1"));
            return 0;
        }

        if (user_profile->num_pd > (1U<<hob->dev_lims.log_max_pd)) {
            MTL_ERROR1(MT_FLFMT("profile: num PDs more than device limit(%d)"),
                       (1U<<hob->dev_lims.log_max_pd));
            return 0;
        } else if (user_profile->num_pd < 1) {
            MTL_ERROR1(MT_FLFMT("profile: num PDs must be at least 1"));
            return 0;
        }
        if (user_profile->num_mr < 1) {
            MTL_ERROR1(MT_FLFMT("profile: num MRs must be at least 1"));
            return 0;
        }
        if (user_profile->max_qp_ous_rd_atom > (1U<<hob->dev_lims.log_max_ra_res_qp)) {
            MTL_ERROR1(MT_FLFMT("profile: max_qp_ous_rd_atom more than device limit (%d)"),
                       (1U<<hob->dev_lims.log_max_ra_res_qp));
            return 0;
        }

        if (ceil_log2((u_int64_t)user_profile->max_mcg) > hob->dev_lims.log_max_mcg) {
            MTL_ERROR1(MT_FLFMT("profile: num MCGs more than device limit(%d)"),
                       (1U<<hob->dev_lims.log_max_mcg));
            return 0;
        }

        if (ceil_log2((u_int64_t)user_profile->qp_per_mcg) > hob->dev_lims.log_max_qp_mcg) {
            MTL_ERROR1(MT_FLFMT("profile: QPs per multicast group greater than device limit (%d)"),
                       (1U<<hob->dev_lims.log_max_qp_mcg));
            return 0;
        }
        if (user_profile->num_cq > (user_profile->num_qp * 2)) {
            MTL_ERROR1(MT_FLFMT("profile: CQs more than twice QPs in hca profile"));
            return 0;
        }
        if ((user_profile->max_mcg > 0) && (user_profile->qp_per_mcg < 8)) {
            MTL_ERROR1(MT_FLFMT("profile: if MCGs not zero, QP_PER_MCG must be >= 8"));
            return 0;
        }
        if (ceil_log2(user_profile->num_mr) > hob->dev_lims.log_max_mpts) {
            MTL_ERROR1("profile:  Requested MRs use more MTTs than HCA provides\n");
            return 0;
        }
        
        tmp_calc = (u_int64_t)((u_int64_t) user_profile->num_qp +
                               (u_int64_t) (unsigned long)THH_NUM_RSVD_QP +
                               (u_int64_t) user_profile->num_cq + 
                               (u_int64_t) user_profile->num_mr);
        if ( hob->dev_lims.log_max_mtt_seg < ceil_log2( tmp_calc * (u_int64_t)(1U<<thh_profile->log2_mtt_segs_per_region) )) {
            MTL_ERROR1("profile:  Requested parameters (CQs + QPs + MRs) use more MTTs than HCA provides\n");
            return 0;
        }


        if (ceil_log2(user_profile->num_mr) > hob->dev_lims.log_max_mpts) {
            MTL_ERROR1("profile:  Requested MRs use more MPTs than HCA provides\n");
            return 0;
        }

        if (ceil_log2(user_profile->num_mw) > hob->dev_lims.log_max_mpts) {
            MTL_ERROR1("profile:  Requested MWs use more MPTs than HCA provides\n");
            return 0;
        }

        tmp_calc = (u_int64_t)((u_int64_t) user_profile->num_qp +
                               (u_int64_t) (unsigned long)THH_NUM_RSVD_QP +
                               (u_int64_t) user_profile->num_cq + 
                               (u_int64_t) user_profile->num_mr + 
                               (u_int64_t) user_profile->num_mw);

        if ( hob->dev_lims.log_max_mpts < ceil_log2( tmp_calc)) {
            MTL_ERROR1("profile:  Requested parameters (CQs + QPs + MRs + MWs) use more MPTs than HCA provides\n");
            return 0;
        }
        return 1;
}
/******************************************************************************
 *  Function:     THH_calculate_profile
 *
 *  Description:  Calculates and installs profile values
 *
 *  input
 *                hob
 *                profile_user_data - pointer to a user override for the data used
 *                                    in calculating the THH profile.
 *
 *  Details:
 *
 *        All calculations are derived from the following data:
 *
 *        - max QPs = 64k per 128M DDR size (= 2^16)
 *        - max MPT entries per HCA:       1M (= 2^20)
 *        - avg Regions/windows per QP     8  (= 2^3)
 *        - avg Segments per Region        8  (= 2^3)
 *        - avg inflight RDMA per QP       4  (= 2^2)
 *
 *        Calculations are as follows:
 *        Max UARs = 1 per QP
 *        Max CQs  = 1 per QP
 *        Max PDs  = 1 per QP
 *        Max Regions/Wins per QP = 8, divided as follows:
 *             internal regions = 2 per QP  (1 for QP, one for CQ)
 *             external regions = 2 per QP
 *             windows          = 4 per QP
 *
 *        MPT:
 *           Tavor has a max of 1M regions/windows per HCA, and the MPT size must
 *           be a power of 2.  It is pointless to have fewer than 8 regions/windows per QP
 *           (as divided up above).  This means that the maximum number of QPs allowable,
 *           regardless of DDR size, is 128K.  Therefore, the presence of the "min" function
 *           in calculating the max number of MPT entries.  In effect, the 1M table size limitation
 *           means that a DDR larger than 256M will only add to the user-available DDR memory, and
 *           not to the driver's internal tables.
 *
 *        MTT:
 *           The default MTT size allocated has 2 segments per Region, with a segment size of 8 entries.
 *
 *        MCG:  for 128M: 4096 Groups per HCA, with 16 QPs per group (so that entry size is 64 bytes).
 *              for 256M: 8192 Groups per HCA, with 16 QPs per group (so that entry size is 64 bytes).
 *
 *        NOTES:
 *           If the profile_user_data is NULL, default values are used.  After the profile calculation,
 *           a check is done to see that all values are within HCA_DEV_LIM values, and that the result
 *           does not exceed the DDR memory size.  If any violations are encountered, the number of QPs
 *           is reduced by half, and the calculation is redone.
 *           
 *****************************************************************************/
static HH_ret_t THH_calculate_profile(THH_hob_t   hob, 
                                      EVAPI_hca_profile_t *profile_user_data,
                                      EVAPI_hca_profile_t  *sugg_profile_p)
{
    u_int8_t            log2_host_pg_size;
    EVAPI_hca_profile_t local_user_profile;
    THH_profile_input_t profile_input_data;
    u_int64_t           tot_ddr_allocs;
    THH_ddr_allocation_vector_t ddr_alloc_vec;
    MT_size_t           *ddr_alloc_iterator, temp_size;
    u_int32_t           i;
    MT_bool             ddr_calc_loop = TRUE, need_to_loop = FALSE;
    u_int32_t           calc_at_minimum = 0;
//    EVAPI_hca_profile_t hca_profile;


    if (profile_user_data != NULL) {
       
        memcpy(&local_user_profile,  profile_user_data, sizeof(EVAPI_hca_profile_t));

        /* default value substitutions */
        local_user_profile.num_qp = (local_user_profile.num_qp == 0xFFFFFFFF) ? 
                       THH_PROF_MAX_QPS : local_user_profile.num_qp; 
        local_user_profile.num_cq = (local_user_profile.num_cq == 0xFFFFFFFF) ? 
                               THH_PROF_MAX_CQS  : local_user_profile.num_cq; 
        local_user_profile.num_pd = (local_user_profile.num_pd == 0xFFFFFFFF) ? 
                               THH_PROF_MAX_PDS : local_user_profile.num_pd; 
        local_user_profile.num_mr = (local_user_profile.num_mr == 0xFFFFFFFF) ? 
                               THH_PROF_MAX_REGIONS : local_user_profile.num_mr; 
        local_user_profile.num_mw = (local_user_profile.num_mw == 0xFFFFFFFF) ? 
                               THH_PROF_MAX_WINDOWS : local_user_profile.num_mw;
        
        local_user_profile.max_qp_ous_rd_atom = (local_user_profile.max_qp_ous_rd_atom == 0xFFFFFFFF) ? 
                               (1 << THH_DDR_LOG2_INFLIGHT_RDMA_PER_QP):
                               local_user_profile.max_qp_ous_rd_atom; 

        local_user_profile.max_mcg = (local_user_profile.max_mcg == 0xFFFFFFFF) ? 
                               (1 << THH_DDR_LOG2_MAX_MCG):local_user_profile.max_mcg; 
        
        local_user_profile.qp_per_mcg = (local_user_profile.qp_per_mcg == 0xFFFFFFFF) ? 
                               (1 << THH_DDR_LOG2_MIN_QP_PER_MCG):local_user_profile.qp_per_mcg; 
        
        if (sugg_profile_p != NULL) {
            memcpy(sugg_profile_p, &local_user_profile, sizeof(EVAPI_hca_profile_t));
        }

        profile_input_data.max_qps        = local_user_profile.num_qp ;
        profile_input_data.max_cqs        = local_user_profile.num_cq ;
        profile_input_data.max_pds        = local_user_profile.num_pd ;
        profile_input_data.max_regions    = local_user_profile.num_mr ;
        profile_input_data.max_windows    = local_user_profile.num_mw ;

        profile_input_data.min_qps        = (1U<<hob->dev_lims.log2_rsvd_qps) + THH_NUM_RSVD_QP + 1;
        profile_input_data.min_cqs        = (1U<<hob->dev_lims.log2_rsvd_cqs) + 1;
        profile_input_data.min_pds        = hob->dev_lims.num_rsvd_pds + THH_NUM_RSVD_PD + 1;
        profile_input_data.min_regions    = (1 << hob->dev_lims.log2_rsvd_mtts) + 1;
        profile_input_data.min_windows    = (1 << hob->dev_lims.log2_rsvd_mrws);
    
        profile_input_data.reduction_pct_qps     = 10;
        profile_input_data.reduction_pct_cqs     = 10;
        profile_input_data.reduction_pct_pds     = 10;
        profile_input_data.reduction_pct_regions = 10;
        profile_input_data.reduction_pct_windows = 10;
        
        profile_input_data.log2_inflight_rdma_per_qp = ceil_log2(local_user_profile.max_qp_ous_rd_atom);
        profile_input_data.log2_max_mcg              = ceil_log2(local_user_profile.max_mcg);
        profile_input_data.log2_min_qp_per_mcg       = ceil_log2(local_user_profile.qp_per_mcg);
        
        profile_input_data.log2_max_eq               = THH_DDR_LOG2_MAX_EQ;
        profile_input_data.log2_mcg_hash_proportion  = THH_DDR_LOG2_MCG_HASH_PROPORTION;
        profile_input_data.log2_mtt_entries_per_seg  = THH_DDR_LOG2_MTT_ENTRIES_PER_SEG;
        profile_input_data.log2_mtt_segs_per_region  = THH_DDR_LOG2_MTT_SEGS_PER_REGION;
        profile_input_data.use_priv_udav             = THH_USE_PRIV_UDAV;
        profile_input_data.log2_wqe_ddr_space_per_qp = THH_LOG2_WQE_DDR_SPACE_PER_QP;
        /*sanity checks */
        if (check_profile_sanity(hob,&local_user_profile, &profile_input_data) == 0) {
            MTL_ERROR1(MT_FLFMT("THH_calculate_profile: user profile not valid"));
            return HH_EINVAL_PARAM;
        }
    } else {
        /* use internally defined default values */
        profile_input_data.max_qps        = THH_PROF_MAX_QPS;
        profile_input_data.max_cqs        = THH_PROF_MAX_CQS;
        profile_input_data.max_pds        = THH_PROF_MAX_PDS;
        profile_input_data.max_regions    = THH_PROF_MAX_REGIONS;
        profile_input_data.max_windows    = THH_PROF_MAX_WINDOWS;
        profile_input_data.max_priv_udavs = THH_DDR_MAX_PRIV_UDAVS;
        
        profile_input_data.min_qps        = THH_PROF_MIN_QPS;
        profile_input_data.min_cqs        = THH_PROF_MIN_CQS;
        profile_input_data.min_pds        = THH_PROF_MIN_PDS;
        profile_input_data.min_regions    = THH_PROF_MIN_REGIONS;
        profile_input_data.min_windows    = THH_PROF_MIN_WINDOWS;

        profile_input_data.reduction_pct_qps     = THH_PROF_PCNT_REDUCTION_QPS;
        profile_input_data.reduction_pct_cqs     = THH_PROF_PCNT_REDUCTION_CQS;
        profile_input_data.reduction_pct_pds     = THH_PROF_PCNT_REDUCTION_PDS;
        profile_input_data.reduction_pct_regions = THH_PROF_PCNT_REDUCTION_REGIONS;
        profile_input_data.reduction_pct_windows = THH_PROF_PCNT_REDUCTION_WINDOWS;
        
        profile_input_data.log2_inflight_rdma_per_qp = THH_DDR_LOG2_INFLIGHT_RDMA_PER_QP;
        profile_input_data.log2_max_eq               = THH_DDR_LOG2_MAX_EQ;
        profile_input_data.log2_max_mcg              = THH_DDR_LOG2_MAX_MCG;
        profile_input_data.log2_min_qp_per_mcg       = THH_DDR_LOG2_MIN_QP_PER_MCG;
        profile_input_data.log2_mcg_hash_proportion  = THH_DDR_LOG2_MCG_HASH_PROPORTION;
        profile_input_data.log2_mtt_entries_per_seg  = THH_DDR_LOG2_MTT_ENTRIES_PER_SEG;
        profile_input_data.log2_mtt_segs_per_region  = THH_DDR_LOG2_MTT_SEGS_PER_REGION;
        profile_input_data.use_priv_udav             = THH_USE_PRIV_UDAV;
        profile_input_data.log2_wqe_ddr_space_per_qp = THH_LOG2_WQE_DDR_SPACE_PER_QP;
    }

    hob->profile.use_priv_udav = profile_input_data.use_priv_udav;
    hob->profile.max_priv_udavs = profile_input_data.max_priv_udavs;
    
    /* need inflight rdma per QP for rdb size in DDR, and for THH_qpm_create */
    hob->profile.log2_inflight_rdma_per_qp = (u_int8_t) profile_input_data.log2_inflight_rdma_per_qp;

    /* manipulate MCG max if not inputting a profile, or if inputting profile which allows reduction */
    /* Reduce the number of MCGs for smaller DDR memories */
    hob->profile.log2_max_mcgs            = profile_input_data.log2_max_mcg;
    if ((hob->profile.ddr_size_code < THH_DDR_SIZE_128M)
        && ((profile_user_data == NULL)|| (local_user_profile.require == FALSE))) {
            hob->profile.log2_max_mcgs--;
    }

    log2_host_pg_size = MOSAL_SYS_PAGE_SHIFT;
    if (log2_host_pg_size < hob->dev_lims.log_pg_sz) {
        MTL_ERROR1("THH_calculate_default_profile:  Host min page size(%lu) is too small\n", 
                   (unsigned long ) MOSAL_SYS_PAGE_SIZE);
        return HH_EAGAIN;
    }

    /* do not allocate DDR memory for MCGs if MCG is not enabled in dev_limits  */
    hob->profile.ddr_alloc_vec_size = (THH_DEV_LIM_MCG_ENABLED(hob) ? 
                                       THH_DDR_ALLOCATION_VEC_SIZE : THH_DDR_ALLOCATION_VEC_SIZE - 1) ;  /* no eec as yet */
    hob->profile.log2_wqe_ddr_space_per_qp = profile_input_data.log2_wqe_ddr_space_per_qp;
    
    /* MCG calculations - not in recalculation loop, since the amount of memory involved is very small*/
    /* each MCG entry must be a power-of-2 size. To guarantee a power-of-2, we take a "ceiling" log of the */
    /* MCG entry size(in bytes), and then compute the actual number of QPs per mcg backwards from the mcg_size variable. */
    /* We also require (as a sanity check) that the log2_mcg_hash_size be greater than zero */
    if ((THH_DEV_LIM_MCG_ENABLED(hob)) &&
        ((int)(hob->profile.log2_max_mcgs + profile_input_data.log2_mcg_hash_proportion) > 0)) {
        hob->profile.log2_mcg_entry_size = ceil_log2(((1U<<profile_input_data.log2_min_qp_per_mcg) * THH_DDR_MCG_BYTES_PER_QP) + 
                                                     THH_DDR_MCG_ENTRY_HEADER_SIZE);
        hob->profile.qps_per_mcg    = ( (1U<<(hob->profile.log2_mcg_entry_size)) - THH_DDR_MCG_ENTRY_HEADER_SIZE) / 
                                                             THH_DDR_MCG_BYTES_PER_QP;
    
        /* the hash proportion is the log of the power-of-2 fraction of the total MCG entries used for the hash table. */
        /* Thus, for example, a proportion of (1/2) gets a log2_mcg_hash_proportion = -1 */
        hob->profile.log2_mcg_hash_size  = hob->profile.log2_max_mcgs + profile_input_data.log2_mcg_hash_proportion;
    } else {
        /*UD MCGs not available on this HCA*/
        hob->profile.log2_mcg_entry_size = 0;
        hob->profile.qps_per_mcg    = 0;
        hob->profile.log2_mcg_hash_size  = 0;
        hob->profile.log2_max_mcgs = 0;
        }
    
    hob->profile.log2_mtt_entries_per_seg = profile_input_data.log2_mtt_entries_per_seg;
    hob->profile.log2_mtt_segs_per_region = profile_input_data.log2_mtt_segs_per_region;
    
    hob->profile.log2_uar_pg_size         = log2_host_pg_size;
    hob->profile.log2_max_uar             = hob->dev_lims.uar_sz + 20 - hob->profile.log2_uar_pg_size;
    hob->profile.log2_max_eqs             = profile_input_data.log2_max_eq;      /* 64 EQs */
    hob->profile.max_num_pds              = profile_input_data.max_pds;

    hob->profile.max_num_qps              = profile_input_data.max_qps;

    hob->profile.log2_max_qps             = ceil_log2(profile_input_data.max_qps+ 
                                               (1U<<hob->dev_lims.log2_rsvd_qps) + THH_NUM_RSVD_QP); 
    
    /* adjust max QPs downward (if using internal profile, or if user profile permits)
     * if the few reserved QPs cause max qps to go beyond a power-of-2.
     */

    if (hob->profile.log2_max_qps > ceil_log2(profile_input_data.max_qps)) {
        MTL_DEBUG1(MT_FLFMT("%s:  reserved qps cause profile qps to jump a power-of-2"),__func__);
        if ((profile_user_data==NULL) || (local_user_profile.require == FALSE)) {
            hob->profile.log2_max_qps--;
            hob->profile.max_num_qps = (1U<<hob->profile.log2_max_qps) - (1U<<hob->dev_lims.log2_rsvd_qps)
                                                                           - THH_NUM_RSVD_QP;
            MTL_DEBUG1(MT_FLFMT("%s: Adjusting max qps to "SIZE_T_DFMT),__func__, hob->profile.max_num_qps);
        }
    }

    /* TBD: Expose max_srqs to profile given by user and use MOD_STAT_CFG */
    if (hob->dev_lims.srq) {
      hob->profile.log2_max_srqs            = hob->dev_lims.log_max_srqs;
      hob->profile.max_num_srqs             = 
        (1U << hob->dev_lims.log_max_srqs) - (1 << hob->dev_lims.log2_rsvd_srqs);
    } else {
      hob->profile.log2_max_srqs            = 0;
      hob->profile.max_num_srqs             = 0;
    }
    
    hob->profile.max_num_cqs              = profile_input_data.max_cqs;
    hob->profile.log2_max_cqs             = ceil_log2(profile_input_data.max_cqs + 
                                                      (1U<<hob->dev_lims.log2_rsvd_cqs)); 
    /* adjust max CQs downward (if using internal profile, or if user profile permits) 
     * if the few reserved CQs cause max cqs to go beyond a power-of-2.
     */

    if (hob->profile.log2_max_cqs > ceil_log2(profile_input_data.max_cqs)) {
        MTL_DEBUG1(MT_FLFMT("%s:  reserved cqs cause profile cqs to jump a power-of-2"),__func__);
        if ((profile_user_data == NULL) || (local_user_profile.require == FALSE)) {
            hob->profile.log2_max_cqs--;
            hob->profile.max_num_cqs = (1U<<hob->profile.log2_max_cqs) - (1U<<hob->dev_lims.log2_rsvd_cqs);
            MTL_DEBUG1(MT_FLFMT("%s: Adjusting max cqs to "SIZE_T_DFMT),__func__, hob->profile.max_num_cqs);
        }
    }
    hob->profile.num_external_mem_regions = profile_input_data.max_regions;  /* 2 per QP */
    hob->profile.num_mem_windows          = profile_input_data.max_windows;
    hob->profile.log2_max_eecs            = 0;

    while (ddr_calc_loop) {
        MT_bool continue_calc_loop;

        continue_calc_loop = FALSE;
    
        MTL_DEBUG4("THH_calculate_profile: max_qps = "SIZE_T_FMT", max_cqs = "SIZE_T_FMT", max_priv_udav="SIZE_T_FMT
                   ",\nmax_pds="SIZE_T_FMT",max_reg="SIZE_T_FMT", max_win="SIZE_T_FMT"\n",
                        hob->profile.max_num_qps, hob->profile.max_num_cqs, hob->profile.max_priv_udavs,
                        hob->profile.max_num_pds, hob->profile.num_external_mem_regions, 
                        hob->profile.num_mem_windows); 

        /* add all raw resources without Tavor-reserved quantities */
        temp_size = hob->profile.max_num_qps + THH_NUM_RSVD_QP + 
                    hob->profile.max_num_cqs + hob->profile.num_external_mem_regions;
        
        hob->profile.log2_max_mtt_entries     = ceil_log2(
                                                   ( temp_size * (1U<<profile_input_data.log2_mtt_segs_per_region)
                                                      * (1U<<profile_input_data.log2_mtt_entries_per_seg))
                                                   + (1 << hob->dev_lims.log2_rsvd_mtts)
                                                );
    
        /* add all raw resources without Tavor-reserved quantities */
        temp_size = hob->profile.max_num_qps + THH_NUM_RSVD_QP + hob->profile.max_num_cqs 
            + hob->profile.num_external_mem_regions + hob->profile.num_mem_windows;
        
        hob->profile.log2_max_mpt_entries       = ceil_log2(temp_size + (1 << hob->dev_lims.log2_rsvd_mrws));
    
    
        if (hob->profile.log2_max_mtt_entries - profile_input_data.log2_mtt_entries_per_seg 
             > hob->dev_lims.log_max_mtt_seg) {
            continue_calc_loop = TRUE;
            need_to_loop = TRUE;
        }
        
        if (!continue_calc_loop) {
            /* Now, compute the total DDR size, and verify that we have not over-allocated it.  If yes, reduce QPs by half, and */
            /* recompute all above parameters starting with log2_max_regions */
            calculate_ddr_alloc_vec(hob, &(hob->profile),&ddr_alloc_vec);
        
            /* Add up all the sizes in the ddr allocation vector */
            tot_ddr_allocs = 0;
            ddr_alloc_iterator = (MT_size_t *)&(ddr_alloc_vec);
            for (i = 0; i < hob->profile.ddr_alloc_vec_size; i++, ddr_alloc_iterator++) {
                if ((*ddr_alloc_iterator) == THH_DDRMM_INVALID_SZ) {
                    temp_size = 0;  /* no allocation */
                } else if ((*ddr_alloc_iterator) >= ceil_log2(hob->profile.ddr_size)) {
                    temp_size = hob->profile.ddr_size;
                } else  {
                    temp_size =  (((MT_size_t) 1ul) << (*ddr_alloc_iterator));
                }
                MTL_DEBUG4("THH_calculate_profile:DDR: %s = "SIZE_T_XFMT"("SIZE_T_DFMT")\n", 
                           THH_get_ddr_allocation_string(i), temp_size, temp_size); 
                tot_ddr_allocs += temp_size;
            }
            
            /* see if need to reserve space for WQEs in DDR */
            if (hob->profile.log2_wqe_ddr_space_per_qp != 0) {
                temp_size =  (((MT_size_t) 1ul) << (hob->profile.log2_max_qps + hob->profile.log2_wqe_ddr_space_per_qp));
                MTL_DEBUG4("THH_calculate_profile:  WQEs ddr area = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n", 
                           temp_size, temp_size); 
                tot_ddr_allocs += temp_size;
            }
        
            /* see if need to reserve space for privileged UDAVs in DDR */
            if (hob->profile.use_priv_udav) {
                temp_size =  hob->profile.max_priv_udavs * (sizeof(struct tavorprm_ud_address_vector_st) / 8);
                MTL_DEBUG4("THH_calculate_profile:  privileged UDAVs ddr area = "SIZE_T_XFMT" ("SIZE_T_DFMT")\n",
                            temp_size, temp_size); 
                tot_ddr_allocs += temp_size;
            }
        
            /* test against DDR size */
            MTL_DEBUG4("THH_calculate_profile:  total DDR allocs = %d MB (incl reserved areas)\n",(int)(tot_ddr_allocs>>20)); 
            if ((hob->profile.ddr_size < tot_ddr_allocs) || 
                          ((profile_user_data == NULL) && (hob->profile.max_num_qps>(1U<<16)))){ 
                          /*do not want more than 64K QPs if using internal defaults*/
                continue_calc_loop = TRUE;
                need_to_loop = TRUE;
            }

        }
        if (continue_calc_loop) {
            u_int64_t  temp;
            u_int32_t  u32_temp, change_flag;
            /* Reduce flagged profile input params by factor of 10 percent */
            change_flag = 0;
            if ((calc_at_minimum & THH_PROFILE_CALC_QP_AT_MINIMUM) == 0) {
                change_flag++;
                temp = (u_int64_t)(hob->profile.max_num_qps) * (100 - profile_input_data.reduction_pct_qps);
                /*check for overflow. If have overflow, use approximate percentages (divide by 1024) */
                if (temp & MAKE_ULONGLONG(0xFFFFFFFF00000000)) {
                    temp = (u_int64_t)(hob->profile.max_num_qps) * (1024 - (profile_input_data.reduction_pct_qps*10));
                    temp >>= 10;
                    u32_temp = (u_int32_t)(temp & 0xFFFFFFFF);
                } else {
                    /* use more exact percentages -- but still not floating point */
                    u32_temp = (u_int32_t)temp;
                    u32_temp /= 100;
                }
                if (u32_temp <= (u_int32_t)profile_input_data.min_qps) {
                    calc_at_minimum |= THH_PROFILE_CALC_QP_AT_MINIMUM;
                    u32_temp = profile_input_data.min_qps;
                }
                hob->profile.max_num_qps = u32_temp;
                hob->profile.log2_max_qps = ceil_log2(u32_temp + (1U<<hob->dev_lims.log2_rsvd_qps) 
                                                      + THH_NUM_RSVD_QP);
            }
            
            
            if ((calc_at_minimum & THH_PROFILE_CALC_CQ_AT_MINIMUM) == 0) {
                change_flag++;
                temp = (u_int64_t)(hob->profile.max_num_cqs) * (100 - profile_input_data.reduction_pct_cqs);
                if (temp & MAKE_ULONGLONG(0xFFFFFFFF00000000)) {
                    temp = (u_int64_t)(hob->profile.max_num_cqs) * (1024 - (profile_input_data.reduction_pct_cqs*10));
                    temp >>= 10;
                    u32_temp = (u_int32_t)(temp & 0xFFFFFFFF);
                } else {
                    /* use more exact percentages -- but still not floating point */
                    u32_temp = (u_int32_t)temp;
                    u32_temp /= 100;
                }
                if (u32_temp <= (u_int32_t)profile_input_data.min_cqs) {
                    calc_at_minimum |= THH_PROFILE_CALC_CQ_AT_MINIMUM;
                    u32_temp = profile_input_data.min_cqs;
                }
                hob->profile.max_num_cqs = u32_temp;
                hob->profile.log2_max_cqs = ceil_log2(u32_temp + (1U<<hob->dev_lims.log2_rsvd_cqs));
            }

            if ((calc_at_minimum & THH_PROFILE_CALC_PD_AT_MINIMUM) == 0) {
                change_flag++;
                temp = (u_int64_t)(hob->profile.max_num_pds) * (100 - profile_input_data.reduction_pct_pds);
                if (temp & MAKE_ULONGLONG(0xFFFFFFFF00000000)) {
                    temp = (u_int64_t)(hob->profile.max_num_pds) * (1024 - (profile_input_data.reduction_pct_pds*10));
                    temp >>= 10;
                    u32_temp = (u_int32_t)(temp & 0xFFFFFFFF);
                } else {
                    /* use more exact percentages -- but still not floating point */
                    u32_temp = (u_int32_t)temp;
                    u32_temp /= 100;
                }
                if (u32_temp <= (u_int32_t)profile_input_data.min_pds) {
                    calc_at_minimum |= THH_PROFILE_CALC_PD_AT_MINIMUM;
                    u32_temp = profile_input_data.min_pds;
                }
                hob->profile.max_num_pds = u32_temp;
            }

            if ((calc_at_minimum & THH_PROFILE_CALC_REG_AT_MINIMUM) == 0) {
                change_flag++;
                temp = (u_int64_t)(hob->profile.num_external_mem_regions) * (100 - profile_input_data.reduction_pct_regions);
                if (temp & MAKE_ULONGLONG(0xFFFFFFFF00000000)) {
                    temp = (u_int64_t)(hob->profile.num_external_mem_regions) * (1024 - (profile_input_data.reduction_pct_regions*10));
                    temp >>= 10;
                    u32_temp = (u_int32_t)(temp & 0xFFFFFFFF);
                } else {
                    /* use more exact percentages -- but still not floating point */
                    u32_temp = (u_int32_t)temp;
                    u32_temp /= 100;
                }
                if (u32_temp <= (u_int32_t)profile_input_data.min_regions) {
                    calc_at_minimum |= THH_PROFILE_CALC_REG_AT_MINIMUM;
                    u32_temp = profile_input_data.min_regions;
                }
                hob->profile.num_external_mem_regions = u32_temp;
            }

            if ((calc_at_minimum & THH_PROFILE_CALC_WIN_AT_MINIMUM) == 0) {
                change_flag++;
                temp = (u_int64_t)(hob->profile.num_mem_windows) * (100 - profile_input_data.reduction_pct_windows);
                if (temp & MAKE_ULONGLONG(0xFFFFFFFF00000000)) {
                    temp = (u_int64_t)(hob->profile.num_mem_windows) * (1024 - (profile_input_data.reduction_pct_windows*10));
                    temp >>= 10;
                    u32_temp = (u_int32_t)(temp & 0xFFFFFFFF);
                } else {
                    /* use more exact percentages -- but still not floating point */
                    u32_temp = (u_int32_t)temp;
                    u32_temp /= 100;
                }
                if (u32_temp <= (u_int32_t)profile_input_data.min_windows) {
                    calc_at_minimum |= THH_PROFILE_CALC_WIN_AT_MINIMUM;
                    u32_temp = profile_input_data.min_windows;
                }
                hob->profile.num_mem_windows = u32_temp;
            }
            if (hob->profile.log2_inflight_rdma_per_qp > THH_DDR_LOG2_INFLIGHT_RDMA_PER_QP) {
                 hob->profile.log2_inflight_rdma_per_qp--;
                 change_flag++;
            }

            /* check if we were able to perform any reductions */
            if (change_flag == 0) {
                MTL_ERROR1("THH_calculate_default_profile:  DDR memory to small for MIN profile\n");
                ddr_alloc_iterator = (MT_size_t *)&(ddr_alloc_vec);
                for (i = 0; i < hob->profile.ddr_alloc_vec_size; i++, ddr_alloc_iterator++) {
                    if ((*ddr_alloc_iterator) == THH_DDRMM_INVALID_SZ) {
                        temp_size = 0;  /* no allocation */
                    } else if ((*ddr_alloc_iterator) >= ceil_log2(hob->profile.ddr_size)) {
                        temp_size = hob->profile.ddr_size;
                        MTL_ERROR1(MT_FLFMT("THH_calculate_profile: %s uses ALL available DDR memory"), 
                               THH_get_ddr_allocation_string(i)); 
                    } else  {
                        temp_size =  (((MT_size_t) 1ul) << (*ddr_alloc_iterator));
                    }
                    MTL_ERROR1(MT_FLFMT("THH_calculate_profile:DDR: %s = "SIZE_T_XFMT"("SIZE_T_DFMT")"), 
                               THH_get_ddr_allocation_string(i), temp_size, temp_size); 
                }
                return HH_EAGAIN;
            }
        } else {
            ddr_calc_loop = FALSE;
        }
    }
    
    THH_check_profile(hob);  /* final adjustment to catch dev-lim overruns*/

    /* adjust mcg hash table after final adjustment of mcg size */
    if (THH_DEV_LIM_MCG_ENABLED(hob)) {
        hob->profile.log2_mcg_hash_size  =  (hob->profile.log2_max_mcgs) + profile_input_data.log2_mcg_hash_proportion;
        if (hob->profile.log2_mcg_hash_size < 0) {
            hob->profile.log2_mcg_hash_size = 0;
        }
    }

    THH_PRINT_PROFILE(&(hob->profile));
    MTL_DEBUG4("Leaving THH_calculate_profile\n");

    if (sugg_profile_p != NULL) { 
            sugg_profile_p->num_mw = (u_int32_t)hob->profile.num_mem_windows;
            sugg_profile_p->num_qp = (u_int32_t)hob->profile.max_num_qps;
            sugg_profile_p->num_cq = (u_int32_t)hob->profile.max_num_cqs;
            sugg_profile_p->num_pd = (u_int32_t)hob->profile.max_num_pds;
            sugg_profile_p->num_mr = (u_int32_t)hob->profile.num_external_mem_regions;
            sugg_profile_p->max_qp_ous_rd_atom = (1U<<hob->profile.log2_inflight_rdma_per_qp);
    }
    if ((profile_user_data != NULL) && (profile_user_data->require != 0) && (need_to_loop == TRUE)) {
            MTL_ERROR1("THH_calculate_default_profile:  Provided profile requires too many resources\n");
        return HH_ENOMEM;
    }
    return HH_OK;
}

/*****************************************************************************
******************************************************************************
************************  HOB Interface FUNCTIONS ****************************
******************************************************************************
******************************************************************************/

/******************************************************************************
 *  Function:     THH_hob_close_hca
 *
 *  Description:  This function stops HCA hardware activity and frees all associated resources.
 *
 *  input:
 *                hca_hndl
 *  output: 
 *                none
 *  returns:
 *                HH_OK
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     If any errors occur, continue process of de-allocating resources.  However, log the errors,
 *                and return HH_ERR instead of HH_OK
 *
 *****************************************************************************/
HH_ret_t THH_hob_close_hca_internal(HH_hca_hndl_t  hca_hndl, MT_bool invoked_from_destroy)
{
  /* TBD - Complete function */
    THH_cmd_status_t    cmd_ret;
    MT_bool             have_error = FALSE;
    HH_ret_t            ret = HH_OK;
    MT_phys_addr_t         *ddr_alloc_area;
    MT_size_t           *ddr_alloc_size;
    u_int32_t           i;
    u_int16_t           num_ports;
    call_result_t       res;
    THH_hob_t  thh_hob_p;
    DECLARE_FATAL_VARS;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_hob_close_hca: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_close_hca : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);
    
    /* uninterruptible acquire.  Want to be sure to clean up */
    MOSAL_mutex_acq_ui(&(thh_hob_p->mtx));
    if (hca_hndl->status == HH_HCA_STATUS_CLOSED) {
        MOSAL_mutex_rel(&(thh_hob_p->mtx));
        MTL_ERROR1("THH_hob_close_hca: Device already closed\n");
        return HH_EINVAL_HCA_HNDL;
    }
    
    /* move the HCA to CLOSING state, preserving fatal indicators */
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    if ((thh_hob_p->thh_state & THH_STATE_RUNNING) == 0) {
        MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
        MOSAL_mutex_rel(&(thh_hob_p->mtx));
        MTL_ERROR1(MT_FLFMT("THH_hob_close_hca:  already invoked"));
        return HH_EBUSY;
    }
    thh_hob_p->thh_state &= THH_STATE_HAVE_ANY_FATAL;
    thh_hob_p->thh_state |= THH_STATE_CLOSING;
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);

    /* transfer to closing state */    
    WAIT_IF_FATAL(thh_hob_p);
    if (have_fatal == FALSE) {
        num_ports = thh_hob_p->dev_lims.num_ports;
        for (i = 1; i <= num_ports; i++) {
            cmd_ret = THH_cmd_CLOSE_IB(thh_hob_p->cmd, (IB_port_t) i);
            if (cmd_ret != THH_CMD_STAT_OK) {
                MTL_ERROR1("THH_hob_close_hca: THH_cmd_CLOSE_IB error (%d)\n", cmd_ret);
                have_error = TRUE;
            }
        }
    }
    /* test if a fatal error occurred during CLOSE_IB. */
    if (have_fatal == FALSE) {
        WAIT_IF_FATAL(thh_hob_p);
    }
    thh_hob_p->compl_eq = THH_INVALID_EQN;
    thh_hob_p->ib_eq = THH_INVALID_EQN;
    
    /* destroy eventq mgr.  Event manager must destroy all EQs */
    ret = THH_eventp_destroy( thh_hob_p->eventp );
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_eventp_destroy error (%d)\n", ret);
        have_error = TRUE;
    }
    thh_hob_p->eventp = (THH_eventp_t)THH_INVALID_HNDL;

    if (thh_hob_p->mcgm != (THH_mcgm_t)THH_INVALID_HNDL) {
        ret = THH_mcgm_destroy(thh_hob_p->mcgm );
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_mcgm_destroy error (%d)\n", ret);
            have_error = TRUE;
        }
        thh_hob_p->mcgm = (THH_mcgm_t)THH_INVALID_HNDL;
    }

    MTL_DEBUG4("%s: calling MOSAL_unmap_phys_addr FOR KAR = " VIRT_ADDR_FMT "\n", __func__,
               (MT_virt_addr_t) thh_hob_p->kar_addr);
    if ((res = (MOSAL_unmap_phys_addr(MOSAL_get_kernel_prot_ctx(), (MT_virt_addr_t) thh_hob_p->kar_addr, 
                               (1 << thh_hob_p->profile.log2_uar_pg_size)))) != MT_OK) {
        MTL_ERROR1("THH_hob_close_hca: MOSAL_unmap_phys_addr error for kar: %d\n", res);
        have_error = TRUE;
    }
    thh_hob_p->kar_addr = (MT_virt_addr_t) 0;
    
    ret = THH_uar_destroy(thh_hob_p->kar);
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_uar_destroy error (%d)\n", ret);
        have_error = TRUE;
    }
    thh_hob_p->kar = (THH_uar_t)THH_INVALID_HNDL;


    if (thh_hob_p->udavm_use_priv) {
        ret = THH_udavm_destroy(thh_hob_p->udavm);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_udavm_destroy error (%d)\n", ret);
            have_error = TRUE;
        }
        thh_hob_p->udavm = (THH_udavm_t)THH_INVALID_HNDL;

        ret = THH_mrwm_deregister_mr(thh_hob_p->mrwm, thh_hob_p->udavm_lkey);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_mrwm_deregister_mr error (%d)\n", ret);
            have_error = TRUE;
        }

        if ((res = MOSAL_unmap_phys_addr( MOSAL_get_kernel_prot_ctx(), 
                                    (MT_virt_addr_t)  thh_hob_p->udavm_table , 
                                    thh_hob_p->udavm_table_size )) != MT_OK) {
            MTL_ERROR1("THH_hob_close_hca: MOSAL_unmap_phys_addr error for udavm: %d\n", res);
            have_error = TRUE;
        }
        thh_hob_p->udavm_table = (MT_virt_addr_t) NULL;

        ret = THH_ddrmm_free(thh_hob_p->ddrmm, thh_hob_p->udavm_table_ddr, thh_hob_p->udavm_table_size);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_ddrmm_free error (%d)\n", ret);
            have_error = TRUE;
        }
 
    }
    thh_hob_p->udavm = (THH_udavm_t)THH_INVALID_HNDL;
    thh_hob_p->udavm_table = (MT_virt_addr_t) NULL;
    thh_hob_p->udavm_table_ddr  = (MT_phys_addr_t) 0;
    thh_hob_p->udavm_table_size = 0;
    thh_hob_p->udavm_lkey = 0;

    if (thh_hob_p->srqm != (THH_srqm_t)THH_INVALID_HNDL) { /* SRQs are supported - SRQM exists */
      ret = THH_srqm_destroy( thh_hob_p->srqm);
      if (ret != HH_OK) {
          MTL_ERROR1("THH_hob_close_hca: THH_srqm_destroy error %s(%d)\n", HH_strerror_sym(ret), ret);
          have_error = TRUE;
      }
      thh_hob_p->srqm = (THH_srqm_t)THH_INVALID_HNDL;
    }
    
    ret = THH_qpm_destroy( thh_hob_p->qpm, have_error);
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_qpm_destroy error (%d)\n", ret);
        have_error = TRUE;
    }
    thh_hob_p->qpm = (THH_qpm_t)THH_INVALID_HNDL;

    FREE(thh_hob_p->init_ib_props);
    thh_hob_p->init_ib_props = ( THH_port_init_props_t *) NULL;

    ret = THH_cqm_destroy( thh_hob_p->cqm, have_error);
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_cqm_destroy error (%d)\n", ret);
        have_error = TRUE;
    }
    thh_hob_p->cqm = (THH_cqm_t)THH_INVALID_HNDL;

    ret = THH_mrwm_destroy(thh_hob_p->mrwm, have_error);
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_mrwm_destroy error (%d)\n", ret);
        have_error = TRUE;
    }
    thh_hob_p->mrwm = (THH_mrwm_t)THH_INVALID_HNDL;

    ret = THH_uldm_destroy(thh_hob_p->uldm );
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_uldm_destroy error (%d)\n", ret);
        have_error = TRUE;
    }
    thh_hob_p->uldm = (THH_uldm_t)THH_INVALID_HNDL;

    ret = THH_cmd_revoke_ddrmm(thh_hob_p->cmd);
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_close_hca: THH_cmd_revoke_ddrmm error (%d)\n", ret);
        have_error = TRUE;
    }

    ddr_alloc_area = (MT_phys_addr_t *) &(thh_hob_p->ddr_alloc_base_addrs_vec);
    ddr_alloc_size = (MT_size_t *)&(thh_hob_p->ddr_alloc_size_vec);
    for (i = 0; i < thh_hob_p->profile.ddr_alloc_vec_size; i++, ddr_alloc_area++, ddr_alloc_size++) {
        ret = *ddr_alloc_area != THH_DDRMM_INVALID_PHYS_ADDR ?
          THH_ddrmm_free(thh_hob_p->ddrmm,*ddr_alloc_area, (1U<< (*ddr_alloc_size))) : HH_OK;
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_ddrmm_free error (%d). i = %d\n", ret, i);
            have_error = TRUE;
        }
    }

    /* test for fatal again here */
    if (have_fatal == FALSE) {
        WAIT_IF_FATAL(thh_hob_p);
    }
    if (have_fatal == FALSE) {
        MTL_TRACE1("THH_hob_close_hca: Performing THH_cmd_CLOSE_HCA (no fatal)\n");
#ifdef SIMULATE_HALT_HCA
        cmd_ret = THH_cmd_CLOSE_HCA(thh_hob_p->cmd);
#else
        cmd_ret = THH_cmd_CLOSE_HCA(thh_hob_p->cmd, FALSE);
#endif
        if (cmd_ret != THH_CMD_STAT_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_cmd_CLOSE_HCA error (%d)\n", cmd_ret);
            have_error = TRUE;
        }
    }
    hca_hndl->status = HH_HCA_STATUS_CLOSED;

    /* move state to "CLOSED"*/
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    thh_hob_p->thh_state &= THH_STATE_HAVE_ANY_FATAL;
    thh_hob_p->thh_state |= THH_STATE_CLOSED;
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    MOSAL_mutex_rel(&(thh_hob_p->mtx));

    MTL_TRACE2("THH_hob_close_hca: device name %s\n", hca_hndl->dev_desc);

    if (have_fatal == FALSE) {
        WAIT_IF_FATAL(thh_hob_p);
    }
    if ((have_fatal == TRUE) && (invoked_from_destroy == FALSE)) {
    	//leo (WINDOWS) restart dosn't work, because the card reset doesn't work
  	#ifndef __WIN__
        MTL_TRACE1("THH_hob_close_hca: HAVE FATAL, restarting\n");
        ret = THH_hob_restart(hca_hndl);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_close_hca: THH_hob_restart error (%d)\n", ret);
            have_error = TRUE;
        }
        #endif
    }
    if (have_error && (have_fatal == FALSE)) {
        return HH_ERR;
    } else {
        return(HH_OK);
    }
} /* THH_hob_close_hca_internal */

HH_ret_t THH_hob_close_hca(HH_hca_hndl_t  hca_hndl)
{
    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    return (THH_hob_close_hca_internal(hca_hndl,FALSE));
}


/******************************************************************************
 *  Function:     THH_hob_destroy
 *
 *  Description:  Deregister given device from HH and free the HH object.
 *
 *  input:
 *                hca_hndl
 *  output: 
 *                none
 *  returns:
 *                HH_OK
 *                HH_EINVAL_HCA_HNDL
 *
 *  Comments:     If HCA is still open,THH_hob_close_hca() is 
 *                invoked before freeing the THH_hob.
 *
 *                Returns HH_EINVAL_HCA_HNDL if any function called internally fails  
 *
 *****************************************************************************/
HH_ret_t    THH_hob_destroy(HH_hca_hndl_t hca_hndl)
{
  HH_ret_t  ret, fn_ret = HH_OK;
  THH_cmd_status_t   cmd_ret;
  THH_hob_t          hob_p;
  int                int_ret = 0;
#if !defined(__DARWIN__) && !defined(__WIN__)
  MT_virt_addr_t     va;
#endif
  call_result_t      mosal_ret;
  MT_bool            have_fatal = FALSE;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_destroy: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_close_hca : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  
  /* return ERROR if device is still open */
  if (hca_hndl->status != HH_HCA_STATUS_CLOSED) {
      MTL_ERROR1("THH_hob_destroy:  Unloading device %s: while it is still open.  Attempting to close it.\n", hca_hndl->dev_desc);
      ret = THH_hob_close_hca_internal(hca_hndl, TRUE);
      if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_destroy: Could not close device %s: not opened or unknown (err=%d)\n", hca_hndl->dev_desc, ret);
        fn_ret = HH_EINVAL_HCA_HNDL;
      }
      hca_hndl->status = HH_HCA_STATUS_CLOSED;
  }

  MTL_TRACE2("THH_hob_destroy: removing the device %s\n",  hca_hndl->dev_desc);
  hob_p = THHOBP(hca_hndl);

  /* move the HCA to DESTROYING state, preserving fatal indicators */
  MOSAL_spinlock_irq_lock(&hob_p->fatal_spl);
  if ((hob_p->thh_state & THH_STATE_DESTROYING) != 0) {
      MOSAL_spinlock_unlock(&hob_p->fatal_spl);
      MTL_ERROR1(MT_FLFMT("THH_hob_destroy:  already invoked"));
      return HH_EBUSY;
  }
  hob_p->thh_state &= THH_STATE_HAVE_ANY_FATAL;
  hob_p->thh_state |= THH_STATE_DESTROYING;
  MOSAL_spinlock_unlock(&hob_p->fatal_spl);
  
#ifndef __DARWIN__ /* TODO: add support in darwin for fatal error handling */

  /* release the fatal signalling thread */
  hob_p->fatal_thread_obj.have_fatal = FALSE;
  MOSAL_syncobj_signal(&hob_p->fatal_thread_obj.fatal_err_sync);
  mosal_ret = MOSAL_syncobj_waiton(&(hob_p->fatal_thread_obj.stop_sync), 10000000);
  if (mosal_ret != MT_OK) {
      if (mosal_ret == MT_EINTR) {
          MTL_DEBUG1(MT_FLFMT("%s: Received OS interrupt while initializing fatal error thread (err = %d)"), 
                     __func__,mosal_ret);
          fn_ret = HH_EINTR;
      } else {
          MTL_ERROR1(MT_FLFMT("%s: Timeout on destroying fatal error thread (err = %d)"), 
                     __func__,mosal_ret);
          fn_ret = HH_ERR;
      }
  }

#endif /* not defined __DARWIN__ */

  /* unregister the device from HH */
  ret = HH_rem_hca_dev(hca_hndl);
  if (ret != HH_OK) {
      MTL_ERROR1("THH_hob_destroy: Could not remove device 0x%p: unknown (%d)\n", hca_hndl, ret);
      fn_ret = HH_EINVAL_HCA_HNDL;
  }

  /* destroy objects created in hob_create, and issue SYS_DIS command to Tavor */
  ret = THH_ddrmm_destroy(hob_p->ddrmm);
  if (ret != HH_OK) {
      MTL_ERROR1("THH_hob_destroy: Could not destroy ddrmm object (err = %d)\n", ret);
      fn_ret = HH_ERR;
  }
  /* do SYS_DIS only if do not have a fatal error state */
  if ((hob_p->thh_state & THH_STATE_HAVE_ANY_FATAL) == 0) {
      have_fatal = FALSE;
      cmd_ret = THH_cmd_SYS_DIS(hob_p->cmd);
      if (cmd_ret != THH_CMD_STAT_OK) {
          MTL_ERROR1("THH_hob_destroy: SYS_DIS command failed (err = %d)\n", cmd_ret);
          if (cmd_ret == THH_CMD_STAT_EFATAL) {
              have_fatal = TRUE;
          }
          fn_ret = HH_ERR;
      }
   } else {
      /* halt the HCA if delayed-halt flag was set, in fatal case only, 
       * to make sure that there is no PCI activity.  If fatal occurred during SYS_DIS above,
       * HCA was already closed, so don't need the "halt hca" operation
       */
      have_fatal = TRUE;
      if (hob_p->module_flags.fatal_delay_halt != 0) {
          MTL_DEBUG1(MT_FLFMT("%s: performing delayed halt-hca"), __func__);
          THH_hob_halt_hca(hob_p);
      }
   }
      
  ret = THH_cmd_destroy(hob_p->cmd);
  if (ret != HH_OK) {
      MTL_ERROR1("THH_hob_destroy: Could not destroy cmd object (err = %d)\n", ret);
      fn_ret = HH_ERR;
  }

  /* do PCI reset here if have a catastrophic error */
  if (have_fatal == TRUE) {
    /* perform sw reset */
      MTL_ERROR1(MT_FLFMT("%s: FATAL ERROR "), __func__);

#if !defined(__DARWIN__) && !defined(__WIN__)
      /* Do the Tavor RESET */
      va = MOSAL_io_remap(hob_p->hw_props.cr_base + 0xF0010, 4);
      if ( va ) {
          /* perform sw reset */
          MTL_ERROR1(MT_FLFMT("%s: PERFORMING SW RESET. pa="PHYS_ADDR_FMT" va="VIRT_ADDR_FMT),
                      __func__, hob_p->hw_props.cr_base + 0xF0010, va);
          MOSAL_MMAP_IO_WRITE_DWORD(((unsigned long)va),MOSAL_cpu_to_be32(0x00000001));
          /* sleep for one second, per PRM */
          MOSAL_delay_execution(1000000);
          MOSAL_io_unmap(va);
      }

      /* now, rewrite the PCI configuration */
      if (hob_p->pci_bridge_info.is_valid == TRUE) {
          write_pci_config(hob_p->pci_bridge_info.pci_dev,
                           hob_p->pci_bridge_info.config);
      }
      if (hob_p->pci_hca_info.is_valid == TRUE) {
          write_pci_config(hob_p->hw_props.pci_dev,
                           hob_p->pci_hca_info.config);
      }
#endif /* not defined __DARWIN__ */
  }

  int_ret = VIP_delay_unlock_destroy(hob_p->delay_unlocks);
  if (int_ret != 0) {
      MTL_ERROR1("THH_hob_destroy: Could not destroy delay_unlocks (err = %d)\n", int_ret);
      fn_ret = HH_ERR;
  }
  
  if (hob_p->fw_error_buf_start_va != (MT_virt_addr_t)(MT_ulong_ptr_t) NULL)  {
     MOSAL_io_unmap(hob_p->fw_error_buf_start_va);
  }

  if (hob_p->fw_error_buf != NULL) {
      FREE(hob_p->fw_error_buf);
  }

  MOSAL_mutex_free(&(hob_p->mtx));
  /* Finally, free the THH object */
  FREE(hca_hndl->device);
  hca_hndl->device = NULL;

  return(fn_ret);
}

/******************************************************************************
 *  Function:     THH_hob_open_hca
 *
 *  Description:  This function opens the given HCA and initializes the HCA with 
 *                given properties/ capabilities.  if prop_props_p is NULL a default 
 *                HCA profile will be set up.
 *
 *  input:
 *                hca_hndl
 *                prop_props_p - Proprietary properties (Non IB)
 *  output: 
 *                none
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EBUSY
 *
 *  Comments:     If HCA is still open,THH_hob_close_hca() is 
 *                invoked before freeing the THH_hob.
 *
 *                Returns HH_EINVAL_HCA_HNDL if any function called internally fails  
 *
 *****************************************************************************/
HH_ret_t THH_hob_open_hca(HH_hca_hndl_t  hca_hndl, 
                                 EVAPI_hca_profile_t  *prop_props_p,
                                 EVAPI_hca_profile_t  *sugg_profile_p)
{
    MT_virt_addr_t            kar_addr;
    HH_ret_t               ret;
    THH_cmd_status_t       cmd_ret;
    THH_hca_props_t        local_hca_props;
    MT_phys_addr_t         *ddr_alloc_area;
    MT_size_t              *ddr_alloc_size;
    u_int32_t              i;
    THH_internal_mr_t      udav_internal_mr;
    MT_size_t              udav_entry_size = 0, udav_table_size = 0;
    MT_phys_addr_t            udav_phys_addr = 0;
    MT_virt_addr_t            udav_virt_addr = 0;
    VAPI_lkey_t            dummy_key;
    THH_eventp_res_t       event_res;
    u_int16_t              num_ports, last_port_initialized;
    THH_hob_t              thh_hob_p;
    VAPI_size_t            udav_vapi_size;
    THH_qpm_init_t         thh_qpm_init_params;
    MT_bool                have_fatal = FALSE;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    MTL_DEBUG4("Entering THH_hob_open_hca\n");
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_hob_open_hca: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }


    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_open_hca : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
        MTL_ERROR1("THH_hob_open_hca: ERROR : No device registered\n");
        return HH_EAGAIN;
    }
  /* Get user profile if available -- if not, use default proportionally to resources. */

  /* DDR parameters.  Get from default profile.  For each one, check that does not exceed */
  /* The maximum resource value supportable by the installed card. */

/*
   Objects:
           cmd;     -- already exists
           ddrmm;   -- already exists
           uldm;    -- uar, pd:  log2_max_uar, log2_max_pg_sz, max_pd
           mrwm;    -- log2_mpt_sz (log2 of number of entries in MPT)
                       log2_mtt_sz (Log2 of number of entries in the MTT) 
                       max_mem_reg (Maximum memory regions to be allocated in the MPT for external registration only)
                       max_mem_reg_internal (Maximum memory regions to be alloc in the MPT for internal use only (WQEs and CQEs buffers) )
                       max_mem_win (Maximum memory windows to be allocated in the MPT)
           cqm;     -- log2_max_cq,
           eecm;    -- 
           qpm;     -- log2_max_qp,
                       privileged_ud_av (boolean)
           udavm;   -- max_av
           mcgm;    -- num mcg's: IBTA min = 512, 8 QPs/group
           eventp;  -- 64 event queues
           kar;     -- UAR 0 (no extra resources needed)
        
*/

    /* Test if have fatal error, and move thh_state to "opening" */
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    if ((thh_hob_p->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {
        /* already in FATAL state */
        MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
        MTL_DEBUG4(MT_FLFMT("%s: already in FATAL state"), __func__);  
        MT_RETURN(HH_EFATAL);
    } else if (thh_hob_p->thh_state != THH_STATE_CLOSED) {
        MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
        MTL_ERROR1(MT_FLFMT("THH_hob_open_hca: ERROR : Device not closed. state = 0x%x"),thh_hob_p->thh_state );
        MT_RETURN(HH_EBUSY);
    }
    thh_hob_p->thh_state = THH_STATE_OPENING;
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    
    /* get the MUTEX */
    if (MOSAL_mutex_acq(&(thh_hob_p->mtx), TRUE) != MT_OK) {
        MTL_ERROR1(MT_FLFMT("THH_hob_open_hca: received signal. returning"));
        ret = HH_EINTR;
        goto post_state_change_error;
    }
    if (hca_hndl->status == HH_HCA_STATUS_OPENED) {
        MTL_ERROR1("THH_hob_open_hca: ERROR : Device already open\n");
        ret = HH_EBUSY;
        goto post_mutex_acquire_err;
    }

    if (prop_props_p != NULL) {
        THH_PRINT_USR_PROFILE(prop_props_p);
    }
    ret = THH_calculate_profile(thh_hob_p, prop_props_p, sugg_profile_p);
    if (ret != HH_OK) {
      MTL_ERROR1(MT_FLFMT("THH_hob_open_hca: could not create internal profile (%d)"), ret);
      if (sugg_profile_p != NULL) {
          THH_PRINT_USR_PROFILE(sugg_profile_p);
      }
      //ret = HH_ERR;
      goto post_mutex_acquire_err;
    }
    if (sugg_profile_p != NULL) {
        THH_PRINT_USR_PROFILE(sugg_profile_p);
    }

    /* check profile against QUERY_DEV_LIMS data*/
    ret = THH_check_profile(thh_hob_p);
    if (ret != HH_OK) {
      MTL_ERROR1("THH_hob_open_hca: Profile check failed (%d)\n", ret);
      ret = HH_ERR;
      goto post_mutex_acquire_err;
    }

    /*  Do ddrmm allocation here, because we need the allocated MCG base address */
    /*  for the INIT_HCA command following the centralized DDR allocation */
    calculate_ddr_alloc_vec(thh_hob_p, &(thh_hob_p->profile), &(thh_hob_p->ddr_alloc_size_vec));

    /* Allocate all required DDR areas */
    ret = THH_ddrmm_alloc_sz_aligned(thh_hob_p->ddrmm, 
                             thh_hob_p->profile.ddr_alloc_vec_size,      /*number of chunks */
                             (MT_size_t *) &(thh_hob_p->ddr_alloc_size_vec), /* IN  */
                             (MT_phys_addr_t *)&(thh_hob_p->ddr_alloc_base_addrs_vec) );  /* OUT */
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not allocate required areas in DDR (%s)\n", HH_strerror_sym(ret));
        ret = HH_ERR;
        goto post_mutex_acquire_err;
    }
    
    /* call cmd interface to initialize its mailboxes in DDR */
    ret = THH_cmd_assign_ddrmm(thh_hob_p->cmd, thh_hob_p->ddrmm);
    if (ret != HH_OK) {
      MTL_ERROR1("THH_hob_open_hca: Failed THH_cmd_assign_ddrmm (%s)\n",HH_strerror_sym(ret)); 
      goto cmd_assign_ddrmm_err;
    }
    
    /* set up parameters for INIT HCA */
    memset(&local_hca_props, 0, sizeof(THH_hca_props_t));
#ifdef MT_LITTLE_ENDIAN
    local_hca_props.he = TAVOR_IF_HOST_LTLENDIAN;
#else
    local_hca_props.he = TAVOR_IF_HOST_BIGENDIAN;
#endif
    local_hca_props.re = FALSE;   /* not a router */
    local_hca_props.udp = TRUE;   /* check port in UD AV */ 
    local_hca_props.ud =  thh_hob_p->profile.use_priv_udav; 
    
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.cqc_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.cqc_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.eec_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.eec_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.eqc_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.eqc_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.qpc_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.qpc_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.rdb_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.rdb_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.log_num_of_cq = 
      (u_int8_t)thh_hob_p->profile.log2_max_cqs;
    // local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.log_num_of_ee = thh_hob_p->profile.log2_max_eecs;
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.log_num_of_ee = 0;
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.log_num_eq = thh_hob_p->profile.log2_max_eqs;
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.log_num_of_qp = (u_int8_t)thh_hob_p->profile.log2_max_qps;
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.eqpc_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.eqpc_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.log_num_of_srq = (u_int8_t)thh_hob_p->profile.log2_max_srqs;
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.srqc_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.srqc_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.qpc_eec_cqc_eqc_rdb_parameters.eeec_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.eeec_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    
    local_hca_props.udavtable_memory_parameters.l_key = THH_UDAVM_PRIV_RESERVED_LKEY;
    local_hca_props.udavtable_memory_parameters.pd    = THH_RESERVED_PD;
    local_hca_props.udavtable_memory_parameters.xlation_en = TRUE;
    
    local_hca_props.tpt_parameters.log_mpt_sz = (u_int8_t)thh_hob_p->profile.log2_max_mpt_entries;
    local_hca_props.tpt_parameters.mpt_base_adr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.mpt_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.tpt_parameters.mtt_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.mtt_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.tpt_parameters.pfto = 0;   /* TBD -- not yet supported. Page Fault RNR Timeout */
    local_hca_props.tpt_parameters.mtt_segment_size = (u_int8_t)((thh_hob_p->profile.log2_mtt_entries_per_seg + THH_DDR_LOG2_MTT_ENTRY_SIZE)
                                                            - THH_DDR_LOG2_MIN_MTT_SEG_SIZE);
    
    local_hca_props.uar_parameters.uar_base_addr = thh_hob_p->hw_props.uar_base;
    local_hca_props.uar_parameters.uar_page_sz   = thh_hob_p->profile.log2_uar_pg_size - 12;
    local_hca_props.uar_parameters.uar_scratch_base_addr = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.uar_scratch_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    
    local_hca_props.multicast_parameters.log_mc_table_sz = (u_int8_t)thh_hob_p->profile.log2_max_mcgs;
    local_hca_props.multicast_parameters.mc_base_addr    = 
        GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.mcg_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    local_hca_props.multicast_parameters.mc_hash_fn      = 0;
    local_hca_props.multicast_parameters.log_mc_table_entry_sz = (u_int16_t)(thh_hob_p->profile.log2_mcg_entry_size);
    local_hca_props.multicast_parameters.mc_table_hash_sz = 1 << (thh_hob_p->profile.log2_mcg_hash_size);

  /* INIT_HCA command */
    cmd_ret = THH_cmd_INIT_HCA(thh_hob_p->cmd,&local_hca_props);
    if (cmd_ret != THH_CMD_STAT_OK) {
        MTL_ERROR1("THH_hob_open_hca: CMD_error in THH_cmd_INIT_HCA (%d)\n", cmd_ret);
        ret = HH_EAGAIN;
        goto init_hca_err;
    }

  /* Now, query HCA to get actual allocated parameters */
    cmd_ret = THH_cmd_QUERY_HCA(thh_hob_p->cmd, &(thh_hob_p->hca_props));
    if (cmd_ret != THH_CMD_STAT_OK) {
        MTL_ERROR1("THH_hob_open_hca: CMD_error in THH_cmd_QUERY_HCA (%d)\n", cmd_ret);
        ret = HH_EAGAIN;
        goto query_hca_err;
    }
    
    /* create uldm */
    ret = THH_uldm_create(thh_hob_p, thh_hob_p->hw_props.uar_base, (u_int8_t) thh_hob_p->profile.log2_max_uar,
                           (u_int8_t) thh_hob_p->profile.log2_uar_pg_size, 
                           (u_int32_t) (thh_hob_p->profile.max_num_pds + thh_hob_p->dev_lims.num_rsvd_pds + THH_NUM_RSVD_PD),
                           &(thh_hob_p->uldm));                      if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not create uldm (%d)\n", ret);
        goto uldm_create_err;
    }

    thh_hob_p->mrwm_props.mtt_base = GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.mtt_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    thh_hob_p->mrwm_props.mpt_base = GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.mpt_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr);
    thh_hob_p->mrwm_props.log2_mpt_sz = (u_int8_t)thh_hob_p->profile.log2_max_mpt_entries;
    thh_hob_p->mrwm_props.log2_mtt_sz = (u_int8_t)thh_hob_p->profile.log2_max_mtt_entries;
    thh_hob_p->mrwm_props.log2_mtt_seg_sz = (u_int8_t)thh_hob_p->profile.log2_mtt_entries_per_seg;
    thh_hob_p->mrwm_props.max_mem_reg = thh_hob_p->profile.num_external_mem_regions;
    thh_hob_p->mrwm_props.max_mem_reg_internal = thh_hob_p->profile.max_num_qps + thh_hob_p->profile.max_num_cqs;
    thh_hob_p->mrwm_props.max_mem_win          = thh_hob_p->profile.num_mem_windows;
    thh_hob_p->mrwm_props.log2_max_mtt_segs    = (u_int8_t)(thh_hob_p->profile.log2_max_mtt_entries - 
                                                           thh_hob_p->mrwm_props.log2_mtt_seg_sz);
    thh_hob_p->mrwm_props.log2_rsvd_mpts       = thh_hob_p->dev_lims.log2_rsvd_mrws;
    thh_hob_p->mrwm_props.log2_rsvd_mtt_segs   = thh_hob_p->dev_lims.log2_rsvd_mtts;

    ret = THH_mrwm_create(thh_hob_p, &(thh_hob_p->mrwm_props), &(thh_hob_p->mrwm));
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not create mrwm (%d)\n", ret);
        goto mrwm_create_err;
    }
    
    /* Create objects */
    ret = THH_cqm_create(thh_hob_p, (u_int8_t) thh_hob_p->profile.log2_max_cqs,
                         thh_hob_p->dev_lims.log2_rsvd_cqs, &(thh_hob_p->cqm));
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not create cqm (%d)\n", ret);
        goto cqm_create_err;
    }
    
    /* create qpm object here */

    /* initialize INIT_IB parameters here for possible use by qpm */
    num_ports = thh_hob_p->dev_lims.num_ports;

    thh_hob_p->init_ib_props = (THH_port_init_props_t *) MALLOC(num_ports * sizeof(THH_port_init_props_t));
    if (!(thh_hob_p->init_ib_props)) {
        MTL_ERROR1( "THH_hob_open_hca: ERROR : cannot allocate memory for port init props)\n");
        goto init_ib_props_malloc_err;
    }
    for (i = 1; i <= num_ports; i++) {
        /* redundant for now. However, leaving option for setting different properties per port */
        thh_hob_p->init_ib_props[i-1].e = TRUE;
        thh_hob_p->init_ib_props[i-1].g0 = FALSE;
        thh_hob_p->init_ib_props[i-1].max_gid = (1 << (thh_hob_p->dev_lims.log_max_gid));
        thh_hob_p->init_ib_props[i-1].mtu_cap = thh_hob_p->dev_lims.max_mtu;
        thh_hob_p->init_ib_props[i-1].max_pkey = (1 << (thh_hob_p->dev_lims.log_max_pkey));
        thh_hob_p->init_ib_props[i-1].vl_cap   = thh_hob_p->dev_lims.max_vl;
        thh_hob_p->init_ib_props[i-1].port_width_cap = thh_hob_p->dev_lims.max_port_width;
    }
    
    memset(&(thh_qpm_init_params), 0, sizeof(thh_qpm_init_params));
    thh_qpm_init_params.rdb_base_index = 
        /* 32 low-order bits, right-shifted by log of size of rdb entry */
      (u_int32_t)(((u_int64_t)(GET_DDR_ADDR(thh_hob_p->ddr_alloc_base_addrs_vec.rdb_base_addr,thh_hob_p->ddr_props.dh,
                          thh_hob_p->ddr_props.ddr_start_adr))) & (u_int64_t)0xFFFFFFFF ) 
                                  >> THH_DDR_LOG2_RDB_ENTRY_SIZE;
    thh_qpm_init_params.log2_max_qp = (u_int8_t) thh_hob_p->profile.log2_max_qps;
    thh_qpm_init_params.log2_max_outs_rdma_atom = thh_hob_p->profile.log2_inflight_rdma_per_qp;
    thh_qpm_init_params.log2_max_outs_dst_rd_atom = thh_hob_p->dev_lims.log_max_ra_req_qp;
    thh_qpm_init_params.n_ports = (u_int8_t)num_ports;
    thh_qpm_init_params.port_props = 
        (thh_hob_p->module_flags.legacy_sqp == TRUE ? NULL : thh_hob_p->init_ib_props );
    thh_qpm_init_params.log2_rsvd_qps = thh_hob_p->dev_lims.log2_rsvd_qps;
    
    ret = THH_qpm_create(thh_hob_p, &(thh_qpm_init_params), &(thh_hob_p->qpm));
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not create qpm %s(%d)\n", HH_strerror_sym(ret), ret);
        goto qpm_create_err;
    }

    if (thh_hob_p->dev_lims.srq) {
      ret= THH_srqm_create(thh_hob_p, 
                           (u_int8_t)thh_hob_p->profile.log2_max_srqs, thh_hob_p->dev_lims.log2_rsvd_srqs,
                           &(thh_hob_p->srqm));
      if (ret != HH_OK) {
          MTL_ERROR1("THH_hob_open_hca: could not create srqm - %s(%d)\n", HH_strerror_sym(ret), ret);
          goto srqm_create_err;
      }
    } else {
      thh_hob_p->srqm= (THH_srqm_t)THH_INVALID_HNDL; /* SRQs are not supported */
    }

    /* CREATE ALL CONTAINED OBJECTS  */
    
    /* create UDAVm if privileged UDAV is set */
    if (thh_hob_p->profile.use_priv_udav) {
        
        thh_hob_p->udavm_use_priv = TRUE;
        
        /* create the table in DDR memory */
        udav_entry_size = (unsigned)(sizeof(struct tavorprm_ud_address_vector_st) / 8);
        udav_table_size = thh_hob_p->profile.max_priv_udavs * udav_entry_size;

        ret = THH_ddrmm_alloc(thh_hob_p->ddrmm, udav_table_size, ceil_log2(udav_entry_size),
                          &udav_phys_addr);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_open_hca: could not allocate protected udavm area in DDR(err = %d)\n", ret);
            goto udavm_ddrmm_alloc_err;
        }
        udav_virt_addr = (MT_virt_addr_t) MOSAL_map_phys_addr( udav_phys_addr , udav_table_size,
                                 MOSAL_MEM_FLAGS_NO_CACHE | MOSAL_MEM_FLAGS_PERM_WRITE | MOSAL_MEM_FLAGS_PERM_READ , 
                                 MOSAL_get_kernel_prot_ctx());
        if (udav_virt_addr == (MT_virt_addr_t) NULL) {
            MTL_ERROR1("THH_hob_open_hca: could not map physical address " PHYS_ADDR_FMT " to virtual\n", 
                       udav_phys_addr);
            goto udavm_mosal_map_err;
        }

        memset(&udav_internal_mr, 0, sizeof(udav_internal_mr));
        udav_internal_mr.force_memkey = TRUE;
        udav_internal_mr.memkey       = THH_UDAVM_PRIV_RESERVED_LKEY;
        udav_internal_mr.pd           = THH_RESERVED_PD;
        udav_internal_mr.size         = udav_table_size;
        udav_internal_mr.start        = udav_virt_addr;
        udav_internal_mr.vm_ctx       = MOSAL_get_kernel_prot_ctx();
        if (udav_phys_addr) {
            VAPI_phy_addr_t udav_phy =  udav_phys_addr;
            udav_internal_mr.num_bufs = 1;      /*  != 0   iff   physical buffesrs supplied */
            udav_internal_mr.phys_buf_lst = &udav_phy;  /* size = num_bufs */
            udav_vapi_size  = (VAPI_size_t) udav_table_size;
            udav_internal_mr.buf_sz_lst = &udav_vapi_size;    /* [num_bufs], corresponds to phys_buf_lst */
        }

        thh_hob_p->udavm_table_size = udav_table_size;
        thh_hob_p->udavm_table      = udav_virt_addr;
        thh_hob_p->udavm_table_ddr  = udav_phys_addr;

        ret = THH_mrwm_register_internal(thh_hob_p->mrwm, &udav_internal_mr, &dummy_key);
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_open_hca: could not register created udavm table (%d)\n", ret);
            goto udavm_table_register_err;
        }
        thh_hob_p->udavm_lkey = dummy_key;

        ret = THH_udavm_create(&(thh_hob_p->version_info), 
                               dummy_key,
                               udav_table_size,
                               udav_virt_addr,
                               &(thh_hob_p->udavm));
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_open_hca: could not create udavm (%d)\n", ret);
            goto udavm_create_err;
        }

    } else {
        thh_hob_p->udavm_use_priv = FALSE;
    }

    /* CREATE KAR (kernel UAR), using UAR 1 for this purpose. */
    kar_addr = (MT_virt_addr_t) MOSAL_map_phys_addr( thh_hob_p->hw_props.uar_base + (1 << thh_hob_p->profile.log2_uar_pg_size),
                                                 (1 << thh_hob_p->profile.log2_uar_pg_size),
                                                 MOSAL_MEM_FLAGS_NO_CACHE | MOSAL_MEM_FLAGS_PERM_WRITE, 
                                                 MOSAL_get_kernel_prot_ctx());
    if (kar_addr == (MT_virt_addr_t) NULL) {
#ifndef __DARWIN__
        MTL_ERROR1("THH_hob_open_hca: MOSAL_map_phys_addr failed for prot ctx %d, addr " PHYS_ADDR_FMT ", size %d\n",
                   MOSAL_get_kernel_prot_ctx(),
                   (MT_phys_addr_t) (thh_hob_p->hw_props.uar_base),
                   (1 << thh_hob_p->profile.log2_uar_pg_size));
#else
        MTL_ERROR1("THH_hob_open_hca: MOSAL_map_phys_addr failed: addr " PHYS_ADDR_FMT ", size %d\n",
                   (MT_phys_addr_t) (thh_hob_p->hw_props.uar_base),
                   (1 << thh_hob_p->profile.log2_uar_pg_size));
#endif
        goto kar_map_phys_addr_err;
    }
    thh_hob_p->kar_addr = kar_addr;
    MTL_DEBUG4("%s: MOSAL_map_phys_addr FOR KAR = " VIRT_ADDR_FMT "\n", __func__, kar_addr);
    ret = THH_uar_create(&(thh_hob_p->version_info), 1/* Kernel UAR page index */, 
                         (void *) kar_addr, &(thh_hob_p->kar));
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not create KAR (%d)\n", ret);
        thh_hob_p->kar_addr = (MT_virt_addr_t) 0;
        goto kar_create_err;
    }
    
    //sharon: wrote fixed numbers till fw bug fixed
    if (THH_DEV_LIM_MCG_ENABLED(thh_hob_p)) {
        ret = THH_mcgm_create(thh_hob_p, 
                          (1 << thh_hob_p->hca_props.multicast_parameters.log_mc_table_sz),
                          /*thh_hob_p->hca_props.multicast_parameters.mc_table_hash_sz*/
                          1 << (thh_hob_p->profile.log2_mcg_hash_size),
                          (u_int16_t)thh_hob_p->profile.qps_per_mcg,           
                           &(thh_hob_p->mcgm) );
        if (ret != HH_OK) {
            MTL_ERROR1("THH_hob_open_hca: could not create mcgm (%d)\n", ret);
            thh_hob_p->hca_capabilities.max_mcast_grp_num = 0;            
            thh_hob_p->hca_capabilities.max_mcast_qp_attach_num = 0;      
            thh_hob_p->hca_capabilities.max_total_mcast_qp_attach_num = 0;
            thh_hob_p->mcgm = (THH_mcgm_t)THH_INVALID_HNDL;
        }
    }

    /* CREATE EVENTP*/
    event_res.cr_base = thh_hob_p->hw_props.cr_base;
    event_res.intr_clr_bit = thh_hob_p->adapter_props.intapin;
    event_res.irq = thh_hob_p->hw_props.interrupt_props.irq;
    event_res.is_srq_enable = thh_hob_p->dev_lims.srq;

    ret = THH_eventp_create (thh_hob_p, &event_res, thh_hob_p->kar, &(thh_hob_p->eventp));
    if (ret != HH_OK) {
        MTL_ERROR1("THH_hob_open_hca: could not create eventp (%d)\n", ret);
        goto eventp_create_err;
    }

    /* CREATE THE VARIOUS EVENT QUEUES (eventp object operations) */
    /* register dummy completion and async event handlers */
    /* set max outstanding EQEs to max number of CQs configured */

    ret = THH_eventp_setup_ib_eq(thh_hob_p->eventp, 
                                 &THH_dummy_async_event,
                                 NULL, 
                                 (MT_size_t)(thh_hob_p->module_flags.async_eq_size == 0 ? 
                                             THH_MAX_ASYNC_EQ_SIZE :
                                             thh_hob_p->module_flags.async_eq_size ), 
                                 &(thh_hob_p->ib_eq));
    if (ret != HH_OK) {
        MTL_ERROR1(MT_FLFMT("THH_hob_open_hca: ERROR : cannot set up async event queue for size "SIZE_T_DFMT" (ret=%d)"), 
                   (MT_size_t)(thh_hob_p->module_flags.async_eq_size == 0 ? 
                               THH_MAX_ASYNC_EQ_SIZE :thh_hob_p->module_flags.async_eq_size ),ret);
        goto eventp_async_err;
    }

    ret = THH_eventp_setup_comp_eq(thh_hob_p->eventp, 
                                   &THH_dummy_comp_event , 
                                   NULL,
                                   (MT_size_t)(1 <<(thh_hob_p->profile.log2_max_cqs)) - 
                                          (MT_size_t)(1ul << (thh_hob_p->dev_lims.log2_rsvd_cqs)), 
                                   &(thh_hob_p->compl_eq));
    if (ret != HH_OK) {
        MTL_ERROR1( "THH_hob_open_hca: ERROR : cannot set up completion event queue (%d)\n", ret);
        goto eventp_compl_err;
    }


    /* PERFORM INIT_IB only for legacy SQPs.  Use max values obtained from QUERY_DEV_LIMS */

    last_port_initialized = 0;
    
	if (thh_hob_p->module_flags.legacy_sqp == TRUE) { 
		for (i = 1; i <= num_ports; i++) {
			MTL_TRACE2("THH_hob_open_hca: INIT_IB COMMAND\n");
			cmd_ret = THH_cmd_INIT_IB(thh_hob_p->cmd, (IB_port_t) i, &(thh_hob_p->init_ib_props[i-1]));
			if (cmd_ret != THH_CMD_STAT_OK) {
				MTL_ERROR1("THH_hob_open_hca: CMD_error in THH_cmd_INIT_IB (%d) for port %d\n", cmd_ret, i);
                if (cmd_ret ==THH_CMD_STAT_EFATAL) {
                    ret = HH_EFATAL;
                } else {
                    ret = HH_EAGAIN;
                }
				goto init_ib_err;
			}

			else {
				MTL_TRACE2("THH_hob_open_hca: INIT_IB COMMAND completed successfuly\n");
			}
			last_port_initialized++;
		}
	}


	/* This must be called after INIT_IB, since it uses the max_pkey value stored in that struct */
    MTL_TRACE2("THH_hob_open_hca:  Before THH_hob_query_struct_init\n");
    ret = THH_hob_query_struct_init(thh_hob_p, 
                              ((prop_props_p == NULL)? FALSE : TRUE),
                              &(thh_hob_p->hca_capabilities));
    if (ret != HH_OK) {
        MTL_ERROR1( "THH_hob_query_struct_init: ERROR : cannot initialize data for query_hca (%d)\n", ret);
        goto query_struct_init_err;
    }

    /* TK - start events after all CMDs are done */

#if (! defined __DARWIN__) || (defined DARWIN_WITH_INTERRUPTS_CMDIF)
    ret = THH_eventp_setup_cmd_eq(thh_hob_p->eventp, CMD_EQ_SIZE /* to overcome a FW bug */
                                  /*(1 << (thh_hob_p->fw_props.log_max_outstanding_cmd))*/ );
    if (ret != HH_OK) {
        MTL_ERROR1( "THH_hob_open_hca: ERROR : cannot set up command event queue (%d)\n", ret);
        goto eventp_cmd_err;
    }
#endif    

    /* move the HCA to running state if had no fatal. If had fatal, return HH_EFATAL */
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    if ((thh_hob_p->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {
        /* already in FATAL state */
        MTL_DEBUG4(MT_FLFMT("%s: already in FATAL state"), __func__);  
        MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
        ret = HH_EFATAL;
        goto fatal_err_at_end;
    }
    thh_hob_p->thh_state = THH_STATE_RUNNING;
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    
    MTL_TRACE2("THH_hob_open_hca: device name %s\n", hca_hndl->dev_desc);
    hca_hndl->status   =   HH_HCA_STATUS_OPENED;
    
    /* free the mutex */
    MOSAL_mutex_rel(&(thh_hob_p->mtx));

    return(HH_OK);

fatal_err_at_end:
eventp_cmd_err:
query_struct_init_err:
init_ib_err:
    /* see if need to close IB for some ports. Do not close ports on fatal error*/
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    /*test fatal again, here -- may have gotten FATAL before end of OPEN_hca process */
    if ((thh_hob_p->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {
        /* got FATAL during OPEN_HCA */
        MTL_DEBUG4(MT_FLFMT("THH_hob_open_hca: In FATAL state")); 
        have_fatal = TRUE;
    } else {
        have_fatal = FALSE;
    }
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    if ((last_port_initialized) && (have_fatal == FALSE)) {
        for (i = 1; i <= last_port_initialized; i++) {
            MTL_DEBUG4(MT_FLFMT("THH_hob_open_hca: closing IB port %d"), i); 
            THH_cmd_CLOSE_IB(thh_hob_p->cmd, (IB_port_t) i);
        }
    }
    thh_hob_p->compl_eq = THH_INVALID_EQN;

eventp_compl_err:
    thh_hob_p->ib_eq = THH_INVALID_EQN;

eventp_async_err:
    THH_eventp_destroy(thh_hob_p->eventp);

eventp_create_err:
    if (thh_hob_p->mcgm != (THH_mcgm_t)THH_INVALID_HNDL) {
        THH_mcgm_destroy(thh_hob_p->mcgm );
        thh_hob_p->mcgm = (THH_mcgm_t)THH_INVALID_HNDL;
    }

    THH_uar_destroy(thh_hob_p->kar);
    thh_hob_p->kar = (THH_uar_t)THH_INVALID_HNDL;

kar_create_err:
    MOSAL_unmap_phys_addr(MOSAL_get_kernel_prot_ctx(), (MT_virt_addr_t) kar_addr, 
                               (1 << thh_hob_p->profile.log2_uar_pg_size));
    thh_hob_p->kar_addr = (MT_virt_addr_t) 0;

kar_map_phys_addr_err:
    if (thh_hob_p->profile.use_priv_udav) {
        THH_udavm_destroy(thh_hob_p->udavm);
    }
    thh_hob_p->udavm = (THH_udavm_t)THH_INVALID_HNDL;
udavm_create_err:
    if (thh_hob_p->profile.use_priv_udav) {
        THH_mrwm_deregister_mr(thh_hob_p->mrwm, dummy_key);
        thh_hob_p->udavm_lkey = 0;
    }
udavm_table_register_err:
    if (thh_hob_p->profile.use_priv_udav) {
        MOSAL_unmap_phys_addr( MOSAL_get_kernel_prot_ctx(), (MT_virt_addr_t) udav_virt_addr , udav_table_size );
        thh_hob_p->udavm_table = (MT_virt_addr_t) NULL;
    }

udavm_mosal_map_err:
    if (thh_hob_p->profile.use_priv_udav) {
        THH_ddrmm_free(thh_hob_p->ddrmm, udav_phys_addr, udav_table_size);
        thh_hob_p->udavm_table_ddr  = (MT_phys_addr_t) 0;
        thh_hob_p->udavm_table_size = 0;
    }

udavm_ddrmm_alloc_err:
    THH_srqm_destroy(thh_hob_p->srqm);
    thh_hob_p->srqm = (THH_srqm_t)THH_INVALID_HNDL;

srqm_create_err:
    THH_qpm_destroy( thh_hob_p->qpm, TRUE);
    thh_hob_p->qpm = (THH_qpm_t)THH_INVALID_HNDL;

qpm_create_err:
    FREE(thh_hob_p->init_ib_props);
    thh_hob_p->init_ib_props = ( THH_port_init_props_t *) NULL;

init_ib_props_malloc_err:
    THH_cqm_destroy( thh_hob_p->cqm, TRUE);
    thh_hob_p->cqm = (THH_cqm_t)THH_INVALID_HNDL;

cqm_create_err:
    THH_mrwm_destroy(thh_hob_p->mrwm, TRUE);
    thh_hob_p->mrwm = (THH_mrwm_t)THH_INVALID_HNDL;

mrwm_create_err:
    THH_uldm_destroy(thh_hob_p->uldm );
    thh_hob_p->uldm = (THH_uldm_t)THH_INVALID_HNDL;
uldm_create_err:
query_hca_err:
#ifdef SIMULATE_HALT_HCA
    THH_cmd_CLOSE_HCA(thh_hob_p->cmd);
#else
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    /*test fatal again, here -- may have gotten FATAL before end of OPEN_hca process */
    if ((thh_hob_p->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {
        /* got FATAL during OPEN_HCA */
        MTL_DEBUG4(MT_FLFMT("THH_hob_open_hca: In FATAL state")); 
        have_fatal = TRUE;
    } else {
        have_fatal = FALSE;
    }
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    if (have_fatal) {
        if (thh_hob_p->module_flags.fatal_delay_halt == 0) {
            MTL_DEBUG1(MT_FLFMT("%s: halting the HCA"), __func__);
            THH_cmd_CLOSE_HCA(thh_hob_p->cmd, TRUE);
        }
    } else {
        MTL_DEBUG1(MT_FLFMT("%s: closing the HCA on non-fatal error %d"), __func__, ret);
        THH_cmd_CLOSE_HCA(thh_hob_p->cmd, FALSE);
    }
#endif

init_hca_err:
    THH_cmd_revoke_ddrmm(thh_hob_p->cmd);

cmd_assign_ddrmm_err:

    ddr_alloc_area = (MT_phys_addr_t *) &(thh_hob_p->ddr_alloc_base_addrs_vec);
    ddr_alloc_size = (MT_size_t *)&(thh_hob_p->ddr_alloc_size_vec);
    for (i = 0; i < thh_hob_p->profile.ddr_alloc_vec_size; i++, ddr_alloc_area++, ddr_alloc_size++) {
        if (*ddr_alloc_area != THH_DDRMM_INVALID_PHYS_ADDR) 
          /* Do not free in case skipped during allocation (e.g., SRQC) */
          THH_ddrmm_free(thh_hob_p->ddrmm,*ddr_alloc_area, (1U<< (*ddr_alloc_size)));
    }

post_mutex_acquire_err:
    MOSAL_mutex_rel(&(thh_hob_p->mtx));
    
post_state_change_error:
    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    /*test fatal again, here -- may have gotten FATAL before end of OPEN_hca process */
    if ((thh_hob_p->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {
        /* got FATAL during OPEN_HCA */
        MTL_DEBUG4(MT_FLFMT("THH_hob_open_hca: In FATAL state")); 
        have_fatal = TRUE;
    } else {
        /* restore the state to closed */
        have_fatal = FALSE;
        thh_hob_p->thh_state = THH_STATE_CLOSED;
    }
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    if (have_fatal) {
        THH_hob_restart(hca_hndl);
    }
    return ret;
}

/******************************************************************************
 *  Function:     THH_hob_query
 *
 *  Description:  Implements VAPI_query_hca verb.  Data is already stored in HOB object.
 *
 *  input:
 *                hca_hndl
 *  output: 
 *                hca_cap_p -- pointer to output structure
 *  returns:
 *                HH_OK
 *                HH_EINVAL_HCA_HNDL
 *
 *****************************************************************************/
 HH_ret_t THH_hob_query(HH_hca_hndl_t  hca_hndl, 
                               VAPI_hca_cap_t *hca_cap_p)
{
    THH_hob_t             hob;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_hob_query: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_query : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    
    hob = THHOBP(hca_hndl);

    if (hob == NULL) {
        MTL_ERROR1("THH_hob_query : ERROR : No device registered\n");
        return HH_EINVAL;
    }
    
    TEST_RETURN_FATAL(hob);

    /* check if ib_init_props have been created -- indicates that open_hca called for this device */
    if (hob->init_ib_props == (THH_port_init_props_t *)NULL) {
        MTL_ERROR1("THH_hob_query: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }

    memcpy(hca_cap_p, &(hob->hca_capabilities), sizeof(VAPI_hca_cap_t));
    return HH_OK;
}
#define SET_MAX_SG(a)  (((a) < (u_int32_t)hob->dev_lims.max_sg) ? (a) : hob->dev_lims.max_sg)
/******************************************************************************
 *  Function:     THH_hob_query_struct_init
 *
 *  Description:  Pre-computes the data for VAPI_query_hca.  Called during THH_hob_open_hca()
 *
 *  input:
 *                hob
 *  output: 
 *                hca_cap_p -- pointer to output structure
 *  returns:
 *                HH_OK
 *                HH_EINVAL_HCA_HNDL
 *
 *  Comments:     Needs to use a MAD query for some of the parameters
 *
 *****************************************************************************/
static  HH_ret_t THH_hob_query_struct_init(THH_hob_t  hob,
                               MT_bool have_usr_profile, 
                               VAPI_hca_cap_t *hca_cap_p)
{
    HH_ret_t  ret;
    u_int32_t flags = 0;
    u_int32_t log2_num_spare_segs = 0;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  MTL_DEBUG4("Entering THH_hob_query_struct_init\n");
        /* Maximum Number of QPs supported.                   */
#if 0
  hca_cap_p->max_num_qp = hob->profile.max_num_qps - THH_NUM_RSVD_QP -
                    ((have_usr_profile == FALSE)? 0 : (1U<<hob->dev_lims.log2_rsvd_qps) );
#else
  hca_cap_p->max_num_qp = (u_int32_t)hob->profile.max_num_qps;
#endif
  hca_cap_p->max_num_srq = (u_int32_t)hob->profile.max_num_srqs;
        /* Maximum Number of oustanding WR on any WQ.         */
  hca_cap_p->max_qp_ous_wr = (1 << (hob->dev_lims.log_max_qp_sz)) - 1;                    
  hca_cap_p->max_wqe_per_srq = (1 << (hob->dev_lims.log_max_srq_sz)) - 1;                    
        
  /* Various flags (VAPI_hca_cap_flags_t)               */

//  VAPI_RESIZE_OUS_WQE_CAP     = 1  /* Not currently supported */

  flags =  (hob->hca_props.udp ? VAPI_UD_AV_PORT_ENFORCE_CAP : 0) |
           (hob->dev_lims.apm ? VAPI_AUTO_PATH_MIG_CAP : 0 )      |
           (hob->dev_lims.rm  ? VAPI_RAW_MULTI_CAP     : 0)       |
           (hob->dev_lims.pkv ? VAPI_BAD_PKEY_COUNT_CAP : 0)      |
           (hob->dev_lims.qkv ? VAPI_BAD_QKEY_COUNT_CAP : 0)      |
           VAPI_CHANGE_PHY_PORT_CAP | VAPI_RC_RNR_NAK_GEN_CAP | VAPI_PORT_ACTIVE_EV_CAP;
           

  hca_cap_p->flags = flags; 
        /* Max num of scatter/gather entries for desc other than RD */
  hca_cap_p->max_num_sg_ent = SET_MAX_SG(28);
        /* Max num of scatter entries for SRQs */
  hca_cap_p->max_srq_sentries = SET_MAX_SG(31);
        /* Max num of scatter/gather entries for RD desc - not supported  */
  hca_cap_p->max_num_sg_ent_rd = 0 ;
        /* Max num of supported CQs                           */
#if 0
  hca_cap_p->max_num_cq = hob->profile.max_num_cqs -
             ((have_usr_profile == FALSE)? 0 : (1U<<hob->dev_lims.log2_rsvd_cqs) );
#else
  hca_cap_p->max_num_cq = (u_int32_t)hob->profile.max_num_cqs;
#endif        
  /* Max num of supported entries per CQ                */
  hca_cap_p->max_num_ent_cq = (1 << (hob->dev_lims.log_max_cq_sz)) - 1 /*for extra cqe needed */;      
        /* Maximum number of memory region supported.         */
#if 0
  hca_cap_p->max_num_mr = hob->profile.num_external_mem_regions -           
                    ((have_usr_profile == FALSE)? 0 : (1U<<hob->dev_lims.log2_rsvd_mtts) );
#else
  hca_cap_p->max_num_mr = (u_int32_t)hob->profile.num_external_mem_regions;          
#endif        
        /* Largest contigous block of memory region in bytes. This may be achieved by registering
         * PHYSICAL memory directly for the region, (and using a page size for the region equal to
         * the size of the physical memory block you are registering).  The PRM allocates 5 bytes
         * for registering the log2 of the page size (with a 4K page size having page-size val 0).
         * Thus, the maximum page size per MTT entry is 4k * 2^31 (= 2^(31+12).  A single region 
         * can include multiple entries, and we can use all the spare MTT entries available for
         * this HUGE region.  The driver requires that every memory region have at least a single
         * segment available for registration.  We can thus use all the spare segments (we have
         * allocated 2 segments per region, but only really need one) to this region. If there are
         * no spare MTT entries, we calculate the value based on the usual mtt entries per segment.
         * This will still be a HUGE number (probably 2^46 or greater). 
         */
  if (hob->profile.log2_mtt_segs_per_region == 0)
      log2_num_spare_segs = (u_int32_t)hob->profile.log2_mtt_entries_per_seg;
  else 
      log2_num_spare_segs  = (u_int32_t)(hob->profile.log2_max_mtt_entries - hob->profile.log2_mtt_segs_per_region);

  /* check that do not overflow 2^64 !! */
  if (log2_num_spare_segs >= 21) 
       hca_cap_p->max_mr_size = MAKE_ULONGLONG(0xFFFFFFFFFFFFFFFF);
  else
       hca_cap_p->max_mr_size = (((u_int64_t)1L) << (31 + 12 + log2_num_spare_segs)) ;         
        /* Maximum number of protection domains supported.    */
#if 0
  hca_cap_p->max_pd_num = hob->profile.max_num_pds - THH_NUM_RSVD_PD - 
      ((have_usr_profile == FALSE)? 0 : hob->dev_lims.num_rsvd_pds );
#else
  hca_cap_p->max_pd_num = (u_int32_t)hob->profile.max_num_pds;
#endif
  /* Largest page size supported by this HCA            */
  hca_cap_p->page_size_cap = (1 << (hob->dev_lims.log_pg_sz));       
        /* Number of physical ports of the HCA.                */
  hca_cap_p->phys_port_num = hob->dev_lims.num_ports;       
        /* Maximum number of partitions supported .           */
  hca_cap_p->max_pkeys = hob->init_ib_props[0].max_pkey;           
        /* Maximum number of oust. RDMA read/atomic as target */
  hca_cap_p->max_qp_ous_rd_atom = 1 << (hob->profile.log2_inflight_rdma_per_qp);  
        /* EE Maximum number of outs. RDMA read/atomic as target -- NOT YET SUPPORTED  */
  hca_cap_p->max_ee_ous_rd_atom = 0;  
        /* Max. Num. of resources used for RDMA read/atomic as target */
  hca_cap_p->max_res_rd_atom = ((1 << (hob->ddr_alloc_size_vec.log2_rdb_size - THH_DDR_LOG2_RDB_ENTRY_SIZE)) > 255 ? 
                                    255 :(1 << (hob->ddr_alloc_size_vec.log2_rdb_size - THH_DDR_LOG2_RDB_ENTRY_SIZE))) ;     
        /* Max. Num. of outs. RDMA read/atomic as initiator. Note that 255 is the max in the struct  */
  hca_cap_p->max_qp_init_rd_atom = ((1 << (hob->dev_lims.log_max_ra_req_qp)) > 255 ? 
                                    255 :  (1 << (hob->dev_lims.log_max_ra_req_qp))); 
        /* EE Max. Num. of outs. RDMA read/atomic as initiator  -- NOT YET SUPPORTED   */
  hca_cap_p->max_ee_init_rd_atom = 0;
        /* Level of Atomicity supported:  if supported, is only within this HCA*/
  hca_cap_p->atomic_cap = (hob->dev_lims.atm ? VAPI_ATOMIC_CAP_HCA : VAPI_ATOMIC_CAP_NONE);        
        /* Maximum number of EEC supported.   -- NOT YET SUPPORTED  */
#if 0
  hca_cap_p->max_ee_num = 1 << (hob->hca_props.qpc_eec_cqc_rdb_parameters.log_num_of_ee);
#else
  hca_cap_p->max_ee_num = 0;
#endif
        /* Maximum number of IB_RDD supported  -- NOT YET SUPPORTED */
  hca_cap_p-> max_rdd_num = 0;                 
        /* Maximum Number of memory windows supported  */
#if 0
  hca_cap_p->max_mw_num = hob->profile.num_mem_windows -                    
      ((have_usr_profile == FALSE)? 0 : (1U<<hob->dev_lims.log2_rsvd_mrws) );
#else
  hca_cap_p->max_mw_num = (u_int32_t)hob->profile.num_mem_windows;                   
#endif
        /* Maximum number of Raw IPV6 QPs supported  -- NOT YET SUPPORTED */ 
  hca_cap_p->max_raw_ipv6_qp = 0;              
        /* Maximum number of Raw Ethertypes QPs supported  -- NOT YET SUPPORTED */
  hca_cap_p->max_raw_ethy_qp = 0;              
        /* Maximum Number of multicast groups  */
  hca_cap_p->max_mcast_grp_num = 1 << (hob->hca_props.multicast_parameters.log_mc_table_sz);            
        /* Maximum number of QP per multicast group    */
  hca_cap_p->max_mcast_qp_attach_num = ( (1U<<(hob->hca_props.multicast_parameters.log_mc_table_entry_sz)) 
                                               -  THH_DDR_MCG_ENTRY_HEADER_SIZE) / 
                                                             THH_DDR_MCG_BYTES_PER_QP;
        /* Maximum number of QPs which can be attached to a mcast grp */
  hca_cap_p->max_total_mcast_qp_attach_num = hca_cap_p->max_mcast_grp_num * hca_cap_p->max_mcast_qp_attach_num;
        /* Maximum number of address handles */
  hca_cap_p->max_ah_num = (u_int32_t)(hob->profile.use_priv_udav ? hob->profile.max_priv_udavs : 
                                THHUL_PDM_MAX_UL_UDAV_PER_PD*(hob->profile.max_num_pds+THH_NUM_RSVD_PD));
        /* max number of fmrs for the use is the number of user entries in MPT */
#if 0
  hca_cap_p->max_num_fmr    = hob->profile.num_external_mem_regions - 
      ((have_usr_profile == FALSE)? 0 : (1U<<hob->dev_lims.log2_rsvd_mtts) );
#else
  hca_cap_p->max_num_fmr    = (u_int32_t)((hob->ddr_props.dh == FALSE) ? hob->profile.num_external_mem_regions : 0);
#endif
/* max maps per fmr is the max number that can be expressed by the MS bits of a u_int32_t
           that are unused for MPT addressing (which will occupy the LS bits of that u_int32_t).*/
  hca_cap_p->max_num_map_per_fmr = (1 << (32 - hob->profile.log2_max_mpt_entries)) - 1;
  
        /* Log2 4.096usec Max. RX to ACK or NAK delay */
  hca_cap_p->local_ca_ack_delay = hob->dev_lims.local_ca_ack_delay;

  /* Node GUID for this hca */
  ret = THH_hob_get_node_guid(hob,&(hca_cap_p->node_guid));           

  return(ret);
}

/******************************************************************************
 *  Function:     THH_hob_modify
 *
 *  Description:  Implements the VAPI_modify_hca verb
 *
 *  input:
 *                hca_hndl
 *                port_num - 1 or 2
 *                hca_attr_p - contains values to modify
 *                hca_attr_mask_p - mask specifying which values in hca_attr_p should
 *                                  be used for modification.
 *  output: 
 *                none
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_EINVAL_PORT
 *                HH_ENOSYS
 *
 *  Comments:     Implements IB Spec 1.0a now.  must be modified to support IB Spec 1.1
 *                JPM
 *
 *****************************************************************************/
HH_ret_t THH_hob_modify(
                        HH_hca_hndl_t        hca_hndl,
                        IB_port_t            port_num,
                        VAPI_hca_attr_t      *hca_attr_p,
                        VAPI_hca_attr_mask_t *hca_attr_mask_p)
{
  /* TBD, will use SET_IB command.  Problem is that can only set PKey and QKey counters to zero. */
  HH_ret_t  retn;
  THH_cmd_status_t cmd_ret;
  VAPI_hca_port_t  port_props;
  THH_set_ib_props_t    set_ib_props;
  IB_port_cap_mask_t    capabilities;           
  THH_hob_t             hob;
   
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_modify: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_modify : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }

  hob = THHOBP(hca_hndl);

  if (hob == NULL) {
      MTL_ERROR1("THH_hob_modify : ERROR : No device registered\n");
      return HH_EINVAL;
  }
  TEST_RETURN_FATAL(hob);

  if (port_num > hob->dev_lims.num_ports || port_num < 1) {
      MTL_ERROR2( "THH_hob_modify: ERROR : invalid port number(%d)\n", port_num);
      return HH_EINVAL_PORT;
  }

  memset(&set_ib_props, 0, sizeof(THH_set_ib_props_t));
  
  set_ib_props.rqk = hca_attr_p->reset_qkey_counter;

  /* start with current capabilities */
  retn = THH_hob_query_port_prop(hca_hndl, port_num, &port_props);
  if (retn != HH_OK) {
      MTL_ERROR1("THH_hob_modify : ERROR : cannot get current capabilities (%d)\n", retn);
      return HH_EAGAIN;
  }
  capabilities = port_props.capability_mask;
  
  /* now, modify the capability mask according to the input */
  if (HCA_ATTR_IS_FLAGS_SET(*hca_attr_mask_p)) {
      /* calculate capabilities modification mask */
      if(HCA_ATTR_IS_SET(*hca_attr_mask_p, HCA_ATTR_IS_SM) ) {
          if (hca_attr_p->is_sm) {
              IB_CAP_MASK_SET(capabilities, IB_CAP_MASK_IS_SM);
          } else {
              IB_CAP_MASK_CLR(capabilities, IB_CAP_MASK_IS_SM);
          }
      }
      if(HCA_ATTR_IS_SET(*hca_attr_mask_p, HCA_ATTR_IS_SNMP_TUN_SUP) ) {
          if (hca_attr_p->is_snmp_tun_sup) {
              IB_CAP_MASK_SET(capabilities, IB_CAP_MASK_IS_SNMP_TUNN_SUP);
          } else {
              IB_CAP_MASK_CLR(capabilities, IB_CAP_MASK_IS_SNMP_TUNN_SUP);
          }
      }
      if(HCA_ATTR_IS_SET(*hca_attr_mask_p, HCA_ATTR_IS_DEV_MGT_SUP) ) {
          if (hca_attr_p->is_dev_mgt_sup) {
              IB_CAP_MASK_SET(capabilities, IB_CAP_MASK_IS_DEVICE_MGMT_SUP);
          } else {
              IB_CAP_MASK_CLR(capabilities, IB_CAP_MASK_IS_DEVICE_MGMT_SUP);
          }
      }
      if(HCA_ATTR_IS_SET(*hca_attr_mask_p, HCA_ATTR_IS_VENDOR_CLS_SUP) ) {
          if (hca_attr_p->is_vendor_cls_sup) {
              IB_CAP_MASK_SET(capabilities, IB_CAP_MASK_IS_VENDOR_CLS_SUP);
          } else {
              IB_CAP_MASK_CLR(capabilities, IB_CAP_MASK_IS_VENDOR_CLS_SUP);
          }
      }
  }

  set_ib_props.capability_mask = capabilities;
  
  /* now, perform the CMD */
  cmd_ret = THH_cmd_SET_IB(hob->cmd , port_num, &set_ib_props);
  if (cmd_ret != THH_CMD_STAT_OK) {
      TEST_CMD_FATAL(cmd_ret);
      MTL_ERROR1("THH_hob_modify: CMD_error in THH_cmd_SET_IB (%d)\n", cmd_ret);
      return HH_EINVAL;
  }

  return(HH_OK);
}
/******************************************************************************
 *  Function:     THH_hob_query_port_prop
 *
 *  Description:  Implements the VAPI_query_hca_port_prop verb
 *
 *  input:
 *                hca_hndl
 *                port_num - 1 or 2
 *  output: 
 *                hca_port_p - port properties output structure
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_EINVAL_PORT
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time.  Data is not pre-fetched, because
 *                the current port state is needed for the answer -- so the query must be performed
 *                anyway.
 *
 *****************************************************************************/
HH_ret_t THH_hob_query_port_prop(HH_hca_hndl_t  hca_hndl,
                                    IB_port_t           port_num,
                                    VAPI_hca_port_t     *hca_port_p ) 
{
    SM_MAD_PortInfo_t  port_info;
    u_int8_t       *mad_frame_in;
    u_int8_t       *mad_frame_out;
    THH_cmd_status_t  cmd_ret;

    THH_hob_t  hob;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_hob_query_port_prop: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_query_port_prop : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    hob = THHOBP(hca_hndl);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_query_port_prop : ERROR : No device registered\n");
        return HH_EINVAL;
    }
    TEST_RETURN_FATAL(hob);

    if (port_num > hob->dev_lims.num_ports || port_num < 1) {
        MTL_ERROR2( "THH_hob_query_port_prop: ERROR : invalid port number(%d)\n", port_num);
        return HH_EINVAL_PORT;
    }

    mad_frame_in = TNMALLOC(u_int8_t, IB_MAD_SIZE);
    if ( !mad_frame_in ) {
      return HH_EAGAIN;
    }
    mad_frame_out = TNMALLOC(u_int8_t, IB_MAD_SIZE);
    if ( !mad_frame_out ) {
      FREE(mad_frame_in);
      return HH_EAGAIN;
    }
    memset(mad_frame_in, 0, sizeof(mad_frame_in));
    memset(mad_frame_out, 0, sizeof(mad_frame_out));

    /* get port props using MAD commands in THH_cmd object */
    /* First, build the MAD header */
    MADHeaderBuild(IB_CLASS_SMP, 
                      0,
                      IB_METHOD_GET,
                      IB_SMP_ATTRIB_PORTINFO,
                      (u_int32_t)   port_num,
                      &(mad_frame_in[0]));

    /* issue the query */
    cmd_ret = THH_cmd_MAD_IFC(hob->cmd, 0, 0, port_num, &(mad_frame_in[0]), &(mad_frame_out[0]));
    if (cmd_ret != THH_CMD_STAT_OK) {
        TEST_CMD_FATAL(cmd_ret);
        MTL_ERROR2( "THH_hob_query_port_prop: ERROR : Get Port Info command failed (%d) for port %d\n", cmd_ret, port_num);
        FREE(mad_frame_out);
        FREE(mad_frame_in);
        if ( cmd_ret == THH_CMD_STAT_EINTR ) {
          return HH_EINTR;
        }
        return HH_ERR;
    }
    /* now, translate the response to a structure */
    PortInfoMADToSt(&port_info, &(mad_frame_out[0]));

    /* finally, extract the information we want */
    hca_port_p->bad_pkey_counter = port_info.wPKViolations;
    hca_port_p->capability_mask  = port_info.dwCapMask;
    hca_port_p->gid_tbl_len      = port_info.bGUIDCap;
    hca_port_p->lid              = port_info.wLID;
    hca_port_p->lmc              = port_info.cLMC;
    hca_port_p->max_msg_sz       = IB_MAX_MESSAGE_SIZE;
    hca_port_p->max_mtu          = hob->dev_lims.max_mtu;
    hca_port_p->max_vl_num       = port_info.cVLCap;
    hca_port_p->pkey_tbl_len     = hob->init_ib_props[port_num-1].max_pkey;
    hca_port_p->qkey_viol_counter = port_info.wQKViolations;
    hca_port_p->sm_lid            = port_info.wMasterSMLID;
    hca_port_p->sm_sl             = port_info.cMasterSMSL;
    hca_port_p->state             = port_info.cPortState;
    hca_port_p->subnet_timeout    = port_info.cSubnetTO;

    hca_port_p->initTypeReply     = 0;   /* not yet supported in FW */
    
  FREE(mad_frame_out);
  FREE(mad_frame_in);
  return(HH_OK);
}

/******************************************************************************
 *  Function:     THH_hob_get_pkey_tbl_local
 *
 *  Description:  Gets PKEY table for a given port  
 *
 *  input:
 *                hca_hndl
 *                port_num - 1 or 2
 *                tbl_len_in - size of table provided for response (in pkeys)
 *                use_mad_query_for_pkeys - if TRUE, query Tavor for pkeys
 *                       else, use pkey_table tracking in thh_qpm
 *  output: 
 *                tbl_len_out - size of returned table (in pkeys)
 *                pkey_tbl_p  - pointer to table containing data (space provided by caller)
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. 
 *
 *****************************************************************************/
static HH_ret_t THH_hob_get_pkey_tbl_local(HH_hca_hndl_t  hca_hndl,
                                     IB_port_t     port_num,
                                     u_int16_t     tbl_len_in,
                                     u_int16_t     *tbl_len_out,
                                     IB_pkey_t     *pkey_tbl_p,
                                     MT_bool       use_mad_query_for_pkeys)
{
  SM_MAD_Pkey_table_t  pkey_table;
  u_int8_t       *mad_frame_in;
  u_int8_t       *mad_frame_out;
  int  i,j;
  int num_pkeys, pkey_index, num_pkeytable_commands;
  THH_cmd_status_t  cmd_ret;
  THH_hob_t  thh_hob_p;
  THH_qpm_t      qpm;
  HH_ret_t     hh_ret = HH_OK;
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_get_pkey_tbl: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_get_pkey_tbl : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  /* check if have valid port number */
  if (port_num > thh_hob_p->dev_lims.num_ports || port_num < 1) {
      MTL_ERROR1("THH_hob_get_pkey_tbl: port number (%d) not valid)\n", port_num);
      return HH_EINVAL_PORT;
  }
  
  if (tbl_len_out == NULL) {
      return HH_EINVAL;
  }
  

  /* check that pkey table has enough space */
  num_pkeys =  thh_hob_p->init_ib_props[port_num-1].max_pkey;
  *tbl_len_out = num_pkeys;

  if (tbl_len_in < num_pkeys) {
      if (!tbl_len_in) {
          MTL_TRACE2( "THH_hob_get_pkey_tbl: returning number of pkeys configured (%d)\n", num_pkeys);
      } else {
          MTL_ERROR2( "THH_hob_get_pkey_tbl: ERROR : not enough space in return value table. num keys = %d\n",
                      num_pkeys);
      }
      return HH_EAGAIN;
  }

  /* check that have valid output buffer area */
  if (pkey_tbl_p == NULL) {
      return HH_EINVAL;
  }


  mad_frame_in = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_in ) {
    return HH_EAGAIN;
  }
  mad_frame_out = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_out ) {
    FREE(mad_frame_in);
    return HH_EAGAIN;
  }
  
  /* get KEY table using MAD command in THH_cmd object */
  /* get PKey table using MAD commands in THH_cmd object */
  /* First, build the MAD header */
  if (use_mad_query_for_pkeys == TRUE) {
      num_pkeytable_commands = ((num_pkeys - 1) / 32) + 1;
    
      pkey_index =  0;
      for (i = 0; i < num_pkeytable_commands; i++) {
          memset(mad_frame_in, 0, sizeof(mad_frame_in));
          memset(mad_frame_out, 0, sizeof(mad_frame_out));
          MADHeaderBuild(IB_CLASS_SMP, 
                            0,
                            IB_METHOD_GET,
                            IB_SMP_ATTRIB_PARTTABLE,
                            (u_int32_t)   (32*i),
                            &(mad_frame_in[0]));
    
          cmd_ret = THH_cmd_MAD_IFC(thh_hob_p->cmd, 0, 0, port_num, &(mad_frame_in[0]), &(mad_frame_out[0]));
          if (cmd_ret != THH_CMD_STAT_OK) {
              TEST_CMD_FATAL(cmd_ret);
              MTL_ERROR2( "THH_hob_get_pkey_tbl: ERROR : Get Partition Table command failed (%d) for port %d\n", cmd_ret, port_num);
              FREE(mad_frame_out);
              FREE(mad_frame_in);
              return HH_ERR;
          }
          PKeyTableMADToSt(&pkey_table, &(mad_frame_out[0]));
    
          for (j = 0; j < 32; j++) {
              pkey_tbl_p[pkey_index++] = pkey_table.pkey[j];
              if (pkey_index == num_pkeys) {
                  break;
              }
          }
      }
  } else {
      hh_ret =  THH_hob_get_qpm ( thh_hob_p, &qpm );
      if (hh_ret != HH_OK) {
          MTL_ERROR2( "THH_hob_get_qpm: invalid QPM handle (ret= %d)\n", hh_ret);
          FREE(mad_frame_out);
          FREE(mad_frame_in);
          return HH_EINVAL;
      }
      hh_ret = THH_qpm_get_all_pkeys(qpm,port_num,num_pkeys, pkey_tbl_p);
      if (hh_ret != HH_OK) {
          MTL_ERROR2( "THH_qpm_get_all_sgids failed (ret= %d)\n", hh_ret);
          FREE(mad_frame_out);
          FREE(mad_frame_in);
          return HH_EINVAL;
      }
  }

  FREE(mad_frame_out);
  FREE(mad_frame_in);
  return(HH_OK);
}
/******************************************************************************
 *  Function:     THH_hob_get_pkey_tbl
 *
 *  Description:  Gets PKEY table for a given port  
 *
 *  input:
 *                hca_hndl
 *                port_num - 1 or 2
 *                tbl_len_in - size of table provided for response (in pkeys)
 *  output: 
 *                tbl_len_out - size of returned table (in pkeys)
 *                pkey_tbl_p  - pointer to table containing data (space provided by caller)
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. 
 *
 *****************************************************************************/
HH_ret_t THH_hob_get_pkey_tbl(HH_hca_hndl_t  hca_hndl,
                                     IB_port_t     port_num,
                                     u_int16_t     tbl_len_in,
                                     u_int16_t     *tbl_len_out,
                                     IB_pkey_t     *pkey_tbl_p)
{
    MT_bool is_legacy = FALSE;
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    FUNC_IN;

    MTL_DEBUG4("THH_hob_get_pkey_tbl:  hca_hndl=0x%p, port= %d, return table len = %d\n",
                 hca_hndl, port_num, tbl_len_in);


    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_get_pkey_tbl : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);
    if (thh_hob_p == NULL) {
        MTL_ERROR1("THH_hob_get_pkey_tbl : ERROR : No device registered\n");
        return HH_EINVAL;
    }
    TEST_RETURN_FATAL(thh_hob_p);

    THH_hob_get_legacy_mode(thh_hob_p, &is_legacy);
    return(THH_hob_get_pkey_tbl_local(hca_hndl,port_num,tbl_len_in,
                                     tbl_len_out,pkey_tbl_p,is_legacy));

}

HH_ret_t  THH_hob_init_pkey_tbl( HH_hca_hndl_t  hca_hndl,
                                      IB_port_t      port_num,
                                      u_int16_t      tbl_len_in,
                                      u_int16_t*     tbl_len_out,
                                      IB_pkey_t     *pkey_tbl_p)
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    return(THH_hob_get_pkey_tbl_local(hca_hndl,port_num,tbl_len_in,
                                     tbl_len_out,pkey_tbl_p,1));

}


/******************************************************************************
 *  Function:     THH_hob_get_gid_tbl_local
 *
 *  Description:  Gets GID table for a given port  
 *
 *  input:
 *                hca_hndl
 *                port_num - 1 or 2
 *                tbl_len_in - size of table provided for response (in pkeys)
 *                use_mad_query_for_gid_prefix - if TRUE, query Tavor for gid prefix.
 *                       else, use gid_table tracking in thh_qpm
 *  output: 
 *                tbl_len_out - size of returned table (in pkeys)
 *                param_gid_p  - pointer to table containing data (space provided by caller)
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. 
 *
 *****************************************************************************/
#ifndef IVAPI_THH
static 
#endif
HH_ret_t  THH_hob_get_gid_tbl_local( HH_hca_hndl_t  hca_hndl,
                                      IB_port_t      port,
                                      u_int16_t      tbl_len_in,
                                      u_int16_t*     tbl_len_out,
                                      IB_gid_t*      param_gid_p,
                                      MT_bool        use_mad_query_for_gid_prefix)
{
  SM_MAD_PortInfo_t  port_info;
  SM_MAD_GUIDInfo_t  guid_info;
  u_int8_t       *mad_frame_in;
  u_int8_t       *mad_frame_out;
  THH_cmd_status_t  cmd_ret;
  int            num_guids, guid_index;
  int            num_guidinfo_commands;
  u_int8_t       *gid_p = (u_int8_t *) param_gid_p;
  int            i,j;
  HH_ret_t       hh_ret = HH_OK;
  THH_qpm_t      qpm;
  IB_gid_t       gid;
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  FUNC_IN;

  MTL_DEBUG4("THH_hob_get_gid_tbl_local:  hca_hndl=0x%p, port= %d, return table len = %d\n",
               hca_hndl, port, tbl_len_in);

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_get_gid_tbl: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_get_gid_tbl : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);
  

  /* check if have valid port number */
  if (port > thh_hob_p->dev_lims.num_ports || port < 1) {
      MTL_ERROR1("THH_hob_get_gid_tbl: port number (%d) not valid)\n", port);
      return HH_EINVAL_PORT;
  }

  if (tbl_len_out == NULL) {
      return HH_EINVAL;
  }
  

  /* check that gid table has enough space */
  num_guids =  thh_hob_p->init_ib_props[port-1].max_gid;
  *tbl_len_out = num_guids;

  if (tbl_len_in < num_guids) {
      if (!tbl_len_in) {
          MTL_TRACE2( "THH_hob_get_gid_tbl: returning gid table configured size (%d)\n", num_guids);
      } else {
          MTL_ERROR2( "THH_hob_get_gid_tbl: ERROR : not enough space in return value table.  Need %d\n",
                      num_guids);
      }
      return HH_EAGAIN;
  }

  /* check that have valid output buffer area */
  if (param_gid_p == NULL) {
      return HH_EINVAL;
  }

  mad_frame_in = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_in ) {
    return HH_EAGAIN;
  }
  mad_frame_out = TNMALLOC(u_int8_t, IB_MAD_SIZE);
  if ( !mad_frame_out ) {
    FREE(mad_frame_in);
    return HH_EAGAIN;
  }


  /* get GID table using MAD commands in THH_cmd object */
  if (use_mad_query_for_gid_prefix == TRUE) {
  /* First, get the GID prefix from via MAD query  */
      memset(mad_frame_in, 0, sizeof(mad_frame_in));
      memset(mad_frame_out, 0, sizeof(mad_frame_out));
      MADHeaderBuild(IB_CLASS_SMP, 
                        0,
                        IB_METHOD_GET,
                        IB_SMP_ATTRIB_PORTINFO,
                        (u_int32_t)   port,
                        &(mad_frame_in[0]));
    
      /* issue the query */
      cmd_ret = THH_cmd_MAD_IFC(thh_hob_p->cmd, 0, 0, port, &(mad_frame_in[0]), &(mad_frame_out[0]));
      if (cmd_ret != THH_CMD_STAT_OK) {
          TEST_CMD_FATAL(cmd_ret);
          MTL_ERROR2( "THH_hob_get_gid_tbl: ERROR : Get Port Info command failed (%d) for port %d\n", cmd_ret, port);
          FREE(mad_frame_out);
          FREE(mad_frame_in);
          return HH_ERR;
      }
      PortInfoMADToSt(&port_info, &(mad_frame_out[0]));
      PortInfoPrint(&port_info);
      memcpy(&gid, &(port_info.qwGIDPrefix), sizeof(port_info.qwGIDPrefix));

      /* Now, get the GUIDs, and build GIDS */
      num_guidinfo_commands = ((num_guids - 1) / 8) + 1;

      guid_index =  0;
      for (i = 0; i < num_guidinfo_commands; i++) {
          memset(mad_frame_in, 0, sizeof(mad_frame_in));
          memset(mad_frame_out, 0, sizeof(mad_frame_out));
          MADHeaderBuild(IB_CLASS_SMP, 
                            0,
                            IB_METHOD_GET,
                            IB_SMP_ATTRIB_GUIDINFO,
                            (u_int32_t)   (i*8),
                            &(mad_frame_in[0]));

          cmd_ret = THH_cmd_MAD_IFC(thh_hob_p->cmd, 0, 0, port, &(mad_frame_in[0]), &(mad_frame_out[0]));
          if (cmd_ret != THH_CMD_STAT_OK) {
              TEST_CMD_FATAL(cmd_ret);
              MTL_ERROR2( "THH_hob_get_gid_tbl: ERROR : Get GUID Info command failed (%d) for port %d\n", cmd_ret, port);
              FREE(mad_frame_out);
              FREE(mad_frame_in);
              return HH_ERR;
          }
          GUIDInfoMADToSt(&guid_info, &(mad_frame_out[0]));
          GUIDInfoPrint(&guid_info);

          for (j = 0; j < 8; j++) {
              memcpy (gid_p, &(gid), sizeof(port_info.qwGIDPrefix));
              gid_p += sizeof(port_info.qwGIDPrefix);
              memcpy (gid_p, &(guid_info.guid[j]), sizeof(IB_guid_t));
              gid_p += sizeof(u_int64_t);
              guid_index++;
              if (guid_index == num_guids) {
                  break;
              }
          }
      }
  } else {
      memset(&port_info, 0, sizeof(port_info));
      hh_ret =  THH_hob_get_qpm ( thh_hob_p, &qpm );
      if (hh_ret != HH_OK) {
          MTL_ERROR2( "THH_hob_get_qpm: invalid QPM handle (ret= %d)\n", hh_ret);
          FREE(mad_frame_out);
          FREE(mad_frame_in);
          return HH_EINVAL;
      }
      hh_ret = THH_qpm_get_all_sgids(qpm,port,num_guids, param_gid_p);
      if (hh_ret != HH_OK) {
          MTL_ERROR2( "THH_qpm_get_all_sgids failed (ret= %d)\n", hh_ret);
          FREE(mad_frame_out);
          FREE(mad_frame_in);
          return HH_EINVAL;
      }
      FREE(mad_frame_out);
      FREE(mad_frame_in);
      return HH_OK;
  }

  FREE(mad_frame_out);
  FREE(mad_frame_in);
  return HH_OK;
} /* THH_get_gid_tbl */
/******************************************************************************
 *  Function:     THH_hob_get_gid_tbl
 *
 *  Description:  Gets GID table for a given port  
 *
 *  input:
 *                hca_hndl
 *                port_num - 1 or 2
 *                tbl_len_in - size of table provided for response (in pkeys)
 *  output: 
 *                tbl_len_out - size of returned table (in pkeys)
 *                param_gid_p  - pointer to table containing data (space provided by caller)
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. 
 *
 *****************************************************************************/
HH_ret_t  THH_hob_get_gid_tbl( HH_hca_hndl_t  hca_hndl,
                                      IB_port_t      port,
                                      u_int16_t      tbl_len_in,
                                      u_int16_t*     tbl_len_out,
                                      IB_gid_t*      param_gid_p)
{
    MT_bool is_legacy = FALSE;
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    FUNC_IN;

    MTL_DEBUG4("THH_hob_get_gid_tbl:  hca_hndl=0x%p, port= %d, return table len = %d\n",
                 hca_hndl, port, tbl_len_in);


    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_get_gid_tbl : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);
    if (thh_hob_p == NULL) {
        MTL_ERROR1("THH_hob_get_gid_tbl : ERROR : No device registered\n");
        return HH_EINVAL;
    }
    TEST_RETURN_FATAL(thh_hob_p);

    
    THH_hob_get_legacy_mode(thh_hob_p, &is_legacy);
    return(THH_hob_get_gid_tbl_local(hca_hndl,port,tbl_len_in,
                                     tbl_len_out,param_gid_p,is_legacy));

}

HH_ret_t  THH_hob_init_gid_tbl( HH_hca_hndl_t  hca_hndl,
                                      IB_port_t      port,
                                      u_int16_t      tbl_len_in,
                                      u_int16_t*     tbl_len_out,
                                      IB_gid_t*      param_gid_p)
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    return(THH_hob_get_gid_tbl_local(hca_hndl,port,tbl_len_in,
                                     tbl_len_out,param_gid_p,1));

}

/******************************************************************************
 *  Function:     THH_hob_set_comp_eventh
 *
 *  Description:  Sets completion event handler for VIP layer (below vapi).  Used internally
 *                by VAPI  
 *
 *  input:
 *                hca_hndl
 *                event -  pointer to handler function
 *                private_data - pointer to context data provided to handler
 *  returns:
 *                HH_OK
 *                HH_EAGAIN
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Initial (dummy) handler is provided during open_hca.  Therefore,
 *                the function THH_eventp_replace_handler is used to register the handler.
 *
 *****************************************************************************/
HH_ret_t THH_hob_set_comp_eventh(HH_hca_hndl_t      hca_hndl,
                                        HH_comp_eventh_t   event,
                                        void*              private_data)
{
  HH_ret_t  ret;
  THH_eventp_handler_t ev_hndlr;
  
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_set_comp_eventh: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_set_comp_eventh : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);
  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_set_comp_eventh: ERROR : No device registered\n");
      return HH_EAGAIN;
  }

  if (event == NULL) {
      event = THH_dummy_comp_event;
  } else {
      TEST_RETURN_FATAL(thh_hob_p);
  }
  ev_hndlr.comp_event_h = event;


  if (thh_hob_p->eventp == (THH_eventp_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_set_comp_eventh: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  /* neutralizing for VAPI start */
  //  return HH_OK;

  ret = THH_eventp_replace_handler(thh_hob_p->eventp,thh_hob_p->compl_eq, ev_hndlr, private_data);
  if (ret != HH_OK) {
      MTL_ERROR1( "THH_hob_set_comp_eventh: ERROR : cannot register completion event handler (%d)\n", ret);
      return HH_ERR;
  }
  
  return HH_OK;
}

/******************************************************************************
 *  Function:     THH_hob_set_async_eventh
 *
 *  Description:  Sets async handler for VIP layer (below vapi).  Used internally
 *                by VAPI  
 *
 *  input:
 *                hca_hndl
 *                event -  pointer to handler function
 *                private_data - pointer to context data provided to handler
 *  returns:
 *                HH_OK
 *                HH_EAGAIN
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Initial (dummy) handler is provided during open_hca.  Therefore,
 *                the function THH_eventp_replace_handler is used to register the handler.
 *
 *****************************************************************************/
HH_ret_t THH_hob_set_async_eventh( HH_hca_hndl_t      hca_hndl,
                                          HH_async_eventh_t  event,
                                          void*              private_data)
{
  HH_ret_t  ret;
  THH_eventp_handler_t ev_hndlr;

  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_set_async_eventh: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_set_async_eventh : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (event == NULL) {event = &THH_dummy_async_event;
  } else {
      TEST_RETURN_FATAL(thh_hob_p);
  }
  
  ev_hndlr.ib_comp_event_h = event;

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_set_async_eventh: ERROR : No device registered\n");
      return HH_EAGAIN;
  }

  if (thh_hob_p->eventp == (THH_eventp_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_set_async_eventh: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }
// neutralizing for VAPI start
// return HH_OK;
  
  ret = THH_eventp_replace_handler(thh_hob_p->eventp,thh_hob_p->ib_eq, ev_hndlr, private_data);
  if (ret != HH_OK) {
      MTL_ERROR1( "THH_hob_set_async_eventh: ERROR : cannot register async event handler (%d)\n", ret);
      return HH_ERR;
  }

  /* track async event handler setting for use in fatal error handling */
  MOSAL_spinlock_irq_lock(&thh_hob_p->async_spl);
  thh_hob_p->async_eventh = event;
  thh_hob_p->async_ev_private_context = private_data;
  MOSAL_spinlock_unlock(&thh_hob_p->async_spl);

  return HH_OK;
}


#ifndef __DARWIN__
int THH_hob_fatal_err_thread(void  *arg)
 {
   THH_hob_t                hob_p;
   THH_hob_cat_err_thread_t  *fatal_thread_obj_p = (THH_hob_cat_err_thread_t  *)arg;
   HH_event_record_t fatal_ev_rec;
   THH_cmd_t    cmd_if;
   THH_eventp_t eventp;
   call_result_t     mosal_ret;
   
   MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
   MTL_TRACE2("%s: Initializing\n", __func__);

   hob_p  = (THH_hob_t)(fatal_thread_obj_p->hob);
   MOSAL_thread_set_name(&fatal_thread_obj_p->mto, "cleanup_thread");


   /* signal that thread is up */
   fatal_thread_obj_p->have_fatal = FALSE;
   MOSAL_syncobj_signal(&fatal_thread_obj_p->start_sync);

   MTL_TRACE3("%s: about to wait on fatal error signal\n", __func__);
   mosal_ret=MOSAL_syncobj_waiton(&hob_p->fatal_thread_obj.fatal_err_sync,
                                  MOSAL_SYNC_TIMEOUT_INFINITE);
   if (mosal_ret == MT_EINTR || hob_p->fatal_thread_obj.have_fatal == FALSE) {
       MTL_DEBUG1(MT_FLFMT("%s: GOT termination request"), __func__);
       /* if no fatal error, just return */
       MOSAL_syncobj_signal(&fatal_thread_obj_p->stop_sync);
       return 1;
   }
   MTL_ERROR1(MT_FLFMT("%s: RECEIVED FATAL ERROR WAKEUP"), __func__);

   /* fatal error processing */
   if (THH_hob_get_cmd_if(hob_p, &cmd_if) == HH_OK) {
       THH_cmd_handle_fatal(cmd_if);
   }
   if (THH_hob_get_eventp(hob_p,&eventp) == HH_OK) {
       THH_eventp_handle_fatal(eventp);
   }

   /* Halt HCA here */
   if (hob_p->module_flags.fatal_delay_halt == 0) {
       MTL_DEBUG1(MT_FLFMT("%s: halting the HCA"), __func__);
       THH_hob_halt_hca(hob_p);
   }

   MOSAL_spinlock_irq_lock(&hob_p->fatal_spl);
   /* turn off STARTED bit, and turn on HALTED bit */
   hob_p->thh_state &= ~(THH_STATE_FATAL_START);
   hob_p->thh_state |= THH_STATE_FATAL_HCA_HALTED;
   MOSAL_syncobj_signal(&hob_p->thh_fatal_complete_syncobj);
   MOSAL_spinlock_unlock(&hob_p->fatal_spl);

   /* INVOKE THE async event callback with fatal error */
   if (hob_p->hh_hca_hndl != NULL && hob_p->async_eventh != NULL) {
       MTL_TRACE1(MT_FLFMT("%s: INVOKE ASYNC CALLBACK"), __func__);  
       memset(&fatal_ev_rec, 0, sizeof(HH_event_record_t));
       fatal_ev_rec.etype = VAPI_LOCAL_CATASTROPHIC_ERROR;
       fatal_ev_rec.syndrome = hob_p->fatal_syndrome;
       (*(hob_p->async_eventh))(hob_p->hh_hca_hndl, &fatal_ev_rec, hob_p->async_ev_private_context);
   }
   MOSAL_syncobj_signal(&fatal_thread_obj_p->stop_sync);
   return 0;
}
#endif /* not defined __DARWIN__ */


/*
 *  mosal_find_capbility_ptr
 */
static call_result_t mosal_find_capbility_ptr(MOSAL_pci_dev_t   pci_dev,
                                              u_int8_t cap_id,
                                              u_int8_t *cap_ptr_p)
{
  call_result_t rc;
  u_int8_t cap_ptr;
  u_int32_t cap_val_dw;
  int c=0;

  /* read cap pointer */
  rc = MOSAL_PCI_read_config(pci_dev, 0x34, 1, &cap_ptr);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: failed reading cap pointer - %s"), __func__, mtl_strerror(rc));
    return rc; 
  }


  while ( c < 64  ) {
    rc = MOSAL_PCI_read_config(pci_dev, cap_ptr, 4, &cap_val_dw);
    if ( rc != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: failed reading dword at address 0x%x - %s"), __func__, cap_ptr, mtl_strerror(rc));
      return rc; 
    }

    if ( (cap_val_dw&0xff) == cap_id ) {
      *cap_ptr_p = cap_ptr;
      return MT_OK;
    }
    cap_ptr = (u_int8_t)(cap_val_dw>>8) & 0xfc; /* mask 2 lsbs */
    if ( cap_ptr == 0 ) break;
    c++;
  }
  return MT_ENORSC;
}


/*
 *  THH_set_max_read_request_size
 *
 *  set Max Read Request Size for Arbel in Tavor mode 5 => 4096 bytes
 */
static call_result_t THH_set_max_read_request_size(THH_hw_props_t *hw_props_p)
{
  call_result_t rc;
  u_int8_t cap_ptr;
  u_int16_t cap_val;
  u_int8_t mrrs_val = 5; /* => 4096 bytes */
  const u_int8_t rbc_cap_id = 16; /* Max Read Request Size capability ID */

  rc = mosal_find_capbility_ptr(hw_props_p->pci_dev, rbc_cap_id, &cap_ptr);
  if ( rc != MT_OK ) {
    MTL_DEBUG1(MT_FLFMT("%s: failed to find MRRS capability - %s"), __func__, mtl_strerror_sym(rc));
    return rc;
  }
  rc = MOSAL_PCI_read_config(hw_props_p->pci_dev, cap_ptr+8, 2, &cap_val);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to read rbc - %s"), __func__, mtl_strerror_sym(rc));
    return rc;
  }
  cap_val &= 0x8fff;
  cap_val |= (mrrs_val<<12);
  rc = MOSAL_PCI_write_config(hw_props_p->pci_dev, cap_ptr+8, 2, cap_val);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to write rbc - %s"), __func__, mtl_strerror_sym(rc));
  }
  return rc;
}

/*
 *  THH_set_rbc
 *
 *  set the default Read Byte Count for Tavor - 3 ==> 4096 bytes
 */
static call_result_t THH_set_rbc(THH_hw_props_t *hw_props_p)
{
  call_result_t rc;
  u_int8_t cap_ptr, cap_val;
  u_int8_t rbc_val = 3;
  const u_int8_t rbc_cap_id = 7; /* Read Byte Count capability ID */


  if ( !hw_props_p ) {
    MTL_ERROR1(MT_FLFMT("hw_props_p=null"));
    return MT_EINVAL;
  }



  rc = mosal_find_capbility_ptr(hw_props_p->pci_dev, rbc_cap_id, &cap_ptr);
  if ( rc != MT_OK ) {
    MTL_DEBUG1(MT_FLFMT("%s: failed to find RBC capability - %s"), __func__, mtl_strerror_sym(rc));
    return rc;
  }


  rc = MOSAL_PCI_read_config(hw_props_p->pci_dev, cap_ptr+2, 1, &cap_val);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to read rbc - %s"), __func__, mtl_strerror_sym(rc));
    return rc;
  }
  cap_val &= 0xf3;
  cap_val |= (rbc_val<<2);
  rc = MOSAL_PCI_write_config(hw_props_p->pci_dev, cap_ptr+2, 1, cap_val);
  if ( rc != MT_OK ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to write rbc - %s"), __func__, mtl_strerror_sym(rc));
  }
  return rc;
}



/*
 *  THH_set_capabilities
 */
static void THH_set_capabilities(THH_hw_props_t *hw_props_p)
{
  /* set the default Read Byte Count for Tavor */
  THH_set_rbc(hw_props_p);

  /* set max read request size in capabilty structure of PCI express */
  THH_set_max_read_request_size(hw_props_p);
}

/******************************************************************************
 *  Function:     THH_hob_create
 *
 *  Description:  Creates the HOB object for an HCA, and registers it in HH  
 *
 *  input:
 *                hw_props_p -- PCI properties (BARs, etc)
 *                hca_seq_num  - a sequence number assigned to this HCA to differentiate it 
 *                               from other HCAs on this host
 *                mod_flags   - flags passed in at module initialization (e.g., insmod)
 *  output: 
 *                hh_hndl_p  - size of returned table (in pkeys)
 *  returns:
 *                HH_OK
 *                HH_EAGAIN
 *                HH_ERR -- other errors
 *
 *  Comments:     This function involves the following steps: 
 *                    1.Allocate THH_hob data context. 
 *                    2.Create the THH_cmd_if object instance (in order to enable 
 *                       queries of HCA resources even before HCA is opened). 
 *                    3.Invoke  ENABLE_SYS command ((polling mode). 
 *                    4.Query HCA for available DDRmemory resources 
 *                      (use the Command interface in polling mode)and create 
 *                       the THH_ddrmm object based on results. 
 *                    5.Query HCA for other capabilties and save them in THH_hob context. 
 *                    6.Register HCA in HH (i.e.call HH_add_hca_dev()).
 *
 *                Also initializes the HOB mutex for controlling Open HCA and Close HCA 
 *
 *****************************************************************************/
HH_ret_t    THH_hob_create(/*IN*/  THH_hw_props_t   *hw_props_p,
                           /*IN*/  u_int32_t         hca_seq_num,
                           /*IN*/  THH_module_flags_t *mod_flags,
                           /*OUT*/ HH_hca_hndl_t    *hh_hndl_p )
{

  //HH_hca_hndl_t  hca_hndl = 0;
  HH_ret_t        ret;
  HH_if_ops_t     *if_ops_p = 0;
  THH_hob_t       hob_p;
  MT_size_t       ddr_size;
  MT_size_t       fw_size;
  HH_hca_hndl_t   new_hh_hndl;
  THH_cmd_status_t  cmd_ret;
  HH_hca_dev_t    tdev;
  u_int64_t       fw_version;
  u_int32_t       req_fw_maj_version=0;
  u_int16_t       req_fw_min_version = 0;
  u_int16_t       req_fw_submin_version = 0;
  int             int_ret = 0;
  call_result_t   mosal_ret = MT_OK;
  MT_phys_addr_t  cmdif_uar0_arg;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  MTL_DEBUG4("Entering THH_hob_create\nhca_seq_num = %d, legacy_flag = %s, av_in_host_mem = %s\n",  
             hca_seq_num, (mod_flags->legacy_sqp == FALSE ? "FALSE" : "TRUE"),
              (mod_flags->av_in_host_mem == FALSE ? "FALSE" : "TRUE"));
  THH_print_hw_props(hw_props_p);

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_create: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  THH_set_capabilities(hw_props_p);

  /* Allocate THH hob structure  */                                     
  hob_p = (THH_hob_t)MALLOC(sizeof(struct THH_hob_st));

  if (hob_p == 0) {
    MTL_ERROR1("THH_hob_create: could not allocate memory for THH hob\n");
    return HH_EAGAIN;
  }

  MTL_DEBUG1("THH_hob_create: HOB address = 0x%p\n", hob_p);
  memset(hob_p, 0, sizeof(struct THH_hob_st));

    
  /* initialize the HOB mutex */
  MOSAL_mutex_init(&(hob_p->mtx));

  /* set device name */
  sprintf(hob_p->dev_name, "InfiniHost%d",hca_seq_num); 
#if !defined(__DARWIN__)
  printk("\nMellanox Tavor Device Driver is creating device \"%s\" (bus=%02x, devfn=%02x)\n\n", 
         hob_p->dev_name,hw_props_p->bus,hw_props_p->dev_func);
#else
  MTL_DEBUG1("\nMellanox Tavor Device Driver is creating device \"%s\"\n\n", hob_p->dev_name);
#endif

  /* set embedded object handles to invalid */
  
  hob_p->cmd  = (THH_cmd_t)THH_INVALID_HNDL;
  hob_p->ddrmm = (THH_ddrmm_t)THH_INVALID_HNDL;
  hob_p->uldm = (THH_uldm_t)THH_INVALID_HNDL;
  hob_p->mrwm = (THH_mrwm_t)THH_INVALID_HNDL;
  hob_p->cqm = (THH_cqm_t)THH_INVALID_HNDL;
//hob_p->eecm = (THH_eecm_t)THH_INVALID_HNDL;  /* JPM -- EECM ADDITIONS HERE */
  hob_p->qpm = (THH_qpm_t)THH_INVALID_HNDL;
  hob_p->udavm = (THH_udavm_t)THH_INVALID_HNDL;
  hob_p->mcgm = (THH_mcgm_t)THH_INVALID_HNDL;
  hob_p->eventp = (THH_eventp_t)THH_INVALID_HNDL;
  hob_p->kar = (THH_uar_t)THH_INVALID_HNDL;
  
  /* initialize EQ handles to all EQs invalid */
  hob_p->compl_eq = THH_INVALID_EQN;
  hob_p->ib_eq    = THH_INVALID_EQN;

  /* initialize fatal error handling fields */
  memcpy(&(hob_p->hw_props), hw_props_p, sizeof(THH_hw_props_t));
  memcpy(&(hob_p->module_flags), mod_flags, sizeof(THH_module_flags_t));
  hob_p->hca_seq_num = hca_seq_num;
  hob_p->thh_state = THH_STATE_CREATING;
  if (MOSAL_spinlock_init(&(hob_p->fatal_spl)) != MT_OK){
    MTL_ERROR4(MT_FLFMT("%s: Failed to initializing fatal error spinlock"), __func__);  
    ret= HH_ERR;
    goto err_free_hob;
  }
  if (MOSAL_spinlock_init(&(hob_p->async_spl)) != MT_OK){
    MTL_ERROR4(MT_FLFMT("%s: Failed to initializing async handler tracking spinlock"), __func__);  
    ret= HH_ERR;
    goto err_free_hob;
  }
  /* dummy async event handler is passed to THH_eventp when initializing ib_eq */
  hob_p->async_eventh = &THH_dummy_async_event;
  hob_p->async_ev_private_context = NULL;

#ifndef __DARWIN__
  /* get bridge config info */
  ret = THH_hob_get_pci_br_config(hob_p);

  /* get hca config info */
  ret = read_pci_config(hob_p->hw_props.pci_dev, hob_p->pci_hca_info.config);
  hob_p->pci_hca_info.bus = hw_props_p->bus;
  hob_p->pci_hca_info.dev_func = hw_props_p->dev_func;
  hob_p->pci_hca_info.is_valid = TRUE;
#endif /* not defined __DARWIN__ */

  /* create the THH_cmd object so that can initialize and query the adapter */
  /* HCR register offset */
  if ( mod_flags->cmdif_post_uar0) {
    cmdif_uar0_arg = hw_props_p->uar_base;
  }
  else {
    cmdif_uar0_arg = (MT_phys_addr_t) MAKE_ULONGLONG(0xFFFFFFFFFFFFFFFF);
  }
  ret = THH_cmd_create(hob_p, hw_props_p->hw_ver, hw_props_p->cr_base, cmdif_uar0_arg, &(hob_p->cmd),
                       mod_flags->inifinite_cmd_timeout, mod_flags->num_cmds_outs);
  if (ret != HH_OK) {
    MTL_ERROR1("THH_hob_create: could not create CMD object (%d)\n", ret);
    ret = HH_ERR;
    goto err_free_hob;
  }


  /* invoke SYS_ENA command on tavor to initialize it -- load firmware from flash, etc.*/
  cmd_ret = THH_cmd_SYS_EN(hob_p->cmd);
  if (cmd_ret != THH_CMD_STAT_OK) {
    if (cmd_ret == THH_CMD_STAT_EFATAL) {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: FATAL ERROR in THH_cmd_SYS_EN"));
        ret = HH_EFATAL;
    } else {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: CMD_error in THH_cmd_SYS_EN (%d)"), cmd_ret);
        ret = HH_ERR;
    }
    goto cmd_err;
  }


  /* do query firmware command */
  cmd_ret = THH_cmd_QUERY_FW(hob_p->cmd, &(hob_p->fw_props));
  if (cmd_ret != THH_CMD_STAT_OK) {
    if (cmd_ret == THH_CMD_STAT_EFATAL) {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: FATAL ERROR in THH_cmd_QUERY_FW"));
        ret = HH_EFATAL;
    } else {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: CMD_error in THH_cmd_QUERY_FW (%d)"), cmd_ret);
        ret = HH_ERR;
    }
    goto undo_sys_ena;
  }
  fw_version = hob_p->fw_props.fw_rev_major;
  fw_version = (fw_version <<16) | hob_p->fw_props.fw_rev_minor;
  fw_version = (fw_version <<16) | hob_p->fw_props.fw_rev_subminor;
  /* enter data into version info structure */
  hob_p->version_info.fw_ver_major = hob_p->fw_props.fw_rev_major;
  hob_p->version_info.fw_ver_minor = hob_p->fw_props.fw_rev_minor;
  hob_p->version_info.fw_ver_subminor = hob_p->fw_props.fw_rev_subminor;
  hob_p->version_info.hw_ver       = hob_p->hw_props.hw_ver;
  hob_p->version_info.cmd_if_ver   = hob_p->fw_props.cmd_interface_rev;


  if (fw_version < THH_MIN_FW_VERSION) {
      req_fw_maj_version = (u_int32_t)  ((((u_int64_t)THH_MIN_FW_VERSION)>>32) & MAKE_ULONGLONG(0xFFFFFFFF));
      req_fw_min_version = (u_int16_t)  ((((u_int64_t)THH_MIN_FW_VERSION)>>16) & MAKE_ULONGLONG(0xFFFF));
      req_fw_submin_version = (u_int16_t)  (((u_int64_t)THH_MIN_FW_VERSION) & MAKE_ULONGLONG(0xFFFF));
      MTL_ERROR1("THH_hob_create: INSTALLED FIRMWARE VERSION IS NOT SUPPORTED:\n                     Installed: %x.%x.%x, Minimum Required: %x.%x.%x\n\n",
                 hob_p->fw_props.fw_rev_major, hob_p->fw_props.fw_rev_minor, hob_p->fw_props.fw_rev_subminor,
                 req_fw_maj_version, req_fw_min_version, req_fw_submin_version);
      ret = HH_ERR;
      goto undo_sys_ena;
  }

  /* map the firmware error buffer if the appropriate fw version is installed */
  if ((fw_version >= THH_MIN_FW_ERRBUF_VERSION) &&
      (hob_p->fw_props.error_buf_start != (u_int64_t) 0) &&
      (hob_p->fw_props.error_buf_size != 0)) 
  {

    /* wa for FW bug number 19695 */
    if ( (hob_p->fw_props.error_buf_start<hw_props_p->cr_base) || (hob_p->fw_props.error_buf_start>(hw_props_p->cr_base+0x100000)) ) {
      MTL_ERROR1(MT_FLFMT("%s: fw_props.error_buf_start is outside of cr-space start="U64_FMT", size=0x%x"),
                 __func__, hob_p->fw_props.error_buf_start, hob_p->fw_props.error_buf_size);
      ret = HH_ERR;
      goto undo_sys_ena;
    }
  
      MTL_DEBUG4(MT_FLFMT("THH_hob_create: using cat err buf. pa=0x"U64_FMT", sz=%d"),
                 hob_p->fw_props.error_buf_start, hob_p->fw_props.error_buf_size);
      hob_p->fw_error_buf_start_va = MOSAL_io_remap(hob_p->fw_props.error_buf_start, 
                                                    4*(hob_p->fw_props.error_buf_size));
      if (hob_p->fw_error_buf_start_va == (MT_virt_addr_t)(MT_ulong_ptr_t) NULL) {
          MTL_ERROR1(MT_FLFMT("%s: Could not map fw error buffer (phys addr = "U64_FMT", size=%d"),
                      __func__, hob_p->fw_props.error_buf_start, hob_p->fw_props.error_buf_size);
      } else {
          hob_p->fw_error_buf = TNMALLOC(u_int32_t,hob_p->fw_props.error_buf_size);
          if (hob_p->fw_error_buf == NULL) {
              MTL_ERROR1(MT_FLFMT("%s: Could not allocate buffer for FW catastrophic error info"),__func__);
          }
      }
  }

  /* Get device limits */
  cmd_ret = THH_cmd_QUERY_DEV_LIM(hob_p->cmd, &(hob_p->dev_lims));
  if (cmd_ret != THH_CMD_STAT_OK) {
    if (cmd_ret == THH_CMD_STAT_EFATAL) {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: FATAL ERROR in THH_cmd_QUERY_DEV_LIM"));
        ret = HH_EFATAL;
    } else {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: CMD_error in THH_cmd_QUERY_DEV_LIM (%d)"), cmd_ret);
        ret = HH_ERR;
    }
    goto undo_sys_ena;
  }

  MTL_DEBUG1(MT_FLFMT("%s: log_max_srq=%u  log2_rsvd_srqs=%u  srq_entry_sz=%u  srq=%ssupported"), __func__,
             hob_p->dev_lims.log_max_srqs, hob_p->dev_lims.log2_rsvd_srqs, 
             hob_p->dev_lims.srq_entry_sz, hob_p->dev_lims.srq ? " ":"NOT-");

  /* Enable SRQ only for FW version 3.1 and up */
  if ((hob_p->dev_lims.srq) && 
      (fw_version < THH_MIN_FW_VERSION_SRQ)) {
      MTL_ERROR1("%s: Disabling SRQ support due to FW version: "
	         "Installed: %x.%x.%x, Minimum Required: 3.1.0\n", __func__,
                 hob_p->fw_props.fw_rev_major, hob_p->fw_props.fw_rev_minor, hob_p->fw_props.fw_rev_subminor);
      hob_p->dev_lims.srq= FALSE;
  }


  /* query tavor for DDR memory resources data */
  cmd_ret = THH_cmd_QUERY_DDR(hob_p->cmd, &(hob_p->ddr_props));
  if (cmd_ret != THH_CMD_STAT_OK) {
    if (cmd_ret == THH_CMD_STAT_EFATAL) {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: FATAL ERROR in THH_cmd_QUERY_DDR"));
        ret = HH_EFATAL;
    } else {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: CMD_error in THH_cmd_QUERY_DDR (%d)"), cmd_ret);
        ret = HH_ERR;
    }
    goto undo_sys_ena;
  }

  /* HIDE-DDR set in firmware:  sanity checks */
#if 0
  /* 1. fail if using 32-bit platform (not PAE and not IA64) */
  if ((hob_p->ddr_props.dh == TRUE) && (sizeof(MT_phys_addr_t) <=4)) {
      MTL_ERROR1("THH_hob_create: HIDE_DDR is not supported on platforms using 32-bit physical addresses\n\n");
      ret = HH_ERR;
      goto undo_sys_ena;
  }
#endif

  /* 2. Fail if firmware version is not recent enough. */
  if ((hob_p->ddr_props.dh == TRUE) && (fw_version < THH_MIN_FW_HIDE_DDR_VERSION)) {
      req_fw_maj_version = (u_int32_t)  ((((u_int64_t)THH_MIN_FW_HIDE_DDR_VERSION)>>32) & 0xFFFFFFFF);
      req_fw_min_version = (u_int16_t)  ((((u_int64_t)THH_MIN_FW_HIDE_DDR_VERSION)>>16) & 0xFFFF);
      req_fw_submin_version = (u_int16_t)  (((u_int64_t)THH_MIN_FW_HIDE_DDR_VERSION) & 0xFFFF);
      MTL_ERROR1("THH_hob_create: INSTALLED FIRMWARE VERSION DOES NOT SUPPORT HIDE_DDR:\n                     Installed: %x.%x.%x, Minimum Required: %x.%x.%x\n\n",
                 hob_p->fw_props.fw_rev_major, hob_p->fw_props.fw_rev_minor, hob_p->fw_props.fw_rev_subminor,
                 req_fw_maj_version, req_fw_min_version, req_fw_submin_version);
      ret = HH_ERR;
      goto undo_sys_ena;
  }
  
#ifdef DEBUG_MEM_OV
cmdif_dbg_ddr = hob_p->ddr_props.ddr_start_adr; /* address in ddr used for out params in debug mode */
#endif

  /* print info messages that device is operating in HIDE DDR mode (not an error) */
  if (hob_p->ddr_props.dh == TRUE) {
      MTL_ERROR1("Device %s is operating in HIDE_DDR mode.\n", hob_p->dev_name);
  }
  

/* query tavor for adapter data */
  cmd_ret = THH_cmd_QUERY_ADAPTER(hob_p->cmd, &(hob_p->adapter_props));
  if (cmd_ret != THH_CMD_STAT_OK) {
    if (cmd_ret == THH_CMD_STAT_EFATAL) {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: FATAL ERROR in THH_cmd_QUERY_ADAPTER"));
        ret = HH_EFATAL;
    } else {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: CMD_error in THH_cmd_QUERY_ADAPTER (%d)"), cmd_ret);
        ret = HH_ERR;
    }
    goto undo_sys_ena;
  }
  if ( (hob_p->fw_props.fw_end_addr <= hob_p->fw_props.fw_base_addr)  ||
        hob_p->fw_props.fw_base_addr < hob_p->ddr_props.ddr_start_adr ||
        hob_p->fw_props.fw_end_addr > hob_p->ddr_props.ddr_end_adr) {
      /* FW region is either improper, or does not lie within bounds of DDR */
      MTL_ERROR1("THH_hob_create: FW region is either improper, or is outside DDR\nFW end  = "U64_FMT
                 ", FW start = "U64_FMT"\n   DDR end = "U64_FMT", DDR start = "U64_FMT"\n", 
                 hob_p->fw_props.fw_end_addr, hob_p->fw_props.fw_base_addr, 
                 hob_p->ddr_props.ddr_end_adr, hob_p->ddr_props.ddr_start_adr);
      ret = HH_ERR;
      goto undo_sys_ena;

  }
  fw_size = (MT_size_t) (hob_p->fw_props.fw_end_addr - hob_p->fw_props.fw_base_addr + 1);

  if (hob_p->ddr_props.ddr_end_adr < hob_p->ddr_props.ddr_start_adr) {
      MTL_ERROR1("THH_hob_create: DDR end address ("U64_FMT") is less than DDR base addr (" U64_FMT ")\n", 
                 hob_p->ddr_props.ddr_end_adr, hob_p->ddr_props.ddr_start_adr);
      ret = HH_ERR;
      goto undo_sys_ena;
  }

  ddr_size = (MT_size_t) (hob_p->ddr_props.ddr_end_adr - hob_p->ddr_props.ddr_start_adr + 1) ;
  hob_p->profile.ddr_size = ddr_size - fw_size;

  /* DDR size code is used in THH_calculate_profile to set number of QPs proportionally to size */
  hob_p->profile.ddr_size_code = THH_get_ddr_size_code(ddr_size - fw_size);

  ret = THH_ddrmm_create((MT_phys_addr_t) (hob_p->ddr_props.ddr_start_adr), ddr_size, &(hob_p->ddrmm));
  if (ret != HH_OK) {
    MTL_ERROR1("THH_hob_create: could not create DDRMM object (%d)\n", ret);
    goto undo_sys_ena;
  }

  ret = THH_ddrmm_reserve(hob_p->ddrmm, hob_p->fw_props.fw_base_addr, fw_size);
  if (ret != HH_OK) {
    MTL_ERROR1("THH_hob_create: could not reserve FW space in DDRMM object (err = %d)\n", ret);
    goto undo_ddrm_create;
  }
  
#ifdef DEBUG_MEM_OV
  ret = THH_ddrmm_reserve(hob_p->ddrmm, hob_p->ddr_props.ddr_start_adr, CMDIF_SIZE_IN_DDR);
  if (ret != HH_OK) {
    MTL_ERROR1("THH_hob_create: could not reserve DDR Outbox space in DDRMM object (err = %d)\n", ret);
    goto undo_ddrm_create;
  }
#endif

  /*create the delay unlock object for catastrophic error use */
  int_ret = VIP_delay_unlock_create(&hob_p->delay_unlocks);
  if (int_ret != 0) {
      MTL_ERROR1("THH_hob_create: could create delay unlock object (err = %d)\n", int_ret);
      ret = HH_ENOMEM;
      goto delay_unlock_err;
  }
  

#ifndef __DARWIN__   /* TODO, need to take care of fatal errors in Darwin */
  
  /* launch catastrophic error thread */
  hob_p->fatal_thread_obj.hob = (struct THH_hob_st *)hob_p;
  MOSAL_syncobj_init(&hob_p->fatal_thread_obj.start_sync);
  MOSAL_syncobj_init(&hob_p->fatal_thread_obj.stop_sync);
  MOSAL_syncobj_init(&hob_p->fatal_thread_obj.fatal_err_sync);
  MOSAL_syncobj_init(&hob_p->thh_fatal_complete_syncobj);
  hob_p->fatal_thread_obj.have_fatal = FALSE;
  mosal_ret = MOSAL_thread_start(&hob_p->fatal_thread_obj.mto, MOSAL_KTHREAD_CLONE_FLAGS,
                                 THH_hob_fatal_err_thread, (void *)(&(hob_p->fatal_thread_obj)));
//  if (mosal_ret != MT_OK) {
//      MTL_ERROR1("THH_hob_create: could not create fatal error thread (err = %d)\n", mosal_ret);
//      ret = HH_ERR;
//      goto fatal_thr_create_err;
//  }
  
  /*wait for fatal thread initialization complete */
  mosal_ret = MOSAL_syncobj_waiton(&(hob_p->fatal_thread_obj.start_sync), 10000000);
  if (mosal_ret != MT_OK) {
      if (mosal_ret == MT_EINTR) {
          MTL_DEBUG1(MT_FLFMT("%s: Received OS interrupt while initializing fatal error thread (err = %d)"), 
                     __func__,mosal_ret);
          ret = HH_EINTR;
      } else {
          MTL_ERROR1(MT_FLFMT("%s: Timeout on initializing fatal error thread (err = %d)"), 
                     __func__,mosal_ret);
          ret = HH_ERR;
      }
      goto fatal_thr_init_err;
  }
  MTL_DEBUG4("%s: Created send completion thread.\n", __func__);
  /* set up the procedure mapping table and register the tavor device */

#endif  /* ! defined __DARWIN__ */

  if_ops_p = &(hob_p->if_ops);

#ifndef IVAPI_THH  
  HH_ifops_tbl_set_enosys(if_ops_p); /* by default, all retuen HH_ENOSYS */
#endif  

  /* HCA Calls */
  if_ops_p->HHIF_open_hca           = &THH_hob_open_hca;
  if_ops_p->HHIF_close_hca          = &THH_hob_close_hca;
  if_ops_p->HHIF_alloc_ul_resources = &THH_hob_alloc_ul_res;
  if_ops_p->HHIF_free_ul_resources  = &THH_hob_free_ul_res;
  if_ops_p->HHIF_query_hca          = &THH_hob_query;
  if_ops_p->HHIF_modify_hca         = &THH_hob_modify;

  /* Misc HCA Operations*/
  if_ops_p->HHIF_query_port_prop        = &THH_hob_query_port_prop;
  if_ops_p->HHIF_get_pkey_tbl           = &THH_hob_get_pkey_tbl;
  if_ops_p->HHIF_get_gid_tbl            = &THH_hob_get_gid_tbl;

  /* Protection Domain Calls */
  if_ops_p->HHIF_alloc_pd               = &THH_hob_alloc_pd;
  if_ops_p->HHIF_free_pd                = &THH_hob_free_pd;
  if_ops_p->HHIF_alloc_rdd              = &THH_hob_alloc_rdd;
  if_ops_p->HHIF_free_rdd               = &THH_hob_free_rdd;

  /* privileged UD AV */
  if_ops_p->HHIF_create_priv_ud_av      = &THH_hob_create_ud_av;
  if_ops_p->HHIF_modify_priv_ud_av      = &THH_hob_modify_ud_av;
  if_ops_p->HHIF_query_priv_ud_av       = &THH_hob_query_ud_av;
  if_ops_p->HHIF_destroy_priv_ud_av     = &THH_hob_destroy_ud_av;

  /* Memory Registration */
  if_ops_p->HHIF_register_mr     = &THH_hob_register_mr;
  if_ops_p->HHIF_reregister_mr   = &THH_hob_reregister_mr;
  if_ops_p->HHIF_register_smr    = &THH_hob_register_smr;
  if_ops_p->HHIF_query_mr        = &THH_hob_query_mr;
  if_ops_p->HHIF_deregister_mr   = &THH_hob_deregister_mr;

  if_ops_p->HHIF_alloc_mw        = &THH_hob_alloc_mw;
  if_ops_p->HHIF_query_mw        = &THH_hob_query_mw;
  if_ops_p->HHIF_free_mw         = &THH_hob_free_mw;

  /* Fast memory regions */
  if_ops_p->HHIF_alloc_fmr       = &THH_hob_alloc_fmr;
  if_ops_p->HHIF_map_fmr         = &THH_hob_map_fmr;
  if_ops_p->HHIF_unmap_fmr       = &THH_hob_unmap_fmr;
  if_ops_p->HHIF_free_fmr        = &THH_hob_free_fmr;

  /* Completion Queues */
  if_ops_p->HHIF_create_cq       = &THH_hob_create_cq;
  if_ops_p->HHIF_resize_cq       = &THH_hob_resize_cq;
  if_ops_p->HHIF_query_cq        = &THH_hob_query_cq;
  if_ops_p->HHIF_destroy_cq      = &THH_hob_destroy_cq;

  /* Queue Pair */
  if_ops_p->HHIF_create_qp       = &THH_hob_create_qp;
  if_ops_p->HHIF_get_special_qp  = &THH_hob_get_special_qp;
  if_ops_p->HHIF_modify_qp       = &THH_hob_modify_qp;
  if_ops_p->HHIF_query_qp        = &THH_hob_query_qp;
  if_ops_p->HHIF_destroy_qp      = &THH_hob_destroy_qp;
#if defined(MT_SUSPEND_QP)
  if_ops_p->HHIF_suspend_qp      = &THH_hob_suspend_qp;
  if_ops_p->HHIF_suspend_cq      = &THH_hob_suspend_cq;
#endif
  /* SRQ */
  if_ops_p->HHIF_create_srq      = &THH_hob_create_srq;
  if_ops_p->HHIF_query_srq       = &THH_hob_query_srq;
  if_ops_p->HHIF_destroy_srq     = &THH_hob_destroy_srq;

  /* EEC */
  if_ops_p->HHIF_create_eec      = &THH_hob_create_eec;
  if_ops_p->HHIF_modify_eec      = &THH_hob_modify_eec;
  if_ops_p->HHIF_query_eec       = &THH_hob_query_eec;
  if_ops_p->HHIF_destroy_eec     = &THH_hob_destroy_eec;

  if_ops_p->HHIF_set_comp_eventh  = &THH_hob_set_comp_eventh;
  if_ops_p->HHIF_set_async_eventh = &THH_hob_set_async_eventh;
  
  
  
  /* Multicast groups */
  if_ops_p->HHIF_attach_to_multicast   = &THH_hob_attach_to_multicast;
  if_ops_p->HHIF_detach_from_multicast = &THH_hob_detach_from_multicast;
  
  /* Process local MAD */
  if_ops_p->HHIF_process_local_mad = &THH_hob_process_local_mad;
  
  if_ops_p->HHIF_ddrmm_alloc = &THH_hob_ddrmm_alloc;
  if_ops_p->HHIF_ddrmm_query = &THH_hob_ddrmm_query;
  if_ops_p->HHIF_ddrmm_free = &THH_hob_ddrmm_free;

  /*
   *  Register device in the init structure
   *
   */
  tdev.dev_desc  = hob_p->dev_name;
  tdev.user_lib  = "TBD libhcatavor";   /* for future dynamic use */
  tdev.vendor_id = MT_MELLANOX_IEEE_VENDOR_ID;
  tdev.dev_id    = (u_int32_t)hw_props_p->device_id;
  MTL_DEBUG1("hw_props_p: device_id = 0x%X, pci_vendor_id=0x%X,hw_ver=0x%X\n",
              hw_props_p->device_id, hw_props_p->pci_vendor_id, hw_props_p->hw_ver);
  tdev.hw_ver    = hob_p->hw_props.hw_ver; 
  tdev.if_ops    = if_ops_p;
  tdev.hca_ul_resources_sz = sizeof(THH_hca_ul_resources_t);
  tdev.pd_ul_resources_sz = sizeof(THH_pd_ul_resources_t);
  tdev.cq_ul_resources_sz = sizeof(THH_cq_ul_resources_t);
  tdev.srq_ul_resources_sz = sizeof(THH_srq_ul_resources_t);
  tdev.qp_ul_resources_sz = sizeof(THH_qp_ul_resources_t);
  tdev.device = (void *) hob_p;
  tdev.status = HH_HCA_STATUS_CLOSED;

  /* Grab the mutex now, just before adding the device to HH */
  MTL_DEBUG4("THH_hob_create:  about to grab mutex\n");
  if (MOSAL_mutex_acq(&(hob_p->mtx), TRUE) != MT_OK) {
      MTL_DEBUG1(MT_FLFMT("%s: Received signal. returning HH_EINTR"), __func__);
      ret = HH_EINTR;
      goto err_acq_mutex;
  }

  MTL_DEBUG4("THH_hob_create:  Before HH_add_hca_dev\n");
  ret = HH_add_hca_dev(&tdev, &new_hh_hndl);
  if (ret != HH_OK) {
    MTL_ERROR1("THH_hob_create: could not register device %s in HCA HAL\n",
                            hob_p->dev_name);
    goto err_release_mutex;
  }

  /* insert HH hca handle into device structure */
  ((THH_hob_t)(new_hh_hndl->device))->hh_hca_hndl =  new_hh_hndl;
  MTL_TRACE1("THH_hob_create: hh_hca_hndl created = %p\n", (void *) new_hh_hndl);

  if (hh_hndl_p != NULL) {
      *hh_hndl_p =  new_hh_hndl;
  }
  MOSAL_spinlock_irq_lock(&hob_p->fatal_spl);
  hob_p->thh_state = THH_STATE_CLOSED;
  MOSAL_spinlock_unlock(&hob_p->fatal_spl);
  
  MOSAL_mutex_rel(&(hob_p->mtx));

  return(HH_OK);

  /* ERROR HANDLING: undoes previous steps in reverse order, as needed */
err_release_mutex:  
  MOSAL_mutex_rel(&(hob_p->mtx));
err_acq_mutex:  
  VIP_delay_unlock_destroy(hob_p->delay_unlocks);

fatal_thr_init_err:
  /* signal the waited-on sync object, so that thread exits */
  MOSAL_syncobj_signal(&(hob_p->fatal_thread_obj.fatal_err_sync));

//fatal_thr_create_err:
delay_unlock_err:
undo_ddrm_create:
  THH_ddrmm_destroy(hob_p->ddrmm);

undo_sys_ena:
  if (hob_p->fw_error_buf_start_va != (MT_virt_addr_t)(MT_ulong_ptr_t) NULL)  {
     MOSAL_io_unmap(hob_p->fw_error_buf_start_va);
  }

  if (hob_p->fw_error_buf != NULL) {
      FREE(hob_p->fw_error_buf);
  }

cmd_ret = THH_cmd_SYS_DIS(hob_p->cmd);
if (cmd_ret != THH_CMD_STAT_OK) {
    if (cmd_ret == THH_CMD_STAT_EFATAL) {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: FATAL ERROR in THH_cmd_SYS_DIS"));
    } else {
        MTL_ERROR1(MT_FLFMT("THH_hob_create: CMD_error in THH_cmd_SYS_DIS (%d)"), cmd_ret);
    }
}
cmd_err:
  THH_cmd_destroy(hob_p->cmd);
  
err_free_hob:
  MOSAL_mutex_free(&(hob_p->mtx));
  FREE(hob_p);
  return (ret);
}


/*****************************************************************************
******************************************************************************
************** EXTERNALLY VISIBLE FUNCTIONS, WITH PROTOTYPES IN THH_HOB.H ****
******************************************************************************
*****************************************************************************/


HH_ret_t THH_hob_get_ver_info ( /*IN*/  THH_hob_t        hob, 
                                /*OUT*/ THH_ver_info_t  *version_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_ver_info: ERROR : No device registered\n");
        return HH_EINVAL;
    }
    
    memcpy(version_p, &(hob->version_info), sizeof(THH_ver_info_t));
    return HH_OK;
}


HH_ret_t THH_hob_get_cmd_if ( /*IN*/  THH_hob_t   hob, 
                              /*OUT*/ THH_cmd_t   *cmd_if_p )
{
    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_cmd_if: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->cmd == THH_CMDIF_INVALID_HANDLE) {
        MTL_ERROR1("THH_hob_get_cmd_if: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }

    *cmd_if_p = hob->cmd;
    return HH_OK;
}

HH_ret_t THH_hob_get_uldm ( /*IN*/ THH_hob_t hob, 
                            /*OUT*/ THH_uldm_t *uldm_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_uldm: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_uldm: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }

    *uldm_p = hob->uldm;
    return HH_OK;
}

HH_ret_t THH_hob_get_ddrmm ( /*IN*/ THH_hob_t hob, 
                            /*OUT*/ THH_ddrmm_t *ddrmm_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_ddrmm: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->ddrmm == (THH_ddrmm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_ddrmm: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }

    *ddrmm_p = hob->ddrmm;
    return HH_OK;
}

HH_ret_t THH_hob_get_mrwm ( /*IN*/ THH_hob_t hob, 
                            /*OUT*/ THH_mrwm_t *mrwm_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_mrwm: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_mrwm: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }
   
    *mrwm_p = hob->mrwm;
   return HH_OK;
}

HH_ret_t THH_hob_get_qpm ( /*IN*/ THH_hob_t hob, 
                           /*OUT*/ THH_qpm_t *qpm_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_qpm: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_qpm: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }
   
    *qpm_p = hob->qpm;
   return HH_OK;
}

HH_ret_t THH_hob_get_cqm ( /*IN*/ THH_hob_t hob, 
                           /*OUT*/ THH_cqm_t *cqm_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_cqm: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->cqm == (THH_cqm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_cqm: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }
   
    *cqm_p = hob->cqm;
   return HH_OK;
}

HH_ret_t THH_hob_get_eventp ( /*IN*/ THH_hob_t hob, 
                             /*OUT*/ THH_eventp_t *eventp_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_eventp: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (hob->eventp== (THH_eventp_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_eventp: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }
   
    *eventp_p = hob->eventp;
   return HH_OK;
}
HH_ret_t THH_hob_get_udavm_info ( /*IN*/ THH_hob_t hob, 
                                  /*OUT*/ THH_udavm_t *udavm_p,
                                  /*OUT*/ MT_bool *use_priv_udav,
                                  /*OUT*/ MT_bool *av_in_host_mem,
                                  /*OUT*/ VAPI_lkey_t  *lkey ,
                                  /*OUT*/ u_int32_t    *max_ah_num,
                                  /*OUT*/ MT_bool *hide_ddr)
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_udavm_info: ERROR : No device registered\n");
        return HH_EINVAL;
    }

    *av_in_host_mem = (MT_bool) (hob->module_flags.av_in_host_mem);

    *use_priv_udav = hob->udavm_use_priv;
    *hide_ddr = (hob->ddr_props.dh ? TRUE : FALSE);
    *max_ah_num = hob->hca_capabilities.max_ah_num;
    if (!(hob->udavm_use_priv)) {
        return HH_OK;
    }

    if (hob->udavm == (THH_udavm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_get_udavm_info: ERROR : HCA device has not yet been opened\n");
        return HH_EINVAL;
    }
   
    *udavm_p = hob->udavm;
    *lkey    = hob->udavm_lkey;

   return HH_OK;
}


HH_ret_t THH_hob_get_hca_hndl ( /*IN*/  THH_hob_t hob, 
                                /*OUT*/ HH_hca_hndl_t *hca_hndl_p )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_hca_hndl: ERROR : No device registered\n");
        return HH_EINVAL;
    }
   
    *hca_hndl_p = hob->hh_hca_hndl;
    return HH_OK;
}

HH_ret_t THH_hob_check_qp_init_attrs ( /*IN*/ THH_hob_t hob,
                                       /*IN*/ HH_qp_init_attr_t * init_attr_p,
                                       /*IN*/ MT_bool is_special_qp )
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hob == NULL) {
        MTL_ERROR1("THH_hob_get_check_qp_init_attrs: ERROR : No device registered\n");
        return HH_EINVAL;
    }


    if (init_attr_p == NULL) {
        MTL_ERROR1("THH_hob_get_check_qp_init_attrs: ERROR : null attributes\n");
        return HH_EINVAL;
    }

    if (init_attr_p->qp_cap.max_oust_wr_rq > hob->hca_capabilities.max_qp_ous_wr  ||
        init_attr_p->qp_cap.max_oust_wr_sq > hob->hca_capabilities.max_qp_ous_wr){
        MTL_ERROR1("%s : max outs work requests more than HCA maximum\n", __func__);
        return HH_E2BIG_WR_NUM;
    }

    if (is_special_qp  || init_attr_p->ts_type != VAPI_TS_RD) {
        if (init_attr_p->qp_cap.max_sg_size_rq > hob->hca_capabilities.max_num_sg_ent ||
                   init_attr_p->qp_cap.max_sg_size_sq > hob->hca_capabilities.max_num_sg_ent) {
            MTL_ERROR1("%s : max s/g list size more than HCA maximum\n", __func__);
            return HH_E2BIG_SG_NUM;
        }
    } else {
        /* is RD */
        if (init_attr_p->qp_cap.max_sg_size_rq > hob->hca_capabilities.max_num_sg_ent_rd ||
                   init_attr_p->qp_cap.max_sg_size_sq > hob->hca_capabilities.max_num_sg_ent_rd) {
            MTL_ERROR1("%s : max s/g list size more than HCA maximum\n", __func__);
            return HH_E2BIG_SG_NUM;
        } 
    }
    return HH_OK;
}

/* Used in restarting HCA on fatal error, in function THH_hob_restart */
HH_ret_t   THH_hob_get_init_params(/*IN*/ THH_hob_t  thh_hob_p, 
                   /*OUT*/  THH_hw_props_t     *hw_props_p,
                   /*OUT*/  u_int32_t          *hca_seq_num,
                   /*OUT*/  THH_module_flags_t *mod_flags)
{

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : No device registered"), __func__);  
        return HH_EAGAIN;
    }
    memcpy(hw_props_p, &thh_hob_p->hw_props, sizeof(THH_hw_props_t));
    *hca_seq_num = thh_hob_p->hca_seq_num;
    memcpy(mod_flags, &thh_hob_p->module_flags, sizeof(THH_module_flags_t));
    return HH_OK;
}
static HH_ret_t   THH_hob_halt_hca(/*IN*/ THH_hob_t hob)
{
#ifdef SIMULATE_HALT_HCA
    THH_cmd_CLOSE_IB(hob->cmd,1);
    THH_cmd_CLOSE_IB(hob->cmd,2);
    THH_cmd_CLOSE_HCA(hob->cmd, FALSE);
    THH_cmd_SYS_DIS(hob->cmd);
#else
    THH_cmd_status_t  stat;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    stat = THH_cmd_CLOSE_HCA(hob->cmd, TRUE); // HALT the HCA
    MTL_ERROR1(MT_FLFMT("%s: HALT HCA returned 0x%x"), __func__,stat);  
#endif
    return HH_OK;
}

static VAPI_event_syndrome_t get_fatal_err_syndrome(THH_hob_t hob) 
{
    u_int32_t    temp;
    int          i;
    
    if ((hob->fw_error_buf_start_va != (MT_virt_addr_t)(MT_ulong_ptr_t) NULL) && 
        (hob->fw_error_buf != NULL) && 
        (hob->fw_props.error_buf_size > 0) ) {
        MOSAL_MMAP_IO_READ_BUF_DWORD(hob->fw_error_buf_start_va,
                                 hob->fw_error_buf,hob->fw_props.error_buf_size);
        /* check for non-zero data in fw catastrophic error buffer */
        temp = 0;
        for (i = 0; i < (int) hob->fw_props.error_buf_size; i++) {
            temp |= hob->fw_error_buf[i];
        }
        if (temp == 0) {
            return VAPI_CATAS_ERR_GENERAL; 
        } else {
            /* Have non-zero data. print out the syndrome details, and return general category */
            for (i = 0; i < (int) hob->fw_props.error_buf_size; i++) {
                MTL_ERROR1(MT_FLFMT("get_fatal_err_syndrome: FW CATASTR ERRBUF[%d] = 0x%x"),
                           i, MOSAL_be32_to_cpu(hob->fw_error_buf[i]));
            }
            switch( (MOSAL_be32_to_cpu(hob->fw_error_buf[0])>>24) & 0xFF) {
            case TAVOR_IF_EV_CATAS_ERR_FW_INTERNAL_ERR:
                return VAPI_CATAS_ERR_FW_INTERNAL;
            case TAVOR_IF_EV_CATAS_ERR_MISBEHAVED_UAR_PAGE:
                return VAPI_CATAS_ERR_MISBEHAVED_UAR_PAGE;
            case TAVOR_IF_EV_CATAS_ERR_UPLINK_BUS_ERR:
                return VAPI_CATAS_ERR_UPLINK_BUS_ERR;
            case TAVOR_IF_EV_CATAS_ERR_HCA_DDR_DATA_ERR:
                return VAPI_CATAS_ERR_HCA_DDR_DATA_ERR;
            case TAVOR_IF_EV_CATAS_ERR_INTERNAL_PARITY_ERR:
                return VAPI_CATAS_ERR_INTERNAL_PARITY_ERR;
            default:
                return VAPI_CATAS_ERR_GENERAL; 
            }
        }
    } else {
        /* no access to the fw cat error buffer */
        return VAPI_CATAS_ERR_GENERAL; 
    }
}
/****************************************************************************************
 * name:      MGT_HOB_rcv
 * function:  handles receive channel listening 
 * args:
 * returns:
 * descr:     This procedure is spawned as a thread main routine .
 *            posts the initial receive buffers, then handles the completion queue polling
 *            and data recording of Completion Queue events (the FIFO) for a test session
 ****************************************************************************************/

HH_ret_t   THH_hob_fatal_error(/*IN*/ THH_hob_t hob,
                               /*IN*/ THH_fatal_err_t  fatal_err_type,
                               /*IN*/ VAPI_event_syndrome_t  syndrome)
{
#ifndef __DARWIN__
    THH_cmd_t    cmd_if;
    THH_eventp_t eventp;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    FUNC_IN;
    MTL_DEBUG1(MT_FLFMT("%s: device=%s, err_type=%d, syndrome=%d"), __func__,  
               hob->dev_name, fatal_err_type, syndrome);

    if (hob == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : No device registered"), __func__);  
        MT_RETURN(HH_EAGAIN);
    }
    
    /* make sure that only one invocation is allowed */
    MOSAL_spinlock_irq_lock(&hob->fatal_spl);
    if ((hob->thh_state & THH_STATE_HAVE_ANY_FATAL) != 0) {
        /* already in FATAL state */
        MTL_DEBUG4(MT_FLFMT("%s: already in FATAL state"), __func__);  
        MOSAL_spinlock_unlock(&hob->fatal_spl);
        MT_RETURN(HH_OK);
    }

    MTL_ERROR1(MT_FLFMT("%s: device=%s, err_type=%d, syndrome=%d"), __func__,  
               hob->dev_name, fatal_err_type, syndrome);
    
    switch(fatal_err_type) {
    /* get syndrome from iomapped firmware memory */
    case THH_FATAL_MASTER_ABORT:  /* detected master abort */
        hob->fatal_syndrome = VAPI_CATAS_ERR_MASTER_ABORT;
        break;
    case THH_FATAL_GOBIT:         /* GO bit of HCR remains set (i.e., stuck) */
        hob->fatal_syndrome = VAPI_CATAS_ERR_GO_BIT;
        break;
    case THH_FATAL_CMD_TIMEOUT:   /* timeout on a command execution */
        hob->fatal_syndrome = VAPI_CATAS_ERR_CMD_TIMEOUT;
        break;
    case THH_FATAL_EQ_OVF:        /* an EQ has overflowed */
        hob->fatal_syndrome = VAPI_CATAS_ERR_EQ_OVERFLOW;
        break;
    case THH_FATAL_EVENT:    /* firmware has generated a LOCAL CATASTROPHIC ERR event */
        hob->fatal_syndrome = get_fatal_err_syndrome(hob);
        break;
    case THH_FATAL_CR:            /* unexpected read from CR-space */
        hob->fatal_syndrome = VAPI_CATAS_ERR_EQ_OVERFLOW;
        break;
    case THH_FATAL_TOKEN:         /* invalid token on command completion */
        hob->fatal_syndrome = VAPI_CATAS_ERR_FATAL_TOKEN;
        break;
    case THH_FATAL_NONE: 
    default:
        hob->fatal_syndrome = VAPI_CATAS_ERR_GENERAL;
    }

    MTL_ERROR1(MT_FLFMT("%s: Fatal Event Syndrome = %s (%d)"), 
               __func__,VAPI_event_syndrome_sym(hob->fatal_syndrome), hob->fatal_syndrome);

    if (hob->thh_state == THH_STATE_RUNNING) {
        /* make use of thread to perform HALT and signal user apps */
        hob->thh_state |= THH_STATE_FATAL_START;
    } else  {
        /* creating, opening, closing, or destroying HCA.
         * Indicate HCA_HALTED directly.
         */
        hob->thh_state |= THH_STATE_FATAL_HCA_HALTED;
    }
    MOSAL_spinlock_unlock(&hob->fatal_spl);

    /* notify cmd and eventp objects, if they exist */
    if (THH_hob_get_cmd_if(hob, &cmd_if) == HH_OK) {
        THH_cmd_notify_fatal(cmd_if, fatal_err_type);
    }
    
    if (THH_hob_get_eventp(hob,&eventp) == HH_OK) {
       THH_eventp_notify_fatal(eventp, fatal_err_type);
    }

    /* now, signal the fatal error thread, ONLY IF WE WERE IN RUNNING STATE */ 
    if ((hob->thh_state & THH_STATE_RUNNING) != 0) {
        hob->fatal_thread_obj.have_fatal = TRUE;
        MTL_TRACE1(MT_FLFMT("%s: signalling fatal thread"), __func__);  
        MOSAL_syncobj_signal(&hob->fatal_thread_obj.fatal_err_sync);
    }

#endif /* not defined __DARWIN__  - TODO in darwin, implement the fatal error handling */
    MT_RETURN(HH_OK);
}

HH_ret_t  THH_hob_get_state(THH_hob_t thh_hob_p, THH_hob_state_t *fatal_state)
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : No device registered"), __func__);  
        return HH_EAGAIN;
    }
    
    if (fatal_state == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : NULL fatal_state parameter"), __func__);  
        return HH_EINVAL;
    }

    MOSAL_spinlock_irq_lock(&thh_hob_p->fatal_spl);
    *fatal_state = thh_hob_p->thh_state;
    MOSAL_spinlock_unlock(&thh_hob_p->fatal_spl);
    return HH_OK;
}

HH_ret_t  THH_hob_get_fatal_syncobj(THH_hob_t thh_hob_p, MOSAL_syncobj_t *syncobj)
{
  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : No device registered"), __func__);  
        return HH_EAGAIN;
    }
    
    if (syncobj == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : NULL syncobj return parameter"), __func__);  
        return HH_EINVAL;
    }

    *syncobj = thh_hob_p->thh_fatal_complete_syncobj;
    return HH_OK;
}

HH_ret_t THH_hob_wait_if_fatal(THH_hob_t thh_hob_p, MT_bool *had_fatal)
{
    THH_hob_state_t   state;

    FUNC_IN;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: Received NULL HOB pointer"), __func__);  
        *had_fatal = FALSE;
        MT_RETURN(HH_OK);
    }

    /*get fatal state value */
    MOSAL_spinlock_irq_lock(&(thh_hob_p->fatal_spl));
    state = thh_hob_p->thh_state;
    MOSAL_spinlock_unlock(&(thh_hob_p->fatal_spl));
    
    MTL_DEBUG4(MT_FLFMT("%s: FATAL STATE=%d"), __func__, state);  

    if ((state & THH_STATE_HAVE_ANY_FATAL) == 0) {
        *had_fatal = FALSE;
        MT_RETURN(HH_OK);
    }
    
    /* We were in running state.  Wait for fatal thread to complete HCA HALT */
    if ((state & THH_STATE_FATAL_START) != 0) {
        MOSAL_syncobj_waiton_ui(&thh_hob_p->thh_fatal_complete_syncobj, 10000000);
    }
    
    /* We are in the FATAL_HCA_HALTED compound state */
    *had_fatal = TRUE;
    MT_RETURN(HH_OK);

}

HH_ret_t  THH_hob_restart(/*IN*/ HH_hca_hndl_t  hca_hndl)
{
    THH_hw_props_t     hw_props;
    u_int32_t          hca_seq_num;
    THH_module_flags_t mod_flags;
    THH_hob_t          thh_hob_p;
    HH_hca_hndl_t      new_hh_hca_hndl; 
    HH_ret_t           rc;
    

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    FUNC_IN;
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1(MT_FLFMT("%s: NOT IN TASK CONTEXT"), __func__);  
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : Invalid HCA handle"), __func__);  
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("%s: ERROR : no device registered"), __func__);  
        return HH_EAGAIN;
    }

    if ((thh_hob_p->thh_state & THH_STATE_FATAL_HCA_HALTED) == 0) {
        MTL_ERROR1(MT_FLFMT("%s: HCA is not halted (state 0x%x)"), __func__, thh_hob_p->thh_state);  
        return HH_ERR;
    }

    THH_hob_get_init_params(thh_hob_p, &hw_props, &hca_seq_num, &mod_flags);

    /* PCI reset is done in destroy, if have catastrophic error */
    rc = THH_hob_destroy(hca_hndl);
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("%s: cannot destroy old HOB (ret=%d)"), __func__, rc);  
    }

    rc = THH_hob_create(&hw_props,hca_seq_num,&mod_flags,&new_hh_hca_hndl);
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("%s: cannot create new HOB (ret=%d)"), __func__, rc);  
    }
    return rc;
}

/*****************************************************************************
******************************************************************************
************** PASS-THROUGH FUNCTIONS  ********************************** ****
******************************************************************************
*****************************************************************************/


/******************************************************************************
 *  Function:     THH_hob_alloc_ul_res <==> THH_uldm_alloc_ul_res
 *****************************************************************************/
HH_ret_t THH_hob_alloc_ul_res(HH_hca_hndl_t      hca_hndl,
                                 MOSAL_protection_ctx_t prot_ctx,
                                 void                   *hca_ul_resources_p)
{
  THH_hca_ul_resources_t*  res = (THH_hca_ul_resources_t *)hca_ul_resources_p;
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_alloc_ul_res: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_alloc_ul_res : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  /* Need to see that uldm object has been allocated.  Then, need to invoke
   * the alloc_ul_resources method of the uldm here. 
   * NOTE: may want the constructor to already pre-allocate the ul resources based upon
   *       configuration info obtained via query.
   */
  memset(res, 0, sizeof(THH_hca_ul_resources_t));

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_alloc_ul_res: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->uldm == (THH_uldm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_alloc_ul_res: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  /* Set THH_hob's information in given hca_ul_res_p buffer */
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->hh_hca_hndl= hca_hndl;
  memcpy(&(((THH_hca_ul_resources_t*)hca_ul_resources_p)->version),
    &(thh_hob_p->version_info),sizeof(THH_ver_info_t));
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->priv_ud_av = thh_hob_p->profile.use_priv_udav;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->log2_mpt_size = (u_int32_t)thh_hob_p->profile.log2_max_mpt_entries;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->max_qp_ous_wr= thh_hob_p->hca_capabilities.max_qp_ous_wr;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->max_srq_ous_wr= thh_hob_p->hca_capabilities.max_wqe_per_srq;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->max_num_sg_ent= thh_hob_p->hca_capabilities.max_num_sg_ent;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->max_num_sg_ent_srq= thh_hob_p->hca_capabilities.max_srq_sentries;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->max_num_sg_ent_rd= thh_hob_p->hca_capabilities.max_num_sg_ent_rd;
  ((THH_hca_ul_resources_t*)hca_ul_resources_p)->max_num_ent_cq= thh_hob_p->hca_capabilities.max_num_ent_cq;

  /* Invoke THH_uldm in order to get a UAR resource */
  return THH_uldm_alloc_ul_res(thh_hob_p->uldm, prot_ctx,  
		  (THH_hca_ul_resources_t *)hca_ul_resources_p);
} /* THH_alloc_ul_resources */

/******************************************************************************
 *  Function:     THH_hob_free_ul_res <==> THH_uldm_free_ul_res
 *****************************************************************************/
HH_ret_t THH_hob_free_ul_res(HH_hca_hndl_t  hca_hndl,
                                    void           *hca_ul_resources_p)
{
    THH_hca_ul_resources_t*  res = (THH_hca_ul_resources_t *)hca_ul_resources_p;
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_hob_free_ul_res: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_free_ul_res : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    /* Need to see that uldm object has been allocated.  Then, need to invoke
     * the alloc_ul_resources method of the uldm here. 
     * NOTE: may want the constructor to already pre-allocate the ul resources based upon
     *       configuration info obtained via query.
     */

    if (thh_hob_p == NULL) {
        MTL_ERROR1("THH_hob_free_ul_res: ERROR : No device registered\n");
        return HH_EAGAIN;
    }

    if (thh_hob_p->uldm == (THH_uldm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_free_ul_res: ERROR : HCA device has not yet been opened\n");
        return HH_EAGAIN;
    }

    return THH_uldm_free_ul_res(thh_hob_p->uldm, res);
} /* THH_free_ul_resources */




/******************************************************************************
 *  Function:     THH_hob_alloc_pd <==> THH_uldm_alloc_pd
 *****************************************************************************/
HH_ret_t THH_hob_alloc_pd(HH_hca_hndl_t hca_hndl, 
                             MOSAL_protection_ctx_t prot_ctx, 
                             void * pd_ul_resources_p,
                             HH_pd_hndl_t *pd_num_p)
{
  THH_hob_t thh_hob_p;
  HH_ret_t  ret;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_alloc_pd: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_alloc_pd : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_alloc_pd: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->uldm == (THH_uldm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_alloc_pd: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  ret = THH_uldm_alloc_pd(thh_hob_p->uldm, prot_ctx, 
		  (THH_pd_ul_resources_t *)pd_ul_resources_p, pd_num_p);
//  MTL_DEBUG4("THH_hob_alloc_pd: ret = %d\n", ret);
  return ret;
}

/******************************************************************************
 *  Function:     THH_hob_free_pd <==> THH_uldm_free_pd
 *****************************************************************************/
HH_ret_t THH_hob_free_pd(HH_hca_hndl_t hca_hndl, HH_pd_hndl_t pd_num)
{
  u_int32_t   max_pd;
  THH_hob_t   thh_hob_p;
  HH_ret_t    rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_free_pd: NOT IN TASK CONTEXT)\n");
      return HH_ERR; 
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_free_pd : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL; 
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_free_pd: ERROR : No device registered\n");
      return HH_EAGAIN;
  }

  max_pd = (1 << thh_hob_p->dev_lims.log_max_pd);
  if (pd_num > max_pd  - 1) {
    MTL_ERROR1("THH_hob_free_pd: ERROR : PD number (%d) is greater than max allowed (%d)\n", pd_num, max_pd);
    return HH_EAGAIN;
  }

  if (thh_hob_p->uldm == (THH_uldm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_free_pd: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }
  
  rc = THH_uldm_free_pd(thh_hob_p->uldm, pd_num);
  return rc; 
}


/******************************************************************************
 *  Function:     THH_hob_alloc_rdd <==> THH_eecm_alloc_rdd
 *****************************************************************************/
HH_ret_t THH_hob_alloc_rdd(HH_hca_dev_t *hh_dev_p, 
                                 HH_rdd_hndl_t *rdd_p)
{
#if 0
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_alloc_rdd: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_alloc_rdd : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_alloc_rdd: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->eecm == (THH_eecm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_alloc_rdd: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_eecm_alloc_rdd(thh_hob_p->uldm, rdd_p);
#else
  return HH_ENOSYS;
#endif
}

/******************************************************************************
 *  Function:     THH_hob_free_rdd <==> THH_eecm_free_rdd
 *****************************************************************************/
HH_ret_t THH_hob_free_rdd(HH_hca_dev_t *hh_dev_p, HH_rdd_hndl_t rdd)
{
#if 0
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_free_rdd: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_free_rdd : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_free_rdd: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->eecm == (THH_eecm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_free_rdd: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_eecm_free_rdd(thh_hob_p->uldm, rdd);
  return rc;
#else
  return HH_ENOSYS;
#endif
}


/******************************************************************************
 *  Function:     THH_hob_create_ud_av <==> THH_udavm_create_av
 *****************************************************************************/
HH_ret_t THH_hob_create_ud_av(HH_hca_hndl_t  hca_hndl,
                                     HH_pd_hndl_t    pd,
                                     VAPI_ud_av_t    *av_p, 
                                     HH_ud_av_hndl_t *ah_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_create_ud_av: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_create_ud_av : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_create_ud_av: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->udavm == (THH_udavm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_create_ud_av: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_udavm_create_av(thh_hob_p->udavm, pd, av_p, ah_p);
}


/******************************************************************************
 *  Function:     THH_hob_modify_ud_av <==> THH_udavm_modify_av
 *****************************************************************************/
HH_ret_t THH_hob_modify_ud_av(HH_hca_hndl_t  hca_hndl, 
                                     HH_ud_av_hndl_t ah,
                                     VAPI_ud_av_t    *av_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_modify_ud_av: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_modify_ud_av : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_modify_ud_av: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->udavm == (THH_udavm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_modify_ud_av: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_udavm_modify_av(thh_hob_p->udavm, ah, av_p);
}


/******************************************************************************
 *  Function:     THH_hob_query_ud_av <==> THH_udavm_query_av
 *****************************************************************************/
HH_ret_t THH_hob_query_ud_av(HH_hca_hndl_t  hca_hndl, 
                                    HH_ud_av_hndl_t ah,
                                    VAPI_ud_av_t    *av_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_query_ud_av: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_query_ud_av : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_query_ud_av: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->udavm == (THH_udavm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_query_ud_av: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_udavm_query_av(thh_hob_p->udavm, ah, av_p);
}


/******************************************************************************
 *  Function:     THH_hob_destroy_ud_av <==> THH_udavm_destroy_av
 *****************************************************************************/
HH_ret_t THH_hob_destroy_ud_av(HH_hca_hndl_t  hca_hndl, 
                                    HH_ud_av_hndl_t   ah)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_destroy_ud_av: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_destroy_ud_av : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_destroy_ud_av: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->udavm == (THH_udavm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_destroy_ud_av: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }
  rc = THH_udavm_destroy_av(thh_hob_p->udavm, ah);
  return rc;
}



/******************************************************************************
 *  Function:     THH_hob_register_mr <==> THH_mrwm_register_mr
 *****************************************************************************/
HH_ret_t THH_hob_register_mr(HH_hca_hndl_t  hca_hndl, 
                                    HH_mr_t       *mr_props_p, 
                                    VAPI_lkey_t   *lkey_p, 
                                    IB_rkey_t     *rkey_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_register_mr: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_register_mr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_register_mr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_register_mr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_register_mr(thh_hob_p->mrwm, mr_props_p, lkey_p, rkey_p);
}



/******************************************************************************
 *  Function:     THH_hob_reregister_mr <==> THH_mrwm_reregister_mr
 *****************************************************************************/
HH_ret_t THH_hob_reregister_mr(HH_hca_hndl_t  hca_hndl,
                               VAPI_lkey_t    lkey,  
                                      VAPI_mr_change_t  change_mask,
                                      HH_mr_t           *mr_props_p, 
                                      VAPI_lkey_t*       lkey_p, 
                                      IB_rkey_t         *rkey_p)
{
  THH_hob_t  thh_hob_p;


  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
 if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_reregister_mr: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_reregister_mr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_reregister_mr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_reregister_mr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_reregister_mr(thh_hob_p->mrwm, lkey, change_mask, mr_props_p,lkey_p, rkey_p);
}



/******************************************************************************
 *  Function:     THH_hob_register_smr <==> THH_mrwm_register_smr
 *****************************************************************************/
HH_ret_t THH_hob_register_smr(HH_hca_hndl_t  hca_hndl, 
                                    HH_smr_t       *mr_props_p, 
                                    VAPI_lkey_t    *lkey_p, 
                                    IB_rkey_t      *rkey_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_register_smr: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_register_smr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_register_smr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_register_smr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_register_smr(thh_hob_p->mrwm, mr_props_p, lkey_p, rkey_p);
}


/******************************************************************************
 *  Function:     THH_hob_query_mr <==> THH_mrwm_query_mr
 *****************************************************************************/
HH_ret_t THH_hob_query_mr(HH_hca_hndl_t  hca_hndl, 
                                 VAPI_lkey_t      lkey, 
                                 HH_mr_info_t     *mr_info_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_query_mr: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_query_mr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_query_mr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_query_mr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_query_mr(thh_hob_p->mrwm, lkey, mr_info_p);
}



/******************************************************************************
 *  Function:     THH_hob_deregister_mr <==> THH_mrwm_deregister_mr
 *****************************************************************************/
HH_ret_t THH_hob_deregister_mr(HH_hca_hndl_t  hca_hndl, 
                                      VAPI_lkey_t      lkey)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_deregister_mr: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_deregister_mr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_deregister_mr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_deregister_mr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_mrwm_deregister_mr(thh_hob_p->mrwm, lkey);
  return rc;
}

/******************************************************************************
 *  Function:     THH_hob_alloc_mw <==> THH_mrwm_alloc_mw
 *****************************************************************************/
HH_ret_t THH_hob_alloc_mw(HH_hca_hndl_t  hca_hndl, 
                                 HH_pd_hndl_t     pd,
                                 IB_rkey_t        *initial_rkey_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_alloc_mw: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_alloc_mw : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }

  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_alloc_mw: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_alloc_mw: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_alloc_mw(thh_hob_p->mrwm, pd, initial_rkey_p);
}


/******************************************************************************
 *  Function:     THH_hob_query_mw <==> THH_mrwm_query_mw
 *****************************************************************************/
HH_ret_t THH_hob_query_mw(HH_hca_hndl_t  hca_hndl,
                                 IB_rkey_t        initial_rkey,
                                 IB_rkey_t        *current_rkey_p,
                                 HH_pd_hndl_t     *pd_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_query_mw: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_query_mw : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_query_mw: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_query_mw: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  MTL_TRACE1("%s: -KL- called for key 0x%x", __func__,initial_rkey);
  return THH_mrwm_query_mw(thh_hob_p->mrwm, initial_rkey, current_rkey_p, pd_p);
}


/******************************************************************************
 *  Function:     THH_hob_free_mw <==> THH_mrwm_free_mw
 *****************************************************************************/
HH_ret_t THH_hob_free_mw(HH_hca_hndl_t  hca_hndl, 
                                IB_rkey_t        initial_rkey)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_free_mw: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_free_mw : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_free_mw: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_free_mw: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_mrwm_free_mw(thh_hob_p->mrwm, initial_rkey);
  return rc;
}


  /* Fast Memory Regions */
  /***********************/
/******************************************************************************
 *  Function:     THH_hob_create_cq <==> THH_mrwm_alloc_fmr
 *****************************************************************************/
HH_ret_t  THH_hob_alloc_fmr(HH_hca_hndl_t  hca_hndl,
                            HH_pd_hndl_t   pd,
                            VAPI_mrw_acl_t acl, 
                            MT_size_t      max_pages,      /* Maximum number of pages that can be mapped using this region */
                            u_int8_t       log2_page_sz,	 /* Fixed page size for all maps on a given FMR */
                            VAPI_lkey_t*   last_lkey_p)    /* To be used as the initial FMR handle */
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_alloc_fmr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_alloc_fmr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_alloc_fmr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  if (thh_hob_p->ddr_props.dh == TRUE) {
      /* Must hide DDR memory. alloc fmr not supported */
      MTL_ERROR1("THH_hob_alloc_fmr: Device is operating in HIDE_DDR mode.  Cannot alloc fmr\n");
      return HH_ENOSYS;
  }

  return THH_mrwm_alloc_fmr(thh_hob_p->mrwm,pd,acl,max_pages,log2_page_sz,last_lkey_p);
}

/******************************************************************************
 *  Function:     THH_hob_map_fmr <==> THH_mrwm_map_fmr
 *****************************************************************************/
HH_ret_t  THH_hob_map_fmr(HH_hca_hndl_t  hca_hndl,
                        VAPI_lkey_t      last_lkey,
                        EVAPI_fmr_map_t* map_p,
                        VAPI_lkey_t*     lkey_p,
                        IB_rkey_t*       rkey_p)

{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_map_fmr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_map_fmr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_map_fmr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_map_fmr(thh_hob_p->mrwm,last_lkey,map_p,lkey_p,rkey_p);
}
  
/******************************************************************************
 *  Function:     THH_hob_unmap_fmr <==> THH_mrwm_unmap_fmr
 *****************************************************************************/
HH_ret_t  THH_hob_unmap_fmr(HH_hca_hndl_t hca_hndl,
                          u_int32_t     num_of_fmrs_to_unmap,
                          VAPI_lkey_t*  last_lkeys_array)

{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_unmap_fmr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_unmap_fmr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_unmap_fmr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mrwm_unmap_fmr(thh_hob_p->mrwm,num_of_fmrs_to_unmap,last_lkeys_array);
}

/******************************************************************************
 *  Function:     THH_hob_free_fmr <==> THH_mrwm_free_fmr
 *****************************************************************************/
HH_ret_t  THH_hob_free_fmr(HH_hca_hndl_t  hca_hndl,
                           VAPI_lkey_t      last_lkey)   /* as returned on last successful mapping operation */

{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_free_fmr : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_free_fmr: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->mrwm == (THH_mrwm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_free_fmr: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_mrwm_free_fmr(thh_hob_p->mrwm,last_lkey);
  return rc;
}

  
  /******************************************************************************
 *  Function:     THH_hob_create_cq <==> THH_cqm_create_cq
 *****************************************************************************/
HH_ret_t THH_hob_create_cq(HH_hca_hndl_t  hca_hndl, 
                                  MOSAL_protection_ctx_t  user_prot_context, 
                                  void                    *cq_ul_resources_p,
                                  HH_cq_hndl_t            *cq_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_create_cq: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_create_cq : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_create_cq: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->cqm == (THH_cqm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_create_cq: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_cqm_create_cq(thh_hob_p->cqm, user_prot_context, thh_hob_p->compl_eq,
                           thh_hob_p->ib_eq, 
                           (THH_cq_ul_resources_t*)cq_ul_resources_p, cq_p);
}

/******************************************************************************
 *  Function:     THH_hob_resize_cq <==> THH_cqm_modify_cq
 *****************************************************************************/
HH_ret_t THH_hob_resize_cq(HH_hca_hndl_t  hca_hndl, 
                           HH_cq_hndl_t            cq,
                           void                    *cq_ul_resources_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_resize_cq: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_resize_cq : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_resize_cq: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->cqm == (THH_cqm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_resize_cq: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_cqm_resize_cq(thh_hob_p->cqm, cq, (THH_cq_ul_resources_t*)cq_ul_resources_p);
}

/******************************************************************************
 *  Function:     THH_hob_query_cq <==> THH_cqm_query_cq
 *****************************************************************************/
HH_ret_t THH_hob_query_cq(HH_hca_hndl_t  hca_hndl, 
                                 HH_cq_hndl_t            cq,
                                 VAPI_cqe_num_t          *num_o_cqes_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_query_cq: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_query_cq : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_query_cq: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->cqm == (THH_cqm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_query_cq: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_cqm_query_cq(thh_hob_p->cqm, cq, num_o_cqes_p);
}


/******************************************************************************
 *  Function:     THH_hob_destroy_cq <==> THH_cqm_destroy_cq
 *****************************************************************************/
HH_ret_t THH_hob_destroy_cq(HH_hca_hndl_t  hca_hndl, 
                                 HH_cq_hndl_t    cq)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_destroy_cq: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_destroy_cq : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_destroy_cq: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->cqm == (THH_cqm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_destroy_cq: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_cqm_destroy_cq(thh_hob_p->cqm, cq);
  return rc;
}


/******************************************************************************
 *  Function:     THH_hob_create_qp <==> THH_qpm_create_qp
 *****************************************************************************/
HH_ret_t THH_hob_create_qp(HH_hca_hndl_t  hca_hndl, 
                           HH_qp_init_attr_t  *init_attr_p, 
                           void               *qp_ul_resources_p, 
                           IB_wqpn_t          *qpn_p)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_create_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_create_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_create_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_create_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  if ((rc=THH_hob_check_qp_init_attrs(thh_hob_p,init_attr_p,FALSE)) != HH_OK) {
      MTL_ERROR1("THH_hob_create_qp: ERROR : requested capabilities exceed HCA limits\n");
      return rc;
  }

  return THH_qpm_create_qp(thh_hob_p->qpm, init_attr_p, 0, (THH_qp_ul_resources_t*)qp_ul_resources_p, qpn_p);
}

/******************************************************************************
 *  Function:     THH_hob_get_special_qp <==> THH_qpm_get_special_qp
 *****************************************************************************/
HH_ret_t THH_hob_get_special_qp(HH_hca_hndl_t  hca_hndl,
                                VAPI_special_qp_t  qp_type,
                                IB_port_t          port, 
                                HH_qp_init_attr_t  *init_attr_p, 
                                void               *qp_ul_resources_p, 
                                IB_wqpn_t          *sqp_hndl_p)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_get_special_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_get_special_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_get_special_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_get_special_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  if ((rc=THH_hob_check_qp_init_attrs(thh_hob_p,init_attr_p,TRUE)) != HH_OK) {
      MTL_ERROR1("THH_hob_get_special_qp: ERROR : requested capabilities exceed HCA limits\n");
      return rc;
  }

  return THH_qpm_get_special_qp(thh_hob_p->qpm, qp_type, port, init_attr_p, (THH_qp_ul_resources_t*)qp_ul_resources_p, sqp_hndl_p);
}

/******************************************************************************
 *  Function:     THH_hob_modify_qp <==> THH_qpm_modify_qp
 *****************************************************************************/
HH_ret_t THH_hob_modify_qp(HH_hca_hndl_t  hca_hndl, 
                                  IB_wqpn_t            qpn, 
                                  VAPI_qp_state_t      cur_qp_state,
                                  VAPI_qp_attr_t       *qp_attr_p,
                                  VAPI_qp_attr_mask_t  *qp_attr_mask_p)
{
  HH_ret_t	 ret;
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_modify_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_modify_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_modify_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_modify_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }
  
  ret = THH_qpm_modify_qp(thh_hob_p->qpm, qpn, cur_qp_state, qp_attr_p, qp_attr_mask_p);

  return ret;
}


/******************************************************************************
 *  Function:     THH_hob_query_qp <==> THH_qpm_query_qp
 *****************************************************************************/
HH_ret_t THH_hob_query_qp(HH_hca_hndl_t  hca_hndl, 
                                 IB_wqpn_t          qpn, 
                                 VAPI_qp_attr_t     *qp_attr_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_query_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_query_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_query_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_query_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_qpm_query_qp(thh_hob_p->qpm, qpn, qp_attr_p);
}


/******************************************************************************
 *  Function:     THH_hob_destroy_qp <==> THH_qpm_destroy_qp
 *****************************************************************************/
HH_ret_t THH_hob_destroy_qp(HH_hca_hndl_t  hca_hndl, 
                                   IB_wqpn_t          qpn)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_destroy_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_destroy_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_destroy_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_destroy_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_qpm_destroy_qp(thh_hob_p->qpm, qpn);
  return rc;
}

/* HH_create_srq */
HH_ret_t THH_hob_create_srq(HH_hca_hndl_t hca_hndl, HH_pd_hndl_t pd, void *srq_ul_resources_p, 
                            HH_srq_hndl_t     *srq_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("%s: NOT IN TASK CONTEXT)\n", __func__);
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("%s : ERROR : Invalid HCA handle\n", __func__);
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("%s: ERROR : No device registered\n", __func__);
      return HH_EINVAL_HCA_HNDL;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->srqm == (THH_srqm_t)THH_INVALID_HNDL) {
    MTL_ERROR1("%s: SRQs are not supported in this HCA configuration\n", __func__);
    return HH_ENOSYS;
  }

  return THH_srqm_create_srq(thh_hob_p->srqm, pd, srq_ul_resources_p, srq_p);
}

HH_ret_t THH_hob_query_srq(HH_hca_hndl_t hca_hndl, HH_srq_hndl_t srq, u_int32_t *limit_p)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("%s: NOT IN TASK CONTEXT)\n", __func__);
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("%s : ERROR : Invalid HCA handle\n", __func__);
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("%s: ERROR : No device registered\n", __func__);
      return HH_EINVAL_HCA_HNDL;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->srqm == (THH_srqm_t)THH_INVALID_HNDL) {
    MTL_ERROR1("%s: SRQs are not supported in this HCA configuration", __func__);
    return HH_ENOSYS;
  }

  return THH_srqm_query_srq(thh_hob_p->srqm, srq, limit_p);
}

HH_ret_t THH_hob_destroy_srq(HH_hca_hndl_t hca_hndl, HH_srq_hndl_t srq)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("%s: NOT IN TASK CONTEXT)\n", __func__);
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("%s : ERROR : Invalid HCA handle\n", __func__);
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("%s: ERROR : No device registered\n", __func__);
      return HH_EINVAL_HCA_HNDL;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->srqm == (THH_srqm_t)THH_INVALID_HNDL) {
    MTL_ERROR1("%s: SRQs are not supported in this HCA configuration", __func__);
    return HH_ENOSYS;
  }

  return THH_srqm_destroy_srq(thh_hob_p->srqm, srq);
}


/******************************************************************************
 *  Function:     THH_hob_process_local_mad <==> THH_qpm_process_local_mad
 *****************************************************************************/
HH_ret_t THH_hob_process_local_mad(
                          HH_hca_hndl_t        hca_hndl,
                          IB_port_t            port,
                          IB_lid_t             slid, /* For Mkey violation trap */
                          EVAPI_proc_mad_opt_t proc_mad_opts,
                          void *               mad_in_p,
                          void *               mad_out_p)
{
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_hob_process_local_mad: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_hob_process_local_mad : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
        MTL_ERROR1("THH_hob_process_local_mad: ERROR : No device registered\n");
        return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);


    if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
        MTL_ERROR1("THH_hob_destroy_qp: ERROR : HCA device has not yet been opened\n");
        return HH_EAGAIN;
    }

    return THH_qpm_process_local_mad(thh_hob_p->qpm,port,slid,proc_mad_opts,mad_in_p,mad_out_p);
}
    

/******************************************************************************
 *  Function:     THH_hob_ddrmm_alloc <==> THH_ddrmm_alloc
 *****************************************************************************/
HH_ret_t THH_hob_ddrmm_alloc(
                            HH_hca_hndl_t  hca_hndl,
                            VAPI_size_t     size, 
                            u_int8_t      align_shift,
                            VAPI_phy_addr_t*  buf_p)
{
    THH_hob_t  thh_hob_p;
    MT_phys_addr_t adrs;
    HH_ret_t ret = HH_OK;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1(MT_FLFMT(" NOT IN TASK CONTEXT\n"));
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1(MT_FLFMT("ERROR : Invalid HCA handle\n"));
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("ERROR : No device registered\n"));
        return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);


    if (thh_hob_p->ddrmm == (THH_ddrmm_t)THH_INVALID_HNDL) {
        MTL_ERROR1(MT_FLFMT("ERROR : HCA device has not yet been opened\n"));
        return HH_EAGAIN;
    }

    if (thh_hob_p->ddr_props.dh == TRUE) {
        /* Must hide DDR memory. alloc fmr not supported */
        MTL_ERROR1(MT_FLFMT("%s: Device is operating in HIDE_DDR mode.  Cannot alloc ddr memory"), __func__);
        return HH_ENOSYS;
    }

    MTL_DEBUG1(MT_FLFMT("before THH_ddrmm_alloc \n"));
    /* tavor ALWAYS has DDR, on other devices we should query if ther's DDR */
    ret = THH_ddrmm_alloc(thh_hob_p->ddrmm,size,align_shift,&adrs);
    MTL_DEBUG1(MT_FLFMT("after THH_ddrmm_alloc \n"));
    *buf_p = (VAPI_phy_addr_t)adrs;
    return ret;
    
}

/******************************************************************************
 *  Function:     THH_hob_ddrmm_query <==> THH_ddrmm_query
 *****************************************************************************/
HH_ret_t THH_hob_ddrmm_query(
                            HH_hca_hndl_t  hca_hndl,
                            u_int8_t      align_shift,   
                            VAPI_size_t*    total_mem,    
                            VAPI_size_t*    free_mem,     
                            VAPI_size_t*    largest_chunk,  
                            VAPI_phy_addr_t*  largest_free_addr_p)
{
    THH_hob_t  thh_hob_p;
        
    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1(MT_FLFMT(" NOT IN TASK CONTEXT\n"));
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1(MT_FLFMT("ERROR : Invalid HCA handle\n"));
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("ERROR : No device registered\n"));
        return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);


    if (thh_hob_p->ddrmm == (THH_ddrmm_t)THH_INVALID_HNDL) {
        MTL_ERROR1(MT_FLFMT("ERROR : HCA device has not yet been opened\n"));
        return HH_EAGAIN;
    }

    if (thh_hob_p->ddr_props.dh == TRUE) {
        /* Must hide DDR memory. alloc fmr not supported */
        MTL_ERROR1(MT_FLFMT("%s: Device is operating in HIDE_DDR mode.  Cannot query ddr memory"), __func__);
        return HH_ENOSYS;
    }

    MTL_DEBUG1(MT_FLFMT("before THH_ddrmm_query \n"));
    /* tavor ALWAYS has DDR, on other devices we should query if ther's DDR */
    return THH_ddrmm_query(thh_hob_p->ddrmm,align_shift,total_mem,free_mem,largest_chunk,largest_free_addr_p);
    
}


/******************************************************************************
 *  Function:     THH_hob_ddrmm_free <==> THH_ddrmm_free
 *****************************************************************************/
HH_ret_t THH_hob_ddrmm_free(
                            HH_hca_hndl_t  hca_hndl,
                            VAPI_phy_addr_t  buf,
                            VAPI_size_t     size)
                            
{
    THH_hob_t  thh_hob_p;
    HH_ret_t   rc = HH_OK;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1(MT_FLFMT(" NOT IN TASK CONTEXT\n"));
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1(MT_FLFMT("ERROR : Invalid HCA handle\n"));
        return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
        MTL_ERROR1(MT_FLFMT("ERROR : No device registered\n"));
        return HH_EAGAIN;
    }


    if (thh_hob_p->ddrmm == (THH_ddrmm_t)THH_INVALID_HNDL) {
        MTL_ERROR1(MT_FLFMT("ERROR : HCA device has not yet been opened\n"));
        return HH_EAGAIN;
    }

    rc = THH_ddrmm_free(thh_hob_p->ddrmm,buf,size);
    return rc;
}








/******************************************************************************
 *  Function:     THH_hob_get_qp1_pkey <==> THH_qpm_get_qp1_pkey
 *****************************************************************************/
HH_ret_t THH_hob_get_qp1_pkey(
                          HH_hca_hndl_t  hca_hndl,
                          IB_port_t  port,/*IN */
                          VAPI_pkey_t* pkey_p/*OUT*/)

{
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hca_hndl == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);

    if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1(MT_FLFMT("HCA %s has not yet been opened"),thh_hob_p->dev_name);
      return HH_EAGAIN;
    }

    return THH_qpm_get_qp1_pkey(thh_hob_p->qpm,port,pkey_p);
}

HH_ret_t THH_hob_get_pkey(
                          HH_hca_hndl_t  hca_hndl,
                          IB_port_t  port,/*IN */
                          VAPI_pkey_ix_t pkey_index, /*IN*/
                          VAPI_pkey_t* pkey_p/*OUT*/)
{
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hca_hndl == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);

    if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1(MT_FLFMT("HCA %s has not yet been opened"),thh_hob_p->dev_name);
      return HH_EAGAIN;
    }

    return THH_qpm_get_pkey(thh_hob_p->qpm,port,pkey_index,pkey_p);
}

/******************************************************************************
 *  Function:     THH_hob_get_sgid <==> THH_qpm_get_sgid
 *****************************************************************************/
HH_ret_t THH_hob_get_sgid(
                          HH_hca_hndl_t  hca_hndl,
                          IB_port_t  port,/*IN */
                          u_int8_t index,/*IN*/
                          IB_gid_t* gid_p/*OUT*/)

{
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hca_hndl == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);

    if (thh_hob_p == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);

    if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1(MT_FLFMT("HCA %s has not yet been opened"),thh_hob_p->dev_name);
      return HH_EAGAIN;
    }

    return THH_qpm_get_sgid(thh_hob_p->qpm,port,index,gid_p);
}

HH_ret_t THH_hob_get_legacy_mode(THH_hob_t thh_hob_p,MT_bool *p_mode)
{
    if (thh_hob_p == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EAGAIN;
    }

	*p_mode = thh_hob_p->module_flags.legacy_sqp;

	return HH_OK;
}


 /******************************************************************************
 *  Function:     THH_hob_create_eec <==> THH_eecm_create_eec
 *****************************************************************************/
HH_ret_t THH_hob_create_eec(HH_hca_hndl_t  hca_hndl, 
                                   HH_rdd_hndl_t  rdd, 
                                   IB_eecn_t      *eecn_p)
{
#if 0
  THH_hob_t  thh_hob_p;

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_create_eec: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_create_eec : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_create_eec: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->eecm == (THH_eecm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_create_eec: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_eecm_create_eec(thh_hob_p->eecm, rdd, eecn_p);
#else
  return HH_ENOSYS;
#endif
}

/******************************************************************************
 *  Function:     THH_hob_modify_eec <==> THH_eecm_modify_eec
 *****************************************************************************/
HH_ret_t THH_hob_modify_eec(HH_hca_hndl_t  hca_hndl,
                                   IB_eecn_t           eecn, 
                                   VAPI_qp_state_t     cur_ee_state, 
                                   VAPI_qp_attr_t      *ee_attr_p, 
                                   VAPI_qp_attr_mask_t *ee_attr_mask_p)
{
#if 0
  THH_hob_t  thh_hob_p;

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_modify_eec: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_modify_eec : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_modify_eec: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->eecm == (THH_eecm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_modify_eec: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_eecm_modify_eec(thh_hob_p->eecm, eecn, cur_ee_state, ee_attr_p, ee_attr_mask_p);
#else
  return HH_ENOSYS;
#endif
}



/******************************************************************************
 *  Function:     THH_hob_query_eec <==> THH_eecm_query_eec
 *****************************************************************************/
HH_ret_t THH_hob_query_eec(HH_hca_hndl_t  hca_hndl,
                                   IB_eecn_t        eecn, 
                                   VAPI_qp_attr_t   *ee_attr_p)
{
#if 0
  THH_hob_t  thh_hob_p;

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_query_eec: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_query_eec : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_query_eec: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->eecm == (THH_eecm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_query_eec: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_eecm_query_eec(thh_hob_p->eecm, eecn, ee_attr_p);
#else
  return HH_ENOSYS;
#endif
}



/******************************************************************************
 *  Function:     THH_hob_destroy_eec <==> THH_eecm_destroy_eec
 *****************************************************************************/
HH_ret_t THH_hob_destroy_eec(HH_hca_hndl_t  hca_hndl,
                                    IB_eecn_t      eecn)
{
#if 0
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_destroy_eec: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_destroy_eec : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_destroy_eec: ERROR : No device registered\n");
      return HH_EAGAIN;
  }


  if (thh_hob_p->eecm == (THH_eecm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_destroy_eec: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_eecm_destroy_eec(thh_hob_p->eecm, eecn);
  return rc;
#else
  return HH_ENOSYS;
#endif
}



/******************************************************************************
 *  Function:     THH_hob_attach_to_multicast <==> THH_mcgm_attach_qp
 *****************************************************************************/
HH_ret_t THH_hob_attach_to_multicast(
                          HH_hca_hndl_t  hca_hndl, 
                          IB_wqpn_t      qpn,
                          IB_gid_t       dgid)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_attach_to_multicast: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_attach_to_multicast : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_attach_to_multicast: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);

  if (thh_hob_p->mcgm == (THH_mcgm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_attach_to_multicast: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_mcgm_attach_qp(thh_hob_p->mcgm, qpn, dgid);
}


/******************************************************************************
 *  Function:     THH_hob_detach_from_multicast <==> THH_mcgm_detach_qp
 *****************************************************************************/
HH_ret_t THH_hob_detach_from_multicast(
                          HH_hca_hndl_t  hca_hndl, 
                          IB_wqpn_t      qpn,
                          IB_gid_t       dgid)
{
  THH_hob_t  thh_hob_p;
  HH_ret_t   rc = HH_OK;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_detach_from_multicast: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_detach_from_multicast : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_detach_from_multicast: ERROR : No device registered\n");
      return HH_EAGAIN;
  }

  if (thh_hob_p->mcgm == (THH_mcgm_t)THH_INVALID_HNDL) {
      MTL_ERROR1( "THH_hob_detach_from_multicast: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  rc = THH_mcgm_detach_qp(thh_hob_p->mcgm, qpn, dgid);
  return rc;
}

VIP_delay_unlock_t THH_hob_get_delay_unlock(THH_hob_t hob)
{ 
    if (hob == NULL) {
        return NULL;
    } else {
        return (hob->delay_unlocks);
    }
}

HH_ret_t THH_get_debug_info(
	HH_hca_hndl_t hca_hndl, 		/*IN*/
	THH_debug_info_t *debug_info_p	/*OUT*/
)
{
    THH_hob_t  hob_p;
    HH_ret_t   rc = HH_OK;
    MT_bool    have_error = FALSE;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
        MTL_ERROR1("THH_get_debug_info: NOT IN TASK CONTEXT)\n");
        return HH_ERR;
    }

    if (hca_hndl == NULL) {
        MTL_ERROR1("THH_get_debug_info : ERROR : Invalid HCA handle\n");
        return HH_EINVAL_HCA_HNDL;
    }
    hob_p = THHOBP(hca_hndl);

    if (hob_p == NULL) {
        MTL_ERROR1("THH_get_debug_info: ERROR : No device registered\n");
        return HH_EAGAIN;
    }

    memset(debug_info_p, 0, sizeof(THH_debug_info_t));
    
    memcpy(&(debug_info_p->hw_props), &(hob_p->hw_props), sizeof(THH_hw_props_t));
    memcpy(&(debug_info_p->profile), &(hob_p->profile), sizeof(THH_profile_t));
    memcpy(&(debug_info_p->ddr_addr_vec), &(hob_p->ddr_alloc_base_addrs_vec), 
           sizeof(THH_ddr_base_addr_vector_t));
    memcpy(&(debug_info_p->ddr_size_vec), &(hob_p->ddr_alloc_size_vec), 
           sizeof(THH_ddr_allocation_vector_t));
    debug_info_p->num_ddr_addrs = THH_DDR_ALLOCATION_VEC_SIZE;
    memcpy(&(debug_info_p->mrwm_props), &(hob_p->mrwm_props), sizeof(THH_mrwm_props_t));

    debug_info_p->hide_ddr = hob_p->ddr_props.dh;

    rc = THH_uldm_get_num_objs(hob_p->uldm,&(debug_info_p->allocated_ul_res),&(debug_info_p->allocated_pd));
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("THH_get_debug_info:  proc THH_uldm_get_num_of_objs returned ERROR"));
        have_error = TRUE;
    }
    
    rc = THH_mrwm_get_num_objs(hob_p->mrwm,&(debug_info_p->allocated_mr_int),
                                  &(debug_info_p->allocated_mr_ext), &(debug_info_p->allocated_mw));
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("THH_get_debug_info:  proc THH_mrwm_get_num_of_objs returned ERROR"));
        have_error = TRUE;
    }

    rc = THH_qpm_get_num_qps(hob_p->qpm,&(debug_info_p->allocated_qp));
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("THH_get_debug_info:  proc THH_tqpm_get_num_of_qps returned ERROR"));
        have_error = TRUE;
    }
    
    rc = THH_cqm_get_num_cqs(hob_p->cqm,&(debug_info_p->allocated_cq));
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("THH_get_debug_info:  proc THH_tcqm_get_num_of_cqs returned ERROR"));
        have_error = TRUE;
    }
    
    rc = THH_mcgm_get_num_mcgs(hob_p->mcgm,&(debug_info_p->allocated_mcg));
    if (rc != HH_OK) {
        MTL_ERROR1(MT_FLFMT("THH_get_debug_info:  proc THH_mcgm_get_num_of_mcgs returned ERROR"));
        have_error = TRUE;
    }

    return (have_error ? HH_ERR : HH_OK);
}

/******************************************************************************
 *  Function:     THH_hob_get_num_ports
 *
 *  Description:  Gets number of physical ports configured for HCA  
 *
 *  input:
 *                hca_hndl
 *  output: 
 *                num_ports_p - 1 or 2
 *  returns:
 *                HH_OK
 *                HH_EINVAL
 *                HH_EINVAL_HCA_HNDL
 *                HH_ERR
 *
 *  Comments:     Does MAD query to get the data in real time. 
 *
 *****************************************************************************/
HH_ret_t  THH_hob_get_num_ports( HH_hca_hndl_t  hca_hndl,
                               IB_port_t      *num_ports_p)
{
    THH_hob_t  thh_hob_p;

    MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
    if (hca_hndl == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EINVAL_HCA_HNDL;
    }
    thh_hob_p = THHOBP(hca_hndl);
    
    if (thh_hob_p == NULL) {
      MTL_ERROR1(MT_FLFMT("Invalid HCA handle"));
      return HH_EAGAIN;
    }
    TEST_RETURN_FATAL(thh_hob_p);
    *num_ports_p = (IB_port_t)(thh_hob_p->dev_lims.num_ports);
    return HH_OK;
}


#if defined(MT_SUSPEND_QP)
/******************************************************************************
 *  Function:     THH_hob_suspend_qp <==> THH_qpm_suspend_qp
 *****************************************************************************/
HH_ret_t THH_hob_suspend_qp(HH_hca_hndl_t  hca_hndl, 
                            IB_wqpn_t      qpn, 
                            MT_bool        suspend_flag)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_suspend_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_suspend_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_suspend_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->qpm == (THH_qpm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_suspend_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_qpm_suspend_qp(thh_hob_p->qpm, qpn, suspend_flag);
}
/******************************************************************************
 *  Function:     THH_hob_suspend_cq <==> THH_qpm_suspend_cq
 *****************************************************************************/
HH_ret_t THH_hob_suspend_cq(HH_hca_hndl_t  hca_hndl, 
                            HH_cq_hndl_t   cq, 
                            MT_bool        do_suspend)
{
  THH_hob_t  thh_hob_p;

  MT_RETURN_IF_LOW_STACK(THH_WATERMARK);
  if (MOSAL_get_exec_ctx() != MOSAL_IN_TASK) {
      MTL_ERROR1("THH_hob_suspend_qp: NOT IN TASK CONTEXT)\n");
      return HH_ERR;
  }

  if (hca_hndl == NULL) {
      MTL_ERROR1("THH_hob_suspend_qp : ERROR : Invalid HCA handle\n");
      return HH_EINVAL_HCA_HNDL;
  }
  thh_hob_p = THHOBP(hca_hndl);

  if (thh_hob_p == NULL) {
      MTL_ERROR1("THH_hob_suspend_qp: ERROR : No device registered\n");
      return HH_EAGAIN;
  }
  TEST_RETURN_FATAL(thh_hob_p);


  if (thh_hob_p->cqm == (THH_cqm_t)THH_INVALID_HNDL) {
      MTL_ERROR1("THH_hob_suspend_qp: ERROR : HCA device has not yet been opened\n");
      return HH_EAGAIN;
  }

  return THH_cqm_suspend_cq(thh_hob_p->cqm, cq, do_suspend);
}
#endif
