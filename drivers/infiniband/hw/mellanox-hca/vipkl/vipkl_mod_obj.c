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

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/linkage.h>
#include <linux/init.h>
// #include <sys/syscall.h>
#include <asm/uaccess.h>
#endif

#include "vipkl_wrap.h"
#include <vapi_common.h>
#include <linux/sched.h>


MODULE_LICENSE("GPL");

//Jack, for ioctl
// The name in the /proc/devices file
#define VIPKL_DRIVER_NAME "mellanox_vipkl"
int vipkl_dev_major_num=0;
static m_file_op_t vipkl_file_ops;
MODULE_PARM(vipkl_dev_major_num,"i");
VIP_ret_t VIPKL_ioctl(VIPKL_ops_t ops,VIP_hca_state_t* rsct_arr,void *pi, u_int32_t isz, void *po, 
	u_int32_t osz, u_int32_t* bs_p );


void VIPKL_wrap_kernel_init(void);

int vipkl_dev_open(struct inode *inode, struct file *filp)
{

  MTL_TRACE2("opening vip_dev \n");
  filp->private_data = VIPKL_open();  	
  if ( !filp->private_data ) {
    MTL_ERROR1(MT_FLFMT("%s: VIPKL_open failed"), __func__);
    return -1;
  }
  return 0;          /* success */
}

static int vipkl_signal_hndlr(void* priv)
{
    /* block all the signals */
    return 0;
}

int vipkl_dev_release(struct inode *inode, struct file *filp)
{
  int i;
  sigset_t mask;
        
  MTL_TRACE2("release vip_dev \n");
  /* set all signals */
  for (i=1; i< _NSIG; i++) {
    sigaddset(&mask,i);
  }
        
  block_all_signals(vipkl_signal_hndlr,NULL,&mask);
  flush_signals(current);
  VIPKL_close((VIP_hca_state_t*)filp->private_data);
  unblock_all_signals();
  return 0;
}

int vipkl_wrap_kernel_ioctl(struct inode *inode, struct file *file,
                          unsigned int ioctl_num, unsigned long ioctl_param)
{
    vip_ioctl_wrap_t vip_ioctl_wrap;
    int rc;
    void *pi = NULL, *po = NULL;
    u_int32_t bytes_to_return;
#define MAX_PI_SZ		8192    
#define MAX_PO_SZ		(8192*2)    

    /* get parameter list */    
    copy_from_user(&vip_ioctl_wrap, (char*) ioctl_param , sizeof(vip_ioctl_wrap));
    MTL_TRACE2(MT_FLFMT("fcn=%d, ioctl_num=0x%x"), (int)vip_ioctl_wrap.ops, ioctl_num);

    /* sanity checks */
    if (vip_ioctl_wrap.pi_sz > MAX_PI_SZ || vip_ioctl_wrap.po_sz > MAX_PO_SZ) {
         MTL_ERROR2("%s: Suspiciously long buffers fcn %d, (pi_sz %d, po_sz %d).\n", __func__,
           (int)vip_ioctl_wrap.ops, vip_ioctl_wrap.pi_sz, vip_ioctl_wrap.po_sz );
         rc = (int)VIP_EINVAL_PARAM;
         goto err_exit;
    }
    	
    /* allocate INPUT buffer */
    if (vip_ioctl_wrap.pi_sz) {
    	pi = (void*)MALLOC(vip_ioctl_wrap.pi_sz);
    	if (pi == NULL) {
         MTL_ERROR2("%s: Cannot allocate INPUT buffer (pi_sz %d).\n", __func__, vip_ioctl_wrap.pi_sz);
         rc = (int)VIP_EAGAIN;
         goto err_exit;
    	}  
    }
    
    /* allocate OUTPUT buffer */
    if (vip_ioctl_wrap.po_sz) {
    	po = (void*)MALLOC(vip_ioctl_wrap.po_sz);
    	if (po == NULL) {
         MTL_ERROR2("%s: Cannot allocate OUTPUT buffer (po_sz %d).\n", __func__, vip_ioctl_wrap.po_sz);
         rc = (int)VIP_EAGAIN;
         goto free_ibuf;
    	}  
    }
    
    /* copy INPUT buffer to kernel */
    copy_from_user(pi, vip_ioctl_wrap.pi , vip_ioctl_wrap.pi_sz);
    
    /* perform the function */
    rc =  VIPKL_ioctl(vip_ioctl_wrap.ops,(VIP_hca_state_t*)file->private_data, pi,vip_ioctl_wrap.pi_sz,
	       	po,vip_ioctl_wrap.po_sz, &bytes_to_return);

    /* return values */
    if (bytes_to_return)
      copy_to_user(vip_ioctl_wrap.po , po, bytes_to_return);

    /* free memory */
    if (po)
    	FREE(po);
    
free_ibuf:
  if (pi)
    FREE(pi);
err_exit:    
    return rc;
}


/*
 *  init_module
 */
static int vipkl_module_init(void)
{
  VIP_ret_t rc;

  rc= VIPKL_init_layer();
  if (rc != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("Failed VIPKL_init_layer() (%s)"),VAPI_strerror_sym(rc));
    return -1;
  }

  VIPKL_wrap_kernel_init();
  vipkl_file_ops.owner = THIS_MODULE;
  vipkl_file_ops.ioctl = vipkl_wrap_kernel_ioctl;
  vipkl_file_ops.open = vipkl_dev_open;
  vipkl_file_ops.release = vipkl_dev_release;
  mosal_chrdev_register(&vipkl_dev_major_num,VIPKL_DRIVER_NAME,&vipkl_file_ops);

  MTL_TRACE('1',"VIPKL kernel module initialized successfully\n");
  return 0;
}


/*
 *  cleanup_module
 */
static void vipkl_module_exit(void)
{
  MTL_TRACE1("Inside " "%scalling VIPKL_cleanup\n", __func__);

  mosal_chrdev_unregister(vipkl_dev_major_num,VIPKL_DRIVER_NAME);
  VIPKL_cleanup();
  
  MTL_TRACE1("VIPKL kernel module removed successfully\n");
}


module_init(vipkl_module_init);
module_exit(vipkl_module_exit);
