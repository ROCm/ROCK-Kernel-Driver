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

  $Id: ts_ib_magic.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_MAGIC_H
#define _TS_IB_MAGIC_H

#include "ts_kernel_trace.h"

#define TS_IB_MAGIC_INVALID  0xbadf00d
#define TS_IB_MAGIC_DEVICE   0x11f11f
#define TS_IB_MAGIC_PD       0x22f11f
#define TS_IB_MAGIC_ADDRESS  0x33f11f
#define TS_IB_MAGIC_QP       0x44f11f
#define TS_IB_MAGIC_CQ       0x55f11f
#define TS_IB_MAGIC_MR       0x66f11f
#define TS_IB_MAGIC_FMR      0x77f11f
#define TS_IB_MAGIC_FMR_POOL 0x88f11f
#define TS_IB_MAGIC_ASYNC    0x99f11f
#define TS_IB_MAGIC_FILTER   0xaaf11f
#define TS_IB_MAGIC_SMA      0xbbf11f
#define TS_IB_MAGIC_PMA      0xccf11f

#define TS_IB_DECLARE_MAGIC                                                \
    unsigned long magic;
#define TS_IB_GET_MAGIC(ptr)                                               \
    (*(unsigned long *) (ptr))
#define TS_IB_SET_MAGIC(ptr, type)                                         \
    do {                                                                   \
      TS_IB_GET_MAGIC(ptr) = TS_IB_MAGIC_##type;                           \
    } while (0)
#define TS_IB_CLEAR_MAGIC(ptr)                                             \
    do {                                                                   \
      TS_IB_GET_MAGIC(ptr) = TS_IB_MAGIC_INVALID;                          \
    } while (0)
#define TS_IB_CHECK_MAGIC(ptr, type)                                       \
    do {                                                                   \
      if (!ptr) {                                                          \
        return -EINVAL;                                                    \
      }                                                                    \
      if (TS_IB_GET_MAGIC(ptr) != TS_IB_MAGIC_##type) {                    \
        TS_REPORT_WARN(MOD_KERNEL_IB, "Bad magic 0x%lx at %p for %s",      \
                       TS_IB_GET_MAGIC(ptr), ptr, #type);                  \
        return -EINVAL;                                                    \
      }                                                                    \
    } while (0)
#define TS_IB_TEST_MAGIC(ptr, type)                                        \
    (TS_IB_GET_MAGIC(ptr) == TS_IB_MAGIC_##type)

#endif /* _TS_IB_MAGIC_H */
