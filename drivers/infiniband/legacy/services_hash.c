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

  $Id: services_hash.c 32 2004-04-09 03:57:42Z roland $
*/

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include "ts_kernel_services.h"
#include "ts_kernel_trace.h"
#include "ts_kernel_hash.h"

#include <linux/slab.h>
#include <linux/random.h>

uint32_t jenkins_hash_initval;

int tsKernelHashTableCreate(
                            int             hash_bits,
                            tTS_HASH_TABLE *table
                            ) {
  int hash_size;
  tTS_HASH_TABLE new_table;
  int i;

  if (hash_bits > 16) {
    TS_REPORT_WARN(MOD_SYS,
                   "hash_bits %d is too big",
                   hash_bits);
    return -EINVAL;
  }

  /* Initialize our seed to a non-zero value */
  while (jenkins_hash_initval == 0) {
    get_random_bytes(&jenkins_hash_initval, sizeof jenkins_hash_initval);
  }

  hash_size = 1 << hash_bits;

  new_table = kmalloc(sizeof *new_table + hash_size * sizeof *new_table->bucket,
                      GFP_KERNEL);

  if (!new_table) {
    return -ENOMEM;
  }

  new_table->hash_mask = hash_size - 1;
  spin_lock_init(&new_table->lock);

  for (i = 0; i < hash_size; ++i) {
    tsKernelHashHeadInit(&new_table->bucket[i]);
  }

  *table = new_table;
  return 0;
}

int tsKernelHashTableDestroy(
                             tTS_HASH_TABLE table
                             ) {
  kfree(table);
  return 0;
}
