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

/*
*-------------------------------------------------------------------
* 									   
*-------------------------------------------------------------------								 
* Kholodenko Alex 								        
*-------------------------------------------------------------------
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>


MODULE_LICENSE("GPL");
#include "mosal_priv.h"


#include <mtl_common.h>
#include <mosal_wrap.h>
call_result_t MOSAL_ioctl(MOSAL_ops_t ops, void *pi, u_int32_t pi_sz, void *po, u_int32_t po_sz, u_int32_t* ret_po_sz_p );


static int mosal_open (struct inode *inode, struct file *filp);
static int mosal_release(struct inode *inode, struct file *filp);

//-----------------------------------------------------------------------------
// Types Definition
//-----------------------------------------------------------------------------

/* contains resources belonging to a proccess
   that need to be tracked */
typedef struct mosal_rsrc_st {
  MOSAL_mlock_ctx_t mlock_ctx; /* mlock context object */
  k2u_cbk_hndl_t k2u_cbk_h;  /* handle to callback db */
}
mosal_rsrc_t;

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------
struct file_operations mosal_fops = {
  open:mosal_open,				
  release:mosal_release,				
	ioctl:mosal_ioctl,
};

//-----------------------------------------------------------------------------
// Global Variables
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Global Function Definition
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Local Function Definition
//-----------------------------------------------------------------------------


static int mosal_open (struct inode *inode, struct file *filp)
{
	call_result_t rc;
  mosal_rsrc_t *mosal_rsrc_p;

	MTL_DEBUG7("%s: opening contmem(filp=0x%p)\n", __func__,filp);

  if ( filp->private_data ) {
    MTL_ERROR1(MT_FLFMT("%s: filp->private_data expected null but is not"), __func__);
    return -EAGAIN;
  }
  mosal_rsrc_p = TMALLOC(mosal_rsrc_t);
  if ( !mosal_rsrc_p ) {
    MTL_ERROR1(MT_FLFMT("%s: failed to allocate memory to hold mosal_rsrc_t"), __func__);
    return -EAGAIN;
  }
  mosal_rsrc_p->mlock_ctx = NULL;
  mosal_rsrc_p->k2u_cbk_h = INVALID_K2U_CBK_HNDL;
  filp->private_data = mosal_rsrc_p;
	rc = MOSAL_mlock_ctx_init(&mosal_rsrc_p->mlock_ctx);
	if ( rc != MT_OK ) {
	  MTL_ERROR1(MT_FLFMT("MOSAL_mlock_ctx_init failed, pid=%d (%s)"),
               current->pid,mtl_strerror_sym(rc));
    FREE(mosal_rsrc_p);
    filp->private_data = NULL;
	  return -1;
	}
	return 0;		   /* success */
}

static int mosal_release(struct inode *inode, struct file *filp)
{
	call_result_t rc;
  mosal_rsrc_t *mosal_rsrc_p = filp->private_data;

	
  if ( !filp->private_data ) {
    MTL_ERROR1(MT_FLFMT("%s: filp->private_data equals null"), __func__);
    return -EAGAIN;
  }

  MTL_DEBUG7("%s: release contmem (filp=0x%p)\n", __func__,filp);
  if ( mosal_rsrc_p->mlock_ctx ) {
    rc= MOSAL_mlock_ctx_cleanup(mosal_rsrc_p->mlock_ctx);
    if (rc != MT_OK)  {
      MTL_ERROR1(MT_FLFMT("MOSAL_unreg_mem_ctx failed, pid=%d (%s)"),
                 current->pid,mtl_strerror_sym(rc));
    }
    mosal_rsrc_p->mlock_ctx = NULL;
  }

  if ( mosal_rsrc_p->k2u_cbk_h != INVALID_K2U_CBK_HNDL ) {
    rc = k2u_cbk_cleanup(mosal_rsrc_p->k2u_cbk_h);
    if ( rc != MT_OK ) {
      MTL_ERROR1(MT_FLFMT("MOSAL_unreg_mem_ctx failed, pid=%d (%s)"),
                 current->pid,mtl_strerror_sym(rc));
    }
    mosal_rsrc_p->k2u_cbk_h = INVALID_K2U_CBK_HNDL;
  }
  FREE(mosal_rsrc_p);
  filp->private_data = NULL;
	return 0;
}



/*
*-----------------------------------------------------------------------------
*						 	mosal_chrdev_register	
*-----------------------------------------------------------------------------
* Dscr  :  Register character device for Linux OS
*-----------------------------------------------------------------------------
* Input : major
* Output: >=0 - new major, else	error
* Notes :
*-----------------------------------------------------------------------------
*/
int mosal_chrdev_register( int *major_p, char *device_name, m_file_op_t *fops_p )
{
	int new_major;

	MTL_TRACE2("%s: driver=%s num=%d\n",
		   __func__, device_name, *major_p);	
	new_major = register_chrdev(*major_p, device_name, fops_p);

	if (new_major < 0)
	{
		MTL_ERROR2("%s(%d): could not register device\n", __func__,
			   *major_p);
		return MT_ERROR;
	}
	MTL_TRACE2("%s: driver %s registered major=%d\n", __func__, device_name, new_major);	
	*major_p=new_major;

	return 0;
}


/*
*-----------------------------------------------------------------------------
*					  	mosal_chrdev_unregister   	
*-----------------------------------------------------------------------------
* Dscr  :  Unregister character device for Linux OS
*-----------------------------------------------------------------------------
* Input : major
* Output: 
* Notes :
*-----------------------------------------------------------------------------
*/
void mosal_chrdev_unregister(unsigned int major, char *device_name)
{
	int rc;
	MTL_TRACE2("%s(%d): deregistering driver\n", __func__, major);
	rc=unregister_chrdev(major, device_name );
	if (rc)
	{
		MTL_ERROR2("%s(%d): deregistering failed with code %d\n", __func__, major, rc);
	}
}

int mosal_wrap_kernel_ioctl(unsigned long ioctl_param)
{
    mosal_ioctl_wrap_t mosal_ioctl_wrap;
    int rc;
    void *pi = NULL, *po = NULL;
    u_int32_t bytes_to_return;
#define MAX_PI_SZ		8192    
#define MAX_PO_SZ		(8192*2)    

    /* get parameter list */    
    copy_from_user(&mosal_ioctl_wrap, (char*) ioctl_param , sizeof(mosal_ioctl_wrap));
    MTL_TRACE2(MT_FLFMT("fcn=%d, ioctl_num=0x%x"), (int)mosal_ioctl_wrap.ops, MT_IOCTL_CALL);

    /* sanity checks */
    if (mosal_ioctl_wrap.pi_sz > MAX_PI_SZ || mosal_ioctl_wrap.po_sz > MAX_PO_SZ) {
         MTL_ERROR2("%s: Suspiciously long buffers fcn %d, (pi_sz %d, po_sz %d).\n", __func__,
           (int)mosal_ioctl_wrap.ops, mosal_ioctl_wrap.pi_sz, mosal_ioctl_wrap.po_sz );
         rc = (int)MT_EINVAL;
         goto err_exit;
    }
    	
    /* allocate INPUT buffer */
    if (mosal_ioctl_wrap.pi_sz) {
    	pi = (void*)MALLOC(mosal_ioctl_wrap.pi_sz);
    	if (pi == NULL) {
         MTL_ERROR2("%s: Cannot allocate INPUT buffer (pi_sz %d).\n", __func__, mosal_ioctl_wrap.pi_sz);
         rc = (int)MT_EAGAIN;
         goto err_exit;
    	}  
    }
    
    /* allocate OUTPUT buffer */
    if (mosal_ioctl_wrap.po_sz) {
    	po = (void*)MALLOC(mosal_ioctl_wrap.po_sz);
    	if (po == NULL) {
         MTL_ERROR2("%s: Cannot allocate OUTPUT buffer (po_sz %d).\n", __func__, mosal_ioctl_wrap.po_sz);
         rc = (int)MT_EAGAIN;
         goto free_ibuf;
    	}  
    }
    
    /* copy INPUT buffer to kernel */
    copy_from_user(pi, mosal_ioctl_wrap.pi , mosal_ioctl_wrap.pi_sz);
    
    /* perform the function */
    rc = MOSAL_ioctl(mosal_ioctl_wrap.ops, pi, mosal_ioctl_wrap.pi_sz, po,mosal_ioctl_wrap.po_sz, &bytes_to_return);

    /* return values */
    if (bytes_to_return)
      copy_to_user(mosal_ioctl_wrap.po , po, bytes_to_return);

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
*-----------------------------------------------------------------------------
*						 	mosal_ioctl	
*-----------------------------------------------------------------------------
* Dscr  :  ioctl function for Linux OS
*-----------------------------------------------------------------------------
* Input : see ioctl description 
* Output: 0-ok, other error code
* Notes :
*-----------------------------------------------------------------------------
*/
int mosal_ioctl(struct inode* pinode, struct file * pfile, 
										unsigned int cmd, unsigned long arg)
{
	int rc;

	MTL_TRACE6("%s: got ioctl %d\n", __func__, cmd);
	
  switch ( cmd ) {
    case MT_IOCTL_CALL:
      rc = mosal_wrap_kernel_ioctl( arg );
      if( rc )
        MTL_ERROR5("%s: kern_os_unwrapper error %d\n", __func__, rc);
      return rc;

    case K2U_CBK_CBK_INIT:
      {
        k2u_cbk_hndl_t k2u_cbk_h;
        mosal_rsrc_t *mosal_rsrc_p=pfile->private_data;

        if ( mosal_rsrc_p->k2u_cbk_h != INVALID_K2U_CBK_HNDL ) {
          MTL_ERROR1(MT_FLFMT("%s: called k2u_cbk_init() more than once for the same process - pid="MT_ULONG_PTR_FMT), __func__, MOSAL_getpid());
          return MT_ERROR;
        }
        rc = k2u_cbk_init(&k2u_cbk_h);
        if ( rc != MT_OK ) {
          return rc;
        }
        mosal_rsrc_p->k2u_cbk_h = k2u_cbk_h;
        MTL_TRACE3(MT_FLFMT("%s: k2u_cbk_init() returned handle=%d"), __func__, k2u_cbk_h);
        
        copy_to_user((void *)arg, &k2u_cbk_h, sizeof(k2u_cbk_h));
        return rc;
      }
      break;

    case K2U_CBK_CBK_CLEANUP:
      {
        mosal_rsrc_t *mosal_rsrc_p=pfile->private_data;

        rc = k2u_cbk_cleanup(arg);
        if ( rc == MT_OK ) {
          if ( mosal_rsrc_p->k2u_cbk_h != INVALID_K2U_CBK_HNDL ) {
            mosal_rsrc_p->k2u_cbk_h = INVALID_K2U_CBK_HNDL;
            MTL_TRACE3(MT_FLFMT("%s: k2u_cbk_cleanup() freed handle %ld"), __func__, arg);
          }
          else {
            MTL_ERROR1(MT_FLFMT("%s: called k2u_cbk_cleanup() while there was an invalid handle in mosal resource tracking"), __func__);
          }
        }
        return rc;
      }
      break;

    default:
      return -ENOTTY;

  }
}




/*
 *  mosal_module_init
 */
static int mosal_module_init(void)
{
  return MOSAL_init(mosal_major);
}


/*
 *  mosal_module_exit
 */
static void mosal_module_exit(void)
{
  MOSAL_cleanup();
}


module_init(mosal_module_init);
module_exit(mosal_module_exit);
