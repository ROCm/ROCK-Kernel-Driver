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

#ifndef H_MOSAL_DRIVER_H
#define H_MOSAL_DRIVER_H


#ifdef __KERNEL__
#include <mtl_common.h>                       
#include <linux/fs.h>
typedef struct file_operations m_file_op_t;

/*
*------------------------------------------------------------------------------
*                               defines
*------------------------------------------------------------------------------
*/
#define MOSAL_CHAR_DEV_NAME "mosal"

/*
*------------------------------------------------------------------------------
*                               types
*------------------------------------------------------------------------------
*/

/*
*------------------------------------------------------------------------------
*                               Global Variables
*------------------------------------------------------------------------------
*/
extern unsigned int mosal_major;	/*  Allocated in mosal_kernel.c */


/*
*------------------------------------------------------------------------------
*                               functions
*------------------------------------------------------------------------------
*/
int mosal_chrdev_register( int *major_p, char *device_name, m_file_op_t *fops_p );
void mosal_chrdev_unregister(unsigned int major, char *device_name);
int mosal_ioctl(struct inode*, struct file *, unsigned int, unsigned long );

#endif

#endif /* H_MOSAL_DRIVER_H */

