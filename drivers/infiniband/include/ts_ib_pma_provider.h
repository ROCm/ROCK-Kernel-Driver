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

  $Id: ts_ib_pma_provider.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_PMA_PROVIDER_H
#define _TS_IB_PMA_PROVIDER_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../core,core_export.ver)
#endif

#include "ts_ib_pma_provider_types.h"

int tsIbPmaDeviceRegister(tTS_IB_PMA_PROVIDER pma_dev,
                          int                 num_ports);
int tsIbPmaDeviceDeregister(tTS_IB_PMA_PROVIDER pma_dev);

tTS_IB_MAD_RESULT tsIbPmaMadProcess(tTS_IB_PMA_PROVIDER pma_dev,
                                    int                 ignore_mkey,
                                    tTS_IB_MAD          in_mad,
                                    tTS_IB_MAD          out_mad
                                   );

// A simulated device provider for example and testing.
extern tTS_IB_PMA_PROVIDER_STRUCT ts_ib_pma_sim_provider;

#endif /* _TS_IB_PMA_PROVIDER_H */
