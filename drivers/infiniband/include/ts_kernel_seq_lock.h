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

  $Id: ts_kernel_seq_lock.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_SEQ_LOCK_H
#define _TS_KERNEL_SEQ_LOCK_H

#include <asm/system.h>

/*
  Lockless consistency primitives, for use when reads are much more
  common than writes.  These primitives allow lockless reads; even if
  multiple CPUs are reading, there is no need to bounce a lock's
  cacheline between CPUs.  In addition, writers are not starved by
  readers.

  USAGE:

    Declaration and initialization:

      struct foo {
        // ...
        tTS_SEQ_LOCK_STRUCT seq_lock;
      };

      struct foo bar;
      tsSeqLockInit(&bar.seq_lock);

    Reading:

      unsigned int seq;

      do {
        seq = tsSeqLockReadBegin(&bar.seq_lock);
        // read from bar
      } while (tsSeqLockReadEnd(&bar.seq_lock, seq));

    Writing:

      // lock bar against other writers, if necessary
      tsSeqLockWriteBegin(&bar.seq_lock);
      // write to bar
      tsSeqLockWriteEnd(&bar.seq_lock);
      // unlock bar, if necessary

  THEORY OF OPERATION:

    A "serial number" is kept for the data.  The serial number starts
    at 0, and is incremented once before the data is written and once
    after the data is written.  Therefore, the serial number will be
    odd while the data is being changed.

    This means that if a reader makes sure that the serial numbers
    before and after reading are the same and even, no writes took
    place while reading the data.  No locks need to be taken to check
    this.  Reads by different CPUs will not invalidate cached versions
    of the serial number, so if no writes are taking place, there will
    be no "cache-line ping-pong."

    If multiple writers can touch the data, then extra (writer-only)
    locking will be required, for example a spinlock.

    The smp_rmb() and smp_wmb() calls are needed to ensure that this
    works on systems where different CPUs may not have in-order views
    of each other's memory access.
*/

typedef struct tTS_SEQ_LOCK_STRUCT {
  volatile unsigned int counter;
} tTS_SEQ_LOCK_STRUCT, *tTS_SEQ_LOCK;

#define TS_SEQ_LOCK_UNLOCKED { 0 }

static inline void tsSeqLockInit(
                                 tTS_SEQ_LOCK seq_lock
                                 ) {
  *seq_lock = (tTS_SEQ_LOCK_STRUCT) TS_SEQ_LOCK_UNLOCKED;
}

static inline unsigned int tsSeqLockReadBegin(
                                              tTS_SEQ_LOCK seq_lock
                                              ) {
  unsigned int ret = seq_lock->counter;
  /* make sure counter is read before data is read */
  smp_rmb();
  return ret;
}

static inline unsigned int tsSeqLockReadEnd(
                                            tTS_SEQ_LOCK seq_lock,
                                            unsigned int initial_value
                                            ) {
  /* make sure data is read before counter is read */
  smp_rmb();
  /* return 0 if counter == initial_value and initial_value is even */
  return (seq_lock->counter ^ initial_value) | (initial_value & 1);
}

static inline void tsSeqLockWriteBegin(
                                       tTS_SEQ_LOCK seq_lock
                                       ) {
  /* make counter odd */
  ++seq_lock->counter;
  /* make sure counter is incremented before data is written*/
  smp_wmb();
}

static inline void tsSeqLockWriteEnd(
                                     tTS_SEQ_LOCK seq_lock
                                     ) {
  /* make sure data is written before counter is incremented */
  smp_wmb();
  /* make counter even again */
  ++seq_lock->counter;
}

#endif /* _TS_KERNEL_SEQ_LOCK_H */
