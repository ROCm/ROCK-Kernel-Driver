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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: udapl_mod.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _UDAPL_MOD_H_
#define _UDAPL_MOD_H_

#include <linux/ioctl.h>

#include <vapi_types.h>

#include "ib_legacy_types.h"
#include "ts_ib_core_types.h"

#define UDAPL_DEVNAME "udapl"
#define UDAPL_DEVFILE "/dev/" UDAPL_DEVNAME


typedef struct wait_obj_wait_param_s {
    void          *wait_obj;
    unsigned long timeout;
    unsigned int  timedout;
} wait_obj_wait_param_t;

typedef struct get_khca_hndl_param_s {
    VAPI_hca_id_t   hca_id;
    VAPI_hca_hndl_t k_hca_handle;
    void            *ts_hca_handle;
} get_khca_hndl_param_t;

typedef struct set_comp_eventh_param_s {
    VAPI_hca_hndl_t            k_hca_handle;
    VAPI_k_cq_hndl_t           k_cq_handle;
    void                       *wait_obj;
    EVAPI_compl_handler_hndl_t comp_handler_handle;
} set_comp_eventh_param_t;

typedef struct clear_comp_eventh_param_s {
    VAPI_hca_hndl_t            k_hca_handle;
    EVAPI_compl_handler_hndl_t comp_handler_handle;
} clear_comp_eventh_param_t;

typedef struct path_record_param_s {
    uint8_t            *dst_ip_addr;
    uint8_t            *src_gid;
    void               *ts_hca_handle;
    uint8_t            port;
    tUINT32            ats_flag;
    tTS_IB_PATH_RECORD path_record;
} path_record_param_t;

typedef struct smr_insert_param_s {
    void         *cookie;
    tUINT32      exists;
} smr_insert_param_t;

typedef struct smr_add_mrh_param_s {
    void           *cookie;
    VAPI_mr_hndl_t mr_handle;
} smr_add_mrh_param_t;

typedef struct smr_del_mrh_param_s {
    void           *cookie;
    VAPI_mr_hndl_t mr_handle;
} smr_del_mrh_param_t;

typedef struct smr_query_param_s {
    void           *cookie;
    tUINT32        ready;
    VAPI_mr_hndl_t mr_handle;
} smr_query_param_t;

typedef struct smr_dec_param_s {
    void        *cookie;
} smr_dec_param_t;

typedef struct ats_ipaddr_lookup_param_s {
    uint8_t   *gid;
    void      *ts_hca_handle;
    uint8_t   port;
    uint8_t   *ip_addr;
} ats_ipaddr_lookup_param_t;

typedef struct ats_set_ipaddr_param_s {
    uint8_t    *gid;
    void       *ts_hca_handle;
    uint8_t    port;
    uint8_t    *ip_addr;
} ats_set_ipaddr_param_t;

typedef struct get_hca_ipaddr_param_s {
    uint8_t    *gid;
    uint32_t   ip_addr;
} get_hca_ipaddr_param_t;

#define T_IOC_MAGIC 92
#define T_WAIT_OBJ_INIT     _IOW(T_IOC_MAGIC,   0, void **)
#define T_WAIT_OBJ_WAIT     _IOWR(T_IOC_MAGIC,  1, wait_obj_wait_param_t *)
#define T_WAIT_OBJ_WAKEUP   _IOR(T_IOC_MAGIC,   2, void *)
#define T_WAIT_OBJ_DESTROY  _IOR(T_IOC_MAGIC,   3, void *)
#define T_GET_KHCA_HNDL     _IOWR(T_IOC_MAGIC,  4, get_khca_hndl_param_t *)
#define T_RELEASE_KHCA_HNDL _IOWR(T_IOC_MAGIC,  5, VAPI_hca_hndl_t)
#define T_SET_COMP_EVENTH   _IOWR(T_IOC_MAGIC,  6, set_comp_eventh_param_t *)
#define T_CLEAR_COMP_EVENTH _IOR(T_IOC_MAGIC,   7, clear_comp_eventh_param_t *)
#define T_PATH_RECORD       _IOWR(T_IOC_MAGIC,  8, path_record_param_t *)
#define T_SMR_INSERT        _IOWR(T_IOC_MAGIC,  9, smr_insert_param_t *)
#define T_SMR_ADD_MRH       _IOR(T_IOC_MAGIC,  10, smr_add_mrh_param_t *)
#define T_SMR_DEL_MRH       _IOR(T_IOC_MAGIC,  11, smr_del_mrh_param_t *)
#define T_SMR_QUERY         _IOWR(T_IOC_MAGIC, 12, smr_query_param_t *)
#define T_SMR_DEC           _IOR(T_IOC_MAGIC,  13, smr_dec_param_t *)
#define T_ATS_IPADDR_LOOKUP _IOWR(T_IOC_MAGIC, 14, ats_ipaddr_lookup_param_t *)
#define T_ATS_SET_IPADDR    _IOR(T_IOC_MAGIC,  15, ats_set_ipaddr_param_t *)
#define T_GET_HCA_IPADDR    _IOWR(T_IOC_MAGIC, 16, get_hca_ipaddr_param_t *)
#define T_SMR_MUTEX_LOCK    _IO(T_IOC_MAGIC,   17)
#define T_SMR_MUTEX_UNLOCK  _IO(T_IOC_MAGIC,   18)
#define T_IOC_MAXNR 18

#endif
