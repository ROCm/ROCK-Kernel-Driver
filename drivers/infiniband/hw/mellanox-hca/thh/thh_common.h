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

#ifndef H_THH_COMMON_H
#define H_THH_COMMON_H


#include <mtl_common.h>
#include <vapi.h>
#include <hh.h>
#include <tlog2.h>

/* resources reserved in driver */
#define THH_NUM_RSVD_PD     2
#define THH_NUM_RSVD_QP     8

/* this macro ensure that total buff size is power of 2 and one extra entry for the cyclic buffer */
#define THH_CYCLIC_BUFF_SIZE(entries) (1<<(floor_log2(entries)+1))

typedef u_int32_t THH_eqn_t;
#define THH_INVALID_EQN               0xFFFFFFFF

typedef struct THH_udavm_st   *THH_udavm_t; /* type to identify the udav object */
typedef  u_int32_t            THH_uar_index_t;
typedef struct THH_uar_st     *THH_uar_t;


/* VERSION INFORMATION: used in order to retrieve major version numbers 
   in order to deal with differences in different versions. */
typedef struct THH_ver_info_st {
  u_int32_t hw_ver;  /* HW version (stepping etc.)*/ 
  u_int16_t fw_ver_major;  /* Firmware major version */ 
  u_int16_t fw_ver_minor;  /* Firmware minor version */ 
  u_int16_t fw_ver_subminor;	/* Firmware Sub-minor version (Patch level).  */
  u_int16_t cmd_if_ver;  /* Command interface version */
}THH_ver_info_t;

typedef struct THH_hca_ul_resources_st {
  HH_hca_hndl_t hh_hca_hndl;
  THH_ver_info_t version;
  THH_uar_index_t uar_index;
  MT_virt_addr_t   uar_map;
  /* HCA capabilities to validate or use in THHUL */
  MT_bool         priv_ud_av; /* Privileged UD AV are enforced ? */
  u_int32_t		    log2_mpt_size;
  u_int32_t       max_qp_ous_wr;       /* Maximum Number of oustanding WR on any WQ.         */             
  u_int32_t       max_srq_ous_wr;       /* Maximum Number of oustanding WR on any WQ.         */             
  u_int32_t       max_num_sg_ent;      /* Max num of scatter/gather entries for desc other than RD */
  u_int32_t       max_num_sg_ent_srq;  /* Max num of scatter/gather entries for SRQs */
  u_int32_t       max_num_sg_ent_rd;   /* Max num of scatter/gather entries for RD desc      */
  u_int32_t       max_num_ent_cq;      /* Max num of supported entries per CQ                */
} THH_hca_ul_resources_t;

typedef struct THH_pd_ul_resources_st {
    /* if user-level udavm_buf is used (i.e., non-zero), it should be malloc'ed
     * with size = (udavm_buf_sz + (1 udav entry size)), to allow the kernel level
     * to align the start of the udavm table to the entry size -- so some spare is
     * needed.  Therefore, the udavm_buf_size value is the size of the actual udavm
     * table, not the size of the malloc'ed buffer.
     */
    MT_virt_addr_t   udavm_buf;            /* IN */
    MT_size_t     udavm_buf_sz;         /* IN */
    HH_pdm_pd_flags_t  pd_flags;      /* IN - if non-zero, is a PD for a special QP*/
    VAPI_lkey_t   udavm_buf_memkey;     /* OUT - set by THH_uldm */
} THH_pd_ul_resources_t;

typedef struct
{
  MT_virt_addr_t      cqe_buf;     /* CQE buffer virtual addr. CQE size aligned */
  MT_size_t        cqe_buf_sz;  /* Buffer size in bytes (mult of CQE size) */
  THH_uar_index_t  uar_index;   /* Index of UAR used for this CQ. */
  u_int32_t        new_producer_index;     /* New producer index after "resize_cq" (OUT)*/
} THH_cq_ul_resources_t;

typedef struct 
{
  MT_virt_addr_t      wqes_buf;     /* WQEs buffer virtual address */
  MT_size_t        wqes_buf_sz;  /* Buffer size in bytes */
  THH_uar_index_t  uar_index;    /* index of UAR used for this QP */
  /*  ER: Not used anywhere: MT_virt_addr_t      uar_map;    */ /* Address in user space of UAR */
} THH_qp_ul_resources_t;

typedef struct 
{
  MT_virt_addr_t      wqes_buf;     /* WQEs buffer virtual address */
  MT_size_t           wqes_buf_sz;  /* Buffer size in bytes */
  MT_size_t           wqe_sz;       /* WQE (descriptor) size in bytes */
  THH_uar_index_t     uar_index;    /* index of UAR used for this QP */
} THH_srq_ul_resources_t;

#endif  /* H_THH_COMMON_H */
