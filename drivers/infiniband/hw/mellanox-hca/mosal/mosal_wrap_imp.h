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

 #ifndef MOSAL_WRAP_IMP_H
#define MOSAL_WRAP_IMP_H

#include <mosal.h>
#include <linux/ioctl.h>
#include <linux/types.h>
/*
*------------------------------------------------------------------------------
*                               defines
*------------------------------------------------------------------------------
*/
#define MT_IOCTL_MAGIC 'x'
#define MT_IOCTL_CALL _IO(MT_IOCTL_MAGIC,0)

/* ioctl numbers for k2u callback */
#define K2U_CBK_CBK_INIT    _IO(MT_IOCTL_MAGIC,1)
#define K2U_CBK_CBK_CLEANUP _IO(MT_IOCTL_MAGIC,2)

typedef struct {
    int 			ops;
    void *			pi;
    u_int32_t		pi_sz;
    void *			po;
    u_int32_t		po_sz;
} mosal_ioctl_wrap_t;

#define MOSAL_FUNC_BASE		200

#endif

