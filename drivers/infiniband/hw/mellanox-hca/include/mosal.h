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

#ifndef H_MOSAL_H
#define H_MOSAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ----- common stuff ----- */
#include <mtl_common.h>

/* ----- OS-dependent implementation ----- */
#ifndef MT_KERNEL
/* Initialization of user level library. 
 * For dynamic linking, invoked via _init/DllMain. 
 * For static linking, user/application (or another init point) must invoke this 
 * function before any invocation of of MOSAL functions.
 */
extern void MOSAL_user_lib_init(void); 
#endif

#include <mosal_prot_ctx_imp.h>
#include <mosal_prot_ctx.h>

#include <mosal_sync_imp.h>
#include <mosal_mem_imp.h>
#include <mosal_iobuf_imp.h>


#include <mosal_timer_imp.h>


#include <mosal_thread_imp.h>
#include <mosalu_socket_imp.h>


/* ----- mosal OS-specific types ----- */
#include <mosal_types.h>

/* ----- bus access ----- */
#include <mosal_bus.h>

/* ----- interrupts, DPC and timers ----- */
#include <mosal_timer.h>

/* ----- memory services ----- */
#include <mosal_arch.h>
#include <mosal_mem.h>
#include <mosal_iobuf.h>
#include <mosal_mlock.h>

/* ----- queue management ----- */
#include <mosal_que.h>

/* ----- synchronization routines ----- */
#include <mosal_sync.h>


/* ----- thread routines ----- */
#include <mosal_thread.h>

/* ----- socket routines ----- */
#include <mosalu_socket.h>


#if !defined(VXWORKS_OS)
#include <mosal_i2c.h>
#endif


#ifndef MT_KERNEL_ONLY

/* callback management */
#include <mosal_k2u_cbk.h>

#endif

/* ----- driver services ----- */
#ifdef __LINUX__
#include <mosal_driver.h>
#endif

/* ----- general services ----- */
#include <mosal_gen.h>
#ifdef __WIN__
#include <mosal_ntddk.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* H_MOSAL_H */

