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

#ifndef H_THH_ULDM_PRIV_H
#define H_THH_ULDM_PRIV_H

#include <mtl_common.h>
#include <cr_types.h>
#include <MT23108_PRM.h>
#include <thh_uldm.h>
#include <tmrwm.h>
#include <tddrmm.h>
#include <udavm.h>
#include <thh_hob.h>
#include <epool.h>
#include <tlog2.h>

typedef struct __THH_uldm_epool_st {
    unsigned long              next;
    unsigned long              prev;
} __THH_uldm_epool_t;

typedef struct __THH_uldm_uar_data_st {
    MOSAL_protection_ctx_t    prot_ctx;
    MT_virt_addr_t               virt_addr;
} __THH_uldm_uar_data_t;


typedef struct __THH_uldm_pd_data_st {
    MOSAL_protection_ctx_t    prot_ctx;
    VAPI_lkey_t               lkey;    /* for freeing non-privileged UDAV table with a PD */
    MT_virt_addr_t               udav_table_ddr_virt_addr;
    MT_phys_addr_t               udav_table_ddr_phys_addr;
    MT_size_t                 udav_table_size;
} __THH_uldm_pd_data_t;

typedef struct THH_uldm_uar_entry_st {
     union {
        __THH_uldm_epool_t    epool;
        __THH_uldm_uar_data_t uar;
     } u1;
     u_int8_t               valid;
} THH_uldm_uar_entry_t;

typedef struct THH_uldm_pd_entry_st {
    union {
        __THH_uldm_epool_t    epool;
        __THH_uldm_pd_data_t  pd;
    } u1;
    u_int8_t                valid;
} THH_uldm_pd_entry_t;

typedef struct THH_uldm_st {
      THH_hob_t               hob;         /* saved during uldm create */
      MT_phys_addr_t             uar_base; 
      u_int8_t                log2_max_uar;
      u_int32_t               max_uar;
      u_int8_t                log2_uar_pg_sz;
      u_int32_t               max_pd; 
      THH_uldm_uar_entry_t    *uldm_uar_table;
      THH_uldm_pd_entry_t     *uldm_pd_table;
      EPool_t                 uar_list;
      EPool_t                 pd_list;
      EPool_meta_t            uar_meta;
      EPool_meta_t            pd_meta;
} THH_uldm_obj_t;

#endif
