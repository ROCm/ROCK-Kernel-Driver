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


#ifndef __FORK_SUP__
#define __FORK_SUP__

#include <vip.h>
#include <vipkl_wrap.h>
#ifdef __KERNEL__
#include <linux/fs.h>
#else
struct file {
};
#endif



typedef enum {
  FS_STATE_IDLE,
  FS_STATE_IN_USE
}
FS_state_t;

typedef struct {
  VIP_hca_hndl_t hca_iter;
  FS_state_t state;
  MOSAL_mutex_t mtx;
}
fork_sup_vipkl_t;


int FS_open(fork_sup_vipkl_t *fs);
void FS_close(fork_sup_vipkl_t *fs);


#define AS_ADDR_ARR_SZ 256  /* size of array passed to VIPKL_get_as_list */


#define FS_IOCTL_CALL _IOR('y',1,void *)
#define FS_FUNC_BASE 200

typedef enum {
  FS_OP_GET_FIRST, /* initilaze the iterator and return */
  FS_OP_GET_NEXT,
  FS_OP_END
}
FS_as_list_op_t;


typedef struct {
  fork_sup_vipkl_t fs;
  VIP_hca_state_t *rsct_arr;
}
VIPKL_linux_priv_t;


typedef enum {
  FS_IOCTL_GET_FILE_COUNT  =  FS_FUNC_BASE,
  FS_IOCTL_RESTORE_PERM,
  FS_IOCTL_GET_AS_LIST
}
FS_ops_t;


/* FS_get_file_count */
struct i_FS_get_file_count_ops_t {
};

struct o_FS_get_file_count_ops_t {
  OUT VIP_ret_t ret;
  OUT unsigned int count;
};


/* FS_restore_perm */
struct i_FS_restore_perm_ops_t {
  IN unsigned int count;
};

struct o_FS_restore_perm_ops_t {
  OUT VIP_ret_t ret;
};


/* FS_get_as_list */
struct i_FS_get_as_list_ops_t {
  IN FS_as_list_op_t op;
  IN int filedes;
};

struct o_FS_get_as_list_ops_t {
  OUT VIP_ret_t ret;
  OUT unsigned int act;
  OUT VIP_region_data_t addr_lst[1];
};


VIP_ret_t FS_ioctl(FS_ops_t ops,
                   struct file *file,
                   VIPKL_linux_priv_t *lp,
                   void *pi,
                   u_int32_t pi_sz,
                   void *po,
                   u_int32_t po_sz,
                   u_int32_t *ret_po_sz_p);


/*************************************************************************
 * Function: FS_get_file_count
 *
 * Arguments:
 *  file(in): file struct from whose usage count is needed
 *  count_p(in): file usage count returned
 *
 * Returns:
 *  VIP_OK
 *
 * Description:
 *     return vipkl's file usage count before forking
 *************************************************************************/ 
VIP_ret_t FS_get_file_count(struct file *file, unsigned int *count_p);


/*************************************************************************
 * Function: FS_restore_perm
 *
 * Arguments:
 *  lp(in): Linux private data containing context data for supporting fork
 *  file(in): vipkl's file struct
 *  count(in): file usage count needed to perform restoration
 *
 * Returns:
 *  VIP_OK  - success
 *  VIP_EAGAIN - required usage count not yet obtained
 *
 * Description:
 *     The function checks if the usage count of file is equal count and then
 *     scans the iobufs and restores the ptes permissions according to those
 *     of the iobuf
 *************************************************************************/ 
VIP_ret_t FS_restore_perm(VIPKL_linux_priv_t *lp, struct file *file, unsigned int count);


/*************************************************************************
 * Function: FS_get_as_list
 *
 * Arguments:
 *  lp(in): Linux private data containing context data for supporting fork
 *  op(in): operation rewired
 *  act_p(out): how many entries returned
 *
 * Returns:
 *  VIP_OK  - success
 *  VIP_EOL - success - not all array returned
 *  VIP_ERROR - error obtaining data
 *
 * Description:
 *  The function should be called in the order:
 *  1. FS_OP_GET_FIRST
 *  2. FS_OP_GET_NEXT while returning VIP__OK
 *  3. FS_OP_END
 *************************************************************************/ 
VIP_ret_t FS_get_as_list(VIPKL_linux_priv_t *lp,
                         FS_as_list_op_t op,
                         unsigned int *act_p,
                         VIP_region_data_t *addr_lst_p);


VIP_ret_t FS_osdep_init(void);
void FS_osdep_cleanup(void);

/*
 *  FS_op_str
 */
const static inline char *FS_op_str(FS_as_list_op_t op)
{
  switch ( op ) {
    case FS_OP_GET_FIRST:
      return "FS_OP_GET_FIRST";
    case FS_OP_GET_NEXT:
      return "FS_OP_GET_NEXT";
    case FS_OP_END:
      return "FS_OP_END";
    default:
        return "Invalid";
  }
}

const static inline char *FS_ioctl_str(FS_ops_t ioctl)
{
  switch ( ioctl ) {
    case FS_IOCTL_GET_FILE_COUNT:
      return "FS_IOCTL_GET_FILE_COUNT";
      break;
    case FS_IOCTL_RESTORE_PERM:
      return "FS_IOCTL_RESTORE_PERM";
      break;
    case FS_IOCTL_GET_AS_LIST:
      return "FS_IOCTL_GET_AS_LIST";
      break;
    default:
      return "Invalid";
  }
}

#endif /* __FORK_SUP__ */
