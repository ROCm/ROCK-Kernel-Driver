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

  $Id: ts_kernel_poll.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_POLL_H
#define _TS_KERNEL_POLL_H

#ifndef W2K_OS // Vipul
#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,poll_export.ver)
#endif
#endif // W2K_OS
enum {
  TS_KERNEL_POLL_NAME_LENGTH = 16
};

/**
   Opaque type for functions registered with poll loop.
*/
typedef struct tTS_KERNEL_POLL_HANDLE_STRUCT tTS_KERNEL_POLL_HANDLE_STRUCT,
  *tTS_KERNEL_POLL_HANDLE;

/**
   Type for functions registered with poll loop.  Return value is used
   analogously to interrupt count; it is added to a running total, and
   the table of totals is accessible through /proc/topspin/poll_counts.
*/
typedef int (*tTS_KERNEL_POLL_FUNCTION)(void *arg);

/**
   Register a function to be polled from poll loop.

   @param name Name for poll function; used to display work total.
   @param function Function to poll
   @param arg Passed to poll function
   @param handle Handle to use to remove function

   @return error code (0 if successful)
*/
int tsKernelPollRegister(
                         const char *name,
                         tTS_KERNEL_POLL_FUNCTION function,
                         void *arg,
                         tTS_KERNEL_POLL_HANDLE *handle
                         );

/**
   Remove a function from the list of functions polled from the poll
   loop.  Note: tsKernelPollFree() should not be called from the
   function being freed.
*/
void tsKernelPollFree(
                      tTS_KERNEL_POLL_HANDLE handle
                      );

#endif /* _TS_KERNEL_POLL_H */
