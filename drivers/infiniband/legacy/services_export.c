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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: services_export.c 32 2004-04-09 03:57:42Z roland $
*/

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"
#include "ts_kernel_thread.h"
#include "ts_kernel_hash.h"

#define __NO_VERSION__
#include <linux/module.h>

/* services_main.c */
EXPORT_SYMBOL(tsKernelProcDirGet);
EXPORT_SYMBOL_NOVERS(mcount);

/* services_trace.c */
EXPORT_SYMBOL(tsKernelTrace);
EXPORT_SYMBOL(tsKernelTraceLevelSet);
EXPORT_SYMBOL(tsKernelTraceFlowMaskSet);

/* services_thread.c */
EXPORT_SYMBOL(tsKernelThreadStart);
EXPORT_SYMBOL(tsKernelThreadStop);
EXPORT_SYMBOL(tsKernelQueueThreadStart);
EXPORT_SYMBOL(tsKernelQueueThreadStop);
EXPORT_SYMBOL(tsKernelQueueThreadAdd);

/* services_hash.c */
EXPORT_SYMBOL_NOVERS(jenkins_hash_initval);
EXPORT_SYMBOL(tsKernelHashTableCreate);
EXPORT_SYMBOL(tsKernelHashTableDestroy);
