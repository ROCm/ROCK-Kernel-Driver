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

#include <vipkl.h>
#include <fork_sup.h>
#include <vapi_common.h>
#include <VIP_rsct.h>
#include "vipkl_sys.h"
#if LINUX_KERNEL_2_6
  #include <linux/file.h>
  #define file_acq_lock(lock) do {spin_lock(lock);} while(0)
  #define file_rel_lock(lock) do {spin_unlock(lock);} while(0)
#else
  #define file_acq_lock(lock) do {read_lock(lock);} while(0)
  #define file_rel_lock(lock) do {read_unlock(lock);} while(0)
#endif

               
               
               
               
#define W_RET					po->ret

/* Generic input parameters size check (FM #18007) */                                                   
#define W_IPARAM_SZ_CHECK_DEFAULT(name)                                                           \
  if (sizeof(struct i_##name##_ops_t) > pi_sz) {                                                  \
     MTL_ERROR1(MT_FLFMT("%s: Input parameters buffer is too small "                              \
      "(sizeof(i_"#name"_ops_t)="SIZE_T_DFMT" > pi_sz="SIZE_T_DFMT")"),                                                 \
      __func__, sizeof(struct i_##name##_ops_t), pi_sz);  \
     return VIP_EINVAL_PARAM;                                                                     \
  }

/* Generic output parameters size check (FM #18007) */                                                   
#define W_OPARAM_SZ_CHECK_DEFAULT(name)                                                           \
  if (sizeof(struct o_##name##_ops_t) > po_sz) {                                                  \
     MTL_ERROR1(MT_FLFMT("%s: Output parameters buffer is too small "                             \
      "(sizeof(o_"#name"_ops_t)="SIZE_T_DFMT" > po_sz="SIZE_T_DFMT")"),       \
      __func__, sizeof(struct o_##name##_ops_t), po_sz);  \
     return VIP_EINVAL_PARAM;                                                                     \
  }

/* Size check for input buffer which includes variable size field (array[1]) */
#define W_IPARAM_SZ_CHECK_VARSIZE_BASE(name,vardata)                                              \
  if (FIELD_OFFSET(struct i_##name##_ops_t, vardata) > pi_sz) {                                   \
     MTL_ERROR1(MT_FLFMT("%s: Input parameters buffer is too small "                              \
      "(FIELD_OFFSET(struct i_"#name"_ops_t, "#vardata")=%lu > pi_sz="SIZE_T_DFMT")"),                       \
      __func__, FIELD_OFFSET(struct i_##name##_ops_t, vardata), pi_sz);                           \
     return VIP_EINVAL_PARAM;                                                                     \
  }

/* Size check for output buffer which includes variable size field (array[1]) */
#define W_OPARAM_SZ_CHECK_VARSIZE_BASE(name,vardata)                                              \
  if (FIELD_OFFSET(struct o_##name##_ops_t, vardata) > po_sz) {                                   \
     MTL_ERROR1(MT_FLFMT("%s: Output parameters buffer is too small "                             \
      "(FIELD_OFFSET(struct o_"#name"_ops_t, "#vardata")=%lu > pi_sz="SIZE_T_DFMT")"),                       \
      __func__, FIELD_OFFSET(struct o_##name##_ops_t, vardata), pi_sz);                           \
     return VIP_EINVAL_PARAM;                                                                     \
  }
  
/* Verify given input buffer size matches given table length (FM #18007)*/
#define W_IPARAM_SZ_CHECK_TABLE(name,table_base,table_len_fld)                                    \
       if ((pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, table_base)) || /* _sz field is valid */\
           (pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, table_base) +                           \
            pi->table_len_fld*sizeof(pi->table_base)) ) {                                         \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given input " #table_base                                                         \
           " buffer is smaller (%lu entries) than declared ("SIZE_T_DFMT" entries) (pi_sz="SIZE_T_DFMT")"),             \
           __func__,                                                                              \
           (pi_sz - FIELD_OFFSET(struct i_##name##_ops_t, table_base)) / sizeof(pi->table_base) , \
           (pi_sz >= FIELD_OFFSET(struct i_##name##_ops_t, table_base)) ? (MT_size_t)pi->table_len_fld : 0,  \
           pi_sz );                                                                               \
         return VIP_EINVAL_PARAM;                                                                 \
       }

/* Verify given output buffer size matches given table length (FM #18007) */
/* Use this macro only after verifying validity of input params buffer    */
/* (it refers to pi->table_len_fld)                                       */
#define W_OPARAM_SZ_CHECK_TABLE(name,table_base,table_len_fld)                                    \
       if (po_sz < FIELD_OFFSET(struct o_##name##_ops_t, table_base) +                            \
            pi->table_len_fld*sizeof(po->table_base)) {                                           \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given output " #table_base                                                        \
           " buffer is smaller (%lu entries) than declared ("SIZE_T_DFMT" entries)"),                        \
           __func__,                                                                              \
           (po_sz - FIELD_OFFSET(struct o_##name##_ops_t, table_base)) / sizeof(po->table_base) , \
           (MT_size_t)pi->table_len_fld);                                                                    \
         return VIP_EINVAL_PARAM;                                                                 \
       }

/* Verify given input buffer size matches given *_ul_resources_sz (FM #18007)*/
#define W_IPARAM_SZ_CHECK_ULRES(name,vardata)                                                     \
       if ((pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, vardata)) || /* _sz field is valid */   \
           (pi_sz < FIELD_OFFSET(struct i_##name##_ops_t, vardata) + pi->vardata##_sz )) {        \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given input ul_res buffer is smaller (%lu B) than declared ("SIZE_T_DFMT" B)"),__func__,     \
           pi_sz - FIELD_OFFSET(struct i_##name##_ops_t, vardata),                                \
           (pi_sz >= FIELD_OFFSET(struct i_##name##_ops_t, vardata)) ? pi->vardata##_sz : 0 );    \
         return VIP_EINVAL_PARAM;                                                                 \
         }

/* Verify given output buffer size matches given *_ul_resources_sz (FM #18007)*/
/* Use this macro only after verifying validity of input params buffer (it refers to pi->*_sz)   */
#define W_OPARAM_SZ_CHECK_ULRES(name,vardata)                                                     \
       if (po_sz < FIELD_OFFSET(struct o_##name##_ops_t, vardata) + pi->vardata##_sz ) {          \
         MTL_ERROR1(MT_FLFMT(                                                                     \
           "%s: Given output ul_res buffer is smaller (%lu B) than declared ("SIZE_T_DFMT" B)"),__func__,    \
           po_sz - FIELD_OFFSET(struct o_##name##_ops_t, vardata),                                \
           pi->vardata##_sz);                                                                     \
         return VIP_EINVAL_PARAM;                                                                 \
       }

#define W_DEFAULT_PARAM_SZ_CHECK(name)      \
  W_IPARAM_SZ_CHECK_DEFAULT(name)           \
  W_OPARAM_SZ_CHECK_DEFAULT(name)

/* create function prototype */
#define W_FUNC_PROTOTYPE(name)        \
static int name##_stat(               \
  struct file *file,                  \
  VIPKL_linux_priv_t *lp,             \
  struct i_##name##_ops_t * pi,       \
  MT_size_t pi_sz,                    \
  struct o_##name##_ops_t * po,       \
  MT_size_t po_sz,                    \
  u_int32_t* ret_po_sz_p)             \
{


/* create function prototype and print it name by MTL_DEBUG3 */
#define W_FUNC_START(name)                  \
  W_FUNC_PROTOTYPE(name)                    \
  W_DEFAULT_PARAM_SZ_CHECK(name)
  
/* Used when one needs to use local variables in the wrapper function */
/* Use W_DEFAULT_PARAM_SZ_CHECK after local variables are declared    */
#define W_FUNC_START_WO_PRINT(name)   W_FUNC_PROTOTYPE(name)              

#define W_FUNC_START_WO_PARAM_SZ_CHECK(name)  \
  W_FUNC_PROTOTYPE(name)                      \
  MTL_DEBUG3("CALLING " #name "_stat \n");  
  


/* print function name by MTL_DEBUG3. Used when one uses more local variables in the wrapper function */
#define W_FUNC_NAME(name)  {MTL_DEBUG3("CALLING " #name "_stat \n");}

/* This return code is the wrapper admin. return code - 
 *  not W_RET, which is the result of the VIPKL call.
 * If we reached here, the wrapper operation was OK (parameters were passed successfully) */
#define W_FUNC_END  return VIP_OK; }

/* return call_result and end the function */
#define W_FUNC_END_RC_SZ                                                               \
  if ((W_RET != VIP_OK)	&& (po_sz >= sizeof(VIP_ret_t)))  *ret_po_sz_p = sizeof(VIP_ret_t);  \
  W_FUNC_END

/***************************************************************************/


W_FUNC_START(FS_get_file_count)
  W_RET = FS_get_file_count(file, &po->count);
  *ret_po_sz_p = FIELD_OFFSET(struct o_FS_get_file_count_ops_t, count);
  if ( W_RET == VIP_OK ) {
    (*ret_po_sz_p) += sizeof(po->count);
  }
  W_FUNC_END

W_FUNC_START(FS_restore_perm)
  W_RET = FS_restore_perm(lp, file, pi->count);
  *ret_po_sz_p = sizeof(struct o_FS_restore_perm_ops_t);
  W_FUNC_END

W_FUNC_START(FS_get_as_list)
  W_RET = FS_get_as_list(lp, pi->op, &po->act, &po->addr_lst[0]);
  if ( (W_RET==VIP_OK) || (W_RET==VIP_EOL) ) {
    MTL_DEBUG1(MT_FLFMT("%s(pid="MT_ULONG_PTR_FMT") op=%s, act=%d"), __func__, MOSAL_getpid(), FS_op_str(pi->op), po->act);
    {
      int i;
      for ( i=0; i<po->act; ++i ) {
        MTL_DEBUG1(MT_FLFMT("%s(pid="MT_ULONG_PTR_FMT") addr="VIRT_ADDR_FMT", size="SIZE_T_FMT", page_shift=%d"), __func__, MOSAL_getpid(), po->addr_lst[i].start_addr, po->addr_lst[i].size, po->addr_lst[i].page_shift);
      }
    }
    *ret_po_sz_p = FIELD_OFFSET(struct o_FS_get_as_list_ops_t, addr_lst) + sizeof(VIP_region_data_t)*MIN(AS_ADDR_ARR_SZ, po->act);
  }
  else {
    *ret_po_sz_p = FIELD_OFFSET(struct o_FS_get_as_list_ops_t, act);
  }
  W_FUNC_END

#define FS_INVOKE_STAT_FUNC(func) rc = func##_stat(file, lp, (struct i_##func##_ops_t *)pi, pi_sz, (struct o_##func##_ops_t*)po, po_sz, ret_po_sz_p)

VIP_ret_t FS_ioctl(FS_ops_t ops,
                   struct file *file,
                   VIPKL_linux_priv_t *lp,
                   void *pi,
                   u_int32_t pi_sz,
                   void *po,
                   u_int32_t po_sz,
                   u_int32_t *ret_po_sz_p)
{
  VIP_ret_t rc;

  MTL_DEBUG1(MT_FLFMT("%s: pid="MT_PID_FMT", ops=%s"), __func__, MOSAL_getpid(), FS_ioctl_str(ops));
  switch ( ops ) {
    case FS_IOCTL_GET_FILE_COUNT: FS_INVOKE_STAT_FUNC(FS_get_file_count); break;
    case FS_IOCTL_RESTORE_PERM: FS_INVOKE_STAT_FUNC(FS_restore_perm); break;
    case FS_IOCTL_GET_AS_LIST: FS_INVOKE_STAT_FUNC(FS_get_as_list); break;
    default:
      rc = VIP_ENOSYS;
  }
  return rc;
}


/*
 *  FS_open
 */
int FS_open(fork_sup_vipkl_t *fs)
{
  fs->hca_iter = 0;
  fs->state = FS_STATE_IDLE;
  MOSAL_mutex_init(&fs->mtx);
  return 1;
}


/*
 *  FS_close
 */
void FS_close(fork_sup_vipkl_t *fs)
{
}



/*
 *  RSCT_check_inc
 */ 
static inline VIP_ret_t RSCT_check_inc(VIP_hca_hndl_t hca_hndl, VIP_hca_state_t *hca_p)
{      
  if ( hca_hndl > VIP_MAX_HCA ) return VIP_EINVAL_HCA_HNDL;
  if ( MOSAL_spinlock_lock(&hca_p->lock) != MT_OK ) return VIP_EINVAL_PARAM;  
  if ( hca_p->state < VIP_HCA_STATE_AVAIL ) { 
    MOSAL_spinlock_unlock(&hca_p->lock);    
    return VIP_EINVAL_HCA_HNDL;    
  }
  else {
    hca_p->state++; 
  }
  MOSAL_spinlock_unlock(&hca_p->lock);    
  return VIP_OK;
}


/*
 *  RSCT_dec
 */
static inline void RSCT_dec(VIP_hca_hndl_t hca_hndl, VIP_hca_state_t *hca_p)
{
  MOSAL_spinlock_lock(&hca_p->lock);
  hca_p->state--;
  MOSAL_spinlock_unlock(&hca_p->lock);
}


/*
 *  get_bunch_pointers
 */
static VIP_ret_t get_bunch_pointers(VIPKL_linux_priv_t *lp, VIP_region_data_t *addr_lst_p, unsigned int *act_p)
{
  VIP_ret_t rc=VIP_EOL;
  fork_sup_vipkl_t *fs = &lp->fs;
  uint i;

  MTL_DEBUG1(MT_FLFMT("%s: pid="MT_PID_FMT", lp=%p"), __func__, MOSAL_getpid(), lp);
  *act_p = 0;
  for ( i=fs->hca_iter; i<VIP_MAX_HCA; ++i ) {
    if ( RSCT_check_inc(i, &lp->rsct_arr[i]) != VIP_OK ) {
      continue;
    }
    MTL_DEBUG1(MT_FLFMT("%s: i=%d, state=%d"), __func__, i, lp->rsct_arr[i].state);
    
    rc = VIPKL_get_mr_list(lp->rsct_arr[fs->hca_iter].rsct ,&fs->hca_iter, addr_lst_p, AS_ADDR_ARR_SZ, act_p);

    RSCT_dec(i, &lp->rsct_arr[i]);
  }

  return rc;
}


/*
 *  fs_handle_init
 */
static VIP_ret_t fs_handle_init(VIPKL_linux_priv_t *lp)
{
  VIP_ret_t rc;
  fork_sup_vipkl_t *fs = &lp->fs;
  int i;

  fs->hca_iter = 0;

  for ( i=0; i<VIP_MAX_HCA; ++i ) {
    if ( (rc=RSCT_check_inc(i, &lp->rsct_arr[i])) != VIP_OK ) {
      return rc;
    }
    rc = VIP_RSCT_iter_init(lp->rsct_arr[i].rsct, VIP_RSCT_MR);
    RSCT_dec(i, &lp->rsct_arr[i]);
  }

  return rc;
}



/*
 *  FS_get_file_count
 */                     
VIP_ret_t FS_get_file_count(struct file *file, uint *count_p)
{
  struct files_struct *files = current->files;

  file_acq_lock(&files->file_lock);
  *count_p = file_count(file);
  file_rel_lock(&files->file_lock);
  MTL_DEBUG1(MT_FLFMT("%s: original count=%d"), __func__, *count_p);
  return VIP_OK;
}


/*
 *  FS_restore_perm
 */                     
VIP_ret_t FS_restore_perm(VIPKL_linux_priv_t *lp, struct file *file, uint count)
{
  uint cur_count;
  VIP_ret_t rc;
  uint act_ptrs;
  
  if ( MOSAL_mutex_acq(&lp->fs.mtx, TRUE) == MT_OK ) {
    MTL_DEBUG1(MT_FLFMT("%s(pid="MT_PID_FMT"): called: lp=%p, file=%p"), __func__, MOSAL_getpid(), lp, file);
    FS_get_file_count(file, &cur_count);
    if ( count < cur_count ) {
      MTL_DEBUG1(MT_FLFMT("%s"), __func__);
      MOSAL_mutex_rel(&lp->fs.mtx);
      return VIP_EAGAIN;
    }

    if ( (rc=fs_handle_init(lp)) != VIP_OK ) {
      MTL_ERROR1(MT_FLFMT("%s: fs_handle_init failed"), __func__);
      MOSAL_mutex_rel(&lp->fs.mtx);
      return rc;
    }

    rc = get_bunch_pointers(lp, NULL, &act_ptrs);
    MTL_DEBUG1(MT_FLFMT("%s(pid="MT_PID_FMT"): - rc=%s"), __func__, MOSAL_getpid(), VAPI_strerror_sym(rc));
    MOSAL_mutex_rel(&lp->fs.mtx);
  }
  else rc = VIP_EINTR;
  return rc;

}


/*
 *  FS_get_as_list
 */                     
VIP_ret_t FS_get_as_list(VIPKL_linux_priv_t *lp,
                         FS_as_list_op_t op,
                         unsigned int *act_p,
                         VIP_region_data_t *addr_lst_p)
{

  VIP_ret_t rc;
  call_result_t mrc;
  uint act_ptrs;

  MTL_DEBUG1(MT_FLFMT("%s: op=%s"), __func__, FS_op_str(op));

  if ( !lp ) {
    MTL_ERROR1(MT_FLFMT("%s: lp==NULL"), __func__);
    return VIP_EINVAL_PARAM;
  }
  mrc = MOSAL_mutex_acq(&lp->fs.mtx, TRUE);
  if ( mrc != MT_OK ) {
    rc = VIP_EINTR;
    MTL_DEBUG1(MT_FLFMT("%s: failed to acquire lock"), __func__);
    goto ex_no_unlock;
  }
  switch ( lp->fs.state ) {
    case FS_STATE_IDLE:
      if ( op != FS_OP_GET_FIRST ) {
        rc = VIP_EINVAL_PARAM;
        MTL_ERROR1(MT_FLFMT("%s: invalid state"), __func__);
        goto ex_unlock;
      }
      if ( (rc=fs_handle_init(lp)) != VIP_OK ) {
        MTL_ERROR1(MT_FLFMT("%s: fs_handle_init failed"), __func__);
        goto ex_unlock;
      }
      lp->fs.state = FS_STATE_IN_USE;
      break;
    case FS_STATE_IN_USE:
      if ( op == FS_OP_GET_NEXT ) {
        /* no state change */
        break;
      }
      else if ( op == FS_OP_END ) {
        lp->fs.state = FS_STATE_IDLE;
        rc = VIP_OK;
        MTL_DEBUG1(MT_FLFMT("%s: rc=%s"), __func__, VAPI_strerror_sym(rc));
        goto ex_unlock;
      }
      else {
        rc = VIP_EINVAL_PARAM;
        MTL_ERROR1(MT_FLFMT("%s: invalid state"), __func__);
        goto ex_unlock;
      }
    default:
      MTL_ERROR1(MT_FLFMT("invalid state (%d)"), lp->fs.state);
      rc = VIP_EINVAL_PARAM;
      goto ex_unlock;
  }

  rc = get_bunch_pointers(lp, addr_lst_p, &act_ptrs);
  if ( (rc==VIP_OK) || (rc==VIP_EOL) ) {
    *act_p = act_ptrs;
    MTL_DEBUG1(MT_FLFMT("%s: act=%d, rc=%s"), __func__, *act_p, VAPI_strerror_sym(rc));
  }
ex_unlock:
  MOSAL_mutex_rel(&lp->fs.mtx);
ex_no_unlock:
  MTL_DEBUG1(MT_FLFMT("%s: rc=%s"), __func__, VAPI_strerror_sym(rc));
  return rc;
}

