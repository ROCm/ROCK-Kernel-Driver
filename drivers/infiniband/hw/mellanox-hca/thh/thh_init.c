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
 
#include <mtl_common.h>
#include <hh.h>
#include <thh.h>
#include <thh_init.h>

extern void  THH_cqm_init(void);
extern void  THH_qpm_init(void);
extern void  THH_mrwm_init(void);

HH_ret_t    THH_init(void)
{
     /* This function should be called by the DDK entry point, to perform any module initializations */
     /* that are global (and not per HCA).  In Linux, for example, the DDK entry point is "init_module". */

     THH_cqm_init();
     THH_qpm_init();
     THH_mrwm_init();
     return(HH_OK);
}


HH_ret_t    THH_cleanup(void)
{
     /* This function should be called by the DDK exit point, to perform any module initializations */
     /* that are global (and not per HCA).  In Linux, for example, the DDK exit point is "cleanup_module". */
     return(HH_OK);
}


