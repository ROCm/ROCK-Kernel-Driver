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

#define C_HH_C


#include <hh.h>
#include <hh_init.h>

static  HH_hca_dev_t         HH_hca_dev_tbl[MAX_HCA_DEV_NUM];
static  HH_hca_dev_t* const  devBegin = &HH_hca_dev_tbl[0];
static  HH_hca_dev_t* const  devEnd   = &HH_hca_dev_tbl[0] + MAX_HCA_DEV_NUM;

static  HH_if_ops_t  invalid_ops;
static  HH_if_ops_t  zombie_ops;

/* forward declarations */
static HH_hca_dev_t* find_free_dev(void);
static void          init_trivial_ops(void);


/************************************************************************/
/* To ease debugging, hook for trivial return statement.
 * Otherwise, calling stack Thru macros may be hard to get,\.
 */
static inline HH_ret_t  self_return(HH_ret_t rc)
{ return rc; }



/************************************************************************/
HH_ret_t HH_add_hca_dev(
  HH_hca_dev_t*   dev_info,
  HH_hca_hndl_t*  hca_hndl_p
)
{
  HH_ret_t       rc = HH_EAGAIN;
  HH_hca_dev_t*  dev = find_free_dev();
  MTL_TRACE4(MT_FLFMT("HH_add_hca_dev"));
  if (dev != devEnd)
  {
    rc = HH_OK;
    *dev = *dev_info;  /* the whole struct */
    MTL_DEBUG4(MT_FLFMT("dev=0x%p, desc=%s res_sz:{hca="SIZE_T_FMT", pd="SIZE_T_FMT", cq="SIZE_T_FMT", qp="SIZE_T_FMT"}"),
               dev, dev->dev_desc,
               dev->hca_ul_resources_sz, dev->pd_ul_resources_sz,
               dev->cq_ul_resources_sz,  dev->qp_ul_resources_sz);
   *hca_hndl_p = dev;
  }
  return rc;
} /* HH_add_hca_dev */


/************************************************************************/
HH_ret_t  HH_rem_hca_dev(HH_hca_hndl_t hca_hndl)
{
   HH_ret_t  rc = ((devBegin <= hca_hndl) && (hca_hndl < devEnd) &&
                   (hca_hndl->if_ops != NULL)
                   ? HH_OK : HH_ENODEV);
   if (rc == HH_OK)
   {
      hca_hndl->status = HH_HCA_STATUS_ZOMBIE;
      hca_hndl->if_ops = &zombie_ops;
   }
   return rc;
} /* HH_rem_hca_dev */


/************************************************************************/
HH_ret_t HH_list_hcas(u_int32_t       buf_entries,
                      u_int32_t*      num_of_hcas_p,
                      HH_hca_hndl_t*  hca_list_buf_p)
{
  HH_ret_t            rc = ((hca_list_buf_p != NULL) || (buf_entries == 0)
                       ? HH_OK : HH_EINVAL);
  u_int32_t      nActual = 0;
  HH_hca_dev_t*  dev = devBegin;

  MTL_DEBUG4(MT_FLFMT("HH_list_hcas: buf_entries=%d, N="VIRT_ADDR_FMT", devBegin=0x%p"), 
             buf_entries, devEnd-devBegin, devBegin);
  for (;  dev != devEnd;  ++dev)
  {
    HH_if_ops_t*  dev_if_ops = dev->if_ops;
    if ((dev_if_ops != &invalid_ops) && (dev_if_ops != &zombie_ops) )
    {
      if (buf_entries <= nActual)  /*  ==  even would be sufficient... */
      {
        hca_list_buf_p = NULL; /* user supplied buffer exceeded */
        rc = HH_EAGAIN;
      }
      ++nActual;
      if (hca_list_buf_p)
      {
        MTL_DEBUG4(MT_FLFMT("nActual=%d, dev=%p"), nActual, dev);
        *hca_list_buf_p++ = dev;
        MTL_DEBUG4(MT_FLFMT("dev=0x%p, res_sz:{hca="SIZE_T_FMT", pd="SIZE_T_FMT", cq="SIZE_T_FMT", qp="SIZE_T_FMT"}"),
                   dev, dev->hca_ul_resources_sz, dev->pd_ul_resources_sz,
                        dev->cq_ul_resources_sz,  dev->qp_ul_resources_sz);
      }
    }
  }
  *num_of_hcas_p = nActual; /* valid result, even if rc != HH_OK */
  return rc;
} /* HH_list_hcas */

#ifdef IVAPI_THH
/************************************************************************/
HH_ret_t HH_lookup_hca(const char * name, HH_hca_hndl_t* hca_handle_p)
{
  HH_hca_dev_t*  dev;

  if (!hca_handle_p)
    return HH_EINVAL;

  for (dev = devBegin;  dev != devEnd;  ++dev)
  {
    HH_if_ops_t*  dev_if_ops = dev->if_ops;
    if ((dev_if_ops != &invalid_ops) && (dev_if_ops != &zombie_ops) )
        {
          if (!strcmp(name, dev->dev_desc)) {
            *hca_handle_p = dev;
            return HH_OK;
          }
        }
  }
  return HH_ENODEV;
}
#endif

/************************************************************************/
/************************************************************************/
/**                   Internal functions                               **/

/************************************************************************/
static HH_hca_dev_t* find_free_dev()
{
  HH_hca_dev_t*  dev = devBegin;
  while ((dev != devEnd) && (dev->if_ops != &invalid_ops))
  {
    ++dev;
  }
  MTL_TRACE4("%s[%d]%s(): devB=%p, devE=%p, dev=%p\n", 
             __FILE__, __LINE__, __FUNCTION__, devBegin, devEnd, dev);
  return dev;
} /* find_free_dev */


#include "invalid.ic"
#include "zombie.ic"
#include "hhenosys.ic"

/************************************************************************/
void HH_ifops_tbl_set_enosys(HH_if_ops_t* tbl)
{
   enosys_init(tbl);
}


/************************************************************************/
static void  init_trivial_ops(void)
{
  static int  beenThereDoneThat = 0;
  if (beenThereDoneThat == 0)
  {
    beenThereDoneThat = 1;
    invalid_init(&invalid_ops);
    zombie_init(&zombie_ops);
  }
} /* init_trivial_ops */



/* Dummy function */
HH_ret_t HHIF_dummy()
{
  return(HH_OK);
}


#ifdef MT_KERNEL


MODULE_LICENSE("GPL");
int init_hh_driver(void)
{
  HH_hca_dev_t*  dev = devBegin;
  MTL_TRACE('1', "%s: installing hh_mod\n", __FUNCTION__);
  init_trivial_ops();
  while (dev != devEnd)
  {
    dev->if_ops = &invalid_ops;
    ++dev;
  }
  return(0);
}

void cleanup_hh_driver(void)
{
	MTL_TRACE('1', "%s: remove hh_mod\n", __FUNCTION__);
	return;
}

#ifndef VXWORKS_OS

int init_module(void)
{
    return(init_hh_driver());
}

void cleanup_module(void)
{
    cleanup_hh_driver();
    return;
}

#endif

#endif

