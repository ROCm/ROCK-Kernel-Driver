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
#define C_HHUL_C








#include <hhul.h>
#include <thhul_hob.h>
/* add includes for other Mellanox devices here */



extern HH_ret_t  HHUL_alloc_hca_hndl
(
  u_int32_t	    vendor_id,
  u_int32_t	    device_id,
  void*             hca_ul_resources_p,
  HHUL_hca_hndl_t*  hhul_hca_hndl_p
)
{
  HH_ret_t  rc = HH_ENODEV;
  if (vendor_id == MT_MELLANOX_IEEE_VENDOR_ID) {
        /* Tavor (InfiniHost) */
        rc = THHUL_hob_create(hca_ul_resources_p, device_id, hhul_hca_hndl_p);
  } else { /* unknown vendor */
    return HH_ENOSYS;
  }


  if (rc == HH_OK)
  {
     struct HHUL_hca_dev_st*  p = *hhul_hca_hndl_p;
     p->vendor_id = vendor_id;
     p->dev_id    = device_id;
  }
  return rc;
} /* HHUL_alloc_hca_hndl */


#include "hhulenosys.ic"

void HHUL_ifops_tbl_set_enosys(HHUL_if_ops_t* tbl)
{
   enosys_init(tbl);
}


