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

  $Id: client_query.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _CLIENT_QUERY_H
#define _CLIENT_QUERY_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif
#endif

#include "ts_ib_mad_types.h"
#include "ts_ib_client_query.h"

int tsIbClientQueryInit(
                        void
                        );

void tsIbClientQueryCleanup(
                            void
                            );

tTS_IB_MAD_DISPATCH_FUNCTION tsIbClientAsyncMadHandlerGet(
                                                          uint8_t mgmt_class
                                                          );

void *tsIbClientAsynMadHandlerArgGet(
                                     uint8_t mgmt_class
                                     );

void tsIbClientMadHandler(
                          tTS_IB_MAD mad,
                          void *arg
                          );

#endif /* _CLIENT_QUERY_H */
