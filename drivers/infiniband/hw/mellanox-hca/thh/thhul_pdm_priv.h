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

#ifndef H_THHUL_PDM_PRIV_H
#define H_THHUL_PDM_PRIV_H

#include <mtl_common.h>
#include <cr_types.h>
#include <MT23108_PRM.h>
#include <thhul_pdm.h>
#include <thh.h>
#include <udavm.h>
#include <thhul_hob.h>
#include <vip_array.h>
#include <vip_hashp2p.h>
#include <mosal.h>

/* number of tavor physical ports defined here */                                
/* for use in checking port number provided in addr vector */
#define THHUL_TAVOR_NUM_PORTS  2

/* allocate initially space for 16 PDs for user-space processes, and 256 for kernel-space */
/* ----- OS-dependent implementation ----- */
#ifndef MT_KERNEL
#define THHUL_PDM_INITIAL_NUM_PDS  16
#else
#define THHUL_PDM_INITIAL_NUM_PDS  256
#endif

/*  PD entry */
typedef struct THHUL_pd_st {
    HH_pd_hndl_t    hh_pd_hndl;
    THH_udavm_t     udavm;
    MT_virt_addr_t     udav_nonddr_table;
    MT_virt_addr_t     udav_nonddr_table_aligned;
    MT_size_t       uadv_nonddr_table_alloc_size;
    VAPI_lkey_t     lkey;
    MT_bool         valid;
    HHUL_pd_hndl_t  hhul_pd_hndl; 
} THHUL_pd_t;

struct THHUL_pdm_st {
    MT_bool         priv_ud_av;
    THH_ver_info_t  version;
    VIP_array_p_t   pd_array;
} ;

#endif /* H_THHUL_PDM_H */
