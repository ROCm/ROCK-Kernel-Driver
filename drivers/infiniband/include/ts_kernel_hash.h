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

  $Id: ts_kernel_hash.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_KERNEL_HASH_H
#define _TS_KERNEL_HASH_H

#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(..,services_export.ver)
#endif

#include <linux/spinlock.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>

#include <asm/semaphore.h>
#include <asm/atomic.h>

typedef struct tTS_HASH_HEAD_STRUCT tTS_HASH_HEAD_STRUCT,
  *tTS_HASH_HEAD;
typedef struct tTS_HASH_NODE_STRUCT tTS_HASH_NODE_STRUCT,
  *tTS_HASH_NODE;
typedef struct tTS_HASH_TABLE_STRUCT tTS_HASH_TABLE_STRUCT,
  *tTS_HASH_TABLE;
typedef struct tTS_HASH_ENTRY_STRUCT tTS_HASH_ENTRY_STRUCT,
  *tTS_HASH_ENTRY;
typedef struct tTS_LOCKED_HASH_ENTRY_STRUCT tTS_LOCKED_HASH_ENTRY_STRUCT,
  *tTS_LOCKED_HASH_ENTRY;

struct tTS_HASH_HEAD_STRUCT {
  tTS_HASH_NODE first;
};

struct tTS_HASH_NODE_STRUCT {
  tTS_HASH_NODE  next;
  tTS_HASH_NODE *pprev;
};

struct tTS_HASH_TABLE_STRUCT {
  spinlock_t           lock;
  uint32_t             hash_mask;

  /* C99 standard says to use hash[] but gcc 2.95 gives an
     'incomplete type' error */
  tTS_HASH_HEAD_STRUCT bucket[0];
};

/* We define a macro so that we guarantee tTS_HASH_ENTRY_STRUCT and
   tTS_LOCKED_HASH_ENTRY_STRUCT start with the same members (so that
   we can use tTS_HASH_ENTRY functions on tTS_LOCKED_HASH_ENTRY). */
#define TS_HASH_STRUCT_MEMBERS \
  uint32_t             key; \
  tTS_HASH_NODE_STRUCT node

struct tTS_HASH_ENTRY_STRUCT {
  TS_HASH_STRUCT_MEMBERS;
};

struct tTS_LOCKED_HASH_ENTRY_STRUCT {
  TS_HASH_STRUCT_MEMBERS;

  atomic_t         waiters;
  int              freeing;
  struct semaphore mutex;
};

#define TS_KERNEL_HASH_ENTRY(ptr, type, member) ({      \
  const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
  (type *)( (char *)__mptr - offsetof(type,member) );})

#define TS_KERNEL_HASH_FOR_EACH(pos, head) \
  for (pos = (head)->first; pos; pos = pos->next)

#define TS_KERNEL_HASH_FOR_EACH_ENTRY(pos, head, member) \
  for (pos = TS_KERNEL_HASH_ENTRY((head)->first, typeof(*pos), member);   \
       &pos->member;                                                      \
       pos = TS_KERNEL_HASH_ENTRY(pos->member.next, typeof(*pos), member))

/* The following hash function is based on
   <http://www.burtleburtle.net/bob/c/lookup2.c> */

#define TS_KERNEL_HASH_MIX(a,b,c) \
{ \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8); \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12);  \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5); \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

#define TS_KERNEL_HASH_GOLDEN_RATIO 0x9e3779b9

extern uint32_t jenkins_hash_initval;

static inline uint32_t tsKernelHashFunction(
                                            uint32_t key,
                                            uint32_t mask
                                            ) {
  uint32_t b = 0x9e3779b9;
  uint32_t c = jenkins_hash_initval;

  key += TS_KERNEL_HASH_GOLDEN_RATIO;

  TS_KERNEL_HASH_MIX(key, b, c);

  return c & mask;
}

static inline uint32_t tsKernelHashString(
                                          uint32_t *key,
                                          int length,
                                          uint32_t initval
                                          ) {
  uint32_t a, b, c, len;

  a   = TS_KERNEL_HASH_GOLDEN_RATIO;
  b   = TS_KERNEL_HASH_GOLDEN_RATIO;
  c   = initval;
  len = length;

  while (len >= 3) {
    a += key[0];
    b += key[1];
    c += key[2];
    TS_KERNEL_HASH_MIX(a, b, c);
    key += 3;
    len -= 3;
  }

  c += length * 4;

  switch (len) {
  case 2: b += key[1];
  case 1: a += key[0];
  };

  TS_KERNEL_HASH_MIX(a, b, c);

  return c;
}

static inline void tsKernelHashHeadInit(
                                        tTS_HASH_HEAD head
                                        ) {
  head->first = NULL;
}

static inline void tsKernelHashNodeAdd(
                                       tTS_HASH_NODE node,
                                       tTS_HASH_HEAD head
                                       ) {
  node->next = head->first;
  if (head->first) {
    head->first->pprev = &node->next;
  }
  head->first = node;
  node->pprev = &head->first;
}

static inline void tsKernelHashNodeRemove(
                                          tTS_HASH_NODE node
                                          ) {
  tTS_HASH_NODE  next  = node->next;
  tTS_HASH_NODE *pprev = node->pprev;

  *pprev = next;
  if (next) {
    next->pprev = pprev;
  }
  node->pprev = NULL;
}

static inline int tsKernelHashNodeUnhashed(
                                           tTS_HASH_NODE node
                                           ) {
  return !node->pprev;
}

int tsKernelHashTableCreate(
                            int             hash_bits,
                            tTS_HASH_TABLE *table
                            );

int tsKernelHashTableDestroy(
                             tTS_HASH_TABLE table
                             );

/**
   Store a key in a hash table.  The caller should only initialize the
   key member of the hash entry.
*/
static inline void tsKernelHashStore(
                                     tTS_HASH_TABLE table,
                                     tTS_HASH_ENTRY entry
                                     ) {
  unsigned long flags;
  uint32_t hash_val = tsKernelHashFunction(entry->key, table->hash_mask);

  spin_lock_irqsave(&table->lock, flags);
  tsKernelHashNodeAdd(&entry->node, &table->bucket[hash_val]);
  spin_unlock_irqrestore(&table->lock, flags);
}

/**
   Look up a key in a hash table without taking the hash table's
   lock.  Should be used only if lock is already held.
*/

static inline tTS_HASH_ENTRY tsKernelHashLookupUnlocked(
                                                        tTS_HASH_TABLE table,
                                                        uint32_t       key
                                                        ) {
  uint32_t hash_val = tsKernelHashFunction(key, table->hash_mask);
  tTS_HASH_ENTRY entry;

  TS_KERNEL_HASH_FOR_EACH_ENTRY(entry, &table->bucket[hash_val], node) {
    if (entry->key == key) {
      return entry;
    }
  }

  return NULL;
}

/**
   Look up a key in a hash table.
*/
static inline tTS_HASH_ENTRY tsKernelHashLookup(
                                                tTS_HASH_TABLE table,
                                                uint32_t       key
                                                ) {
  unsigned long flags;
  tTS_HASH_ENTRY entry;

  spin_lock_irqsave(&table->lock, flags);
  entry = tsKernelHashLookupUnlocked(table, key);
  spin_unlock_irqrestore(&table->lock, flags);

  return entry;
}

/**
   Remove an entry from a hash table.
*/
static inline void tsKernelHashRemove(
                                      tTS_HASH_TABLE table,
                                      tTS_HASH_ENTRY entry
                                      ) {
  unsigned long flags;

  spin_lock_irqsave(&table->lock, flags);
  tsKernelHashNodeRemove(&entry->node);
  spin_unlock_irqrestore(&table->lock, flags);
}

/**
   Store a key in a hash table.  The caller should only initialize the
   key member of the hash entry.  When this function returns, the
   caller holds the lock on the entry and should call
   tsKernelHashEntryRelease() when ready.
*/
static inline void tsKernelLockedHashStore(
                                           tTS_HASH_TABLE table,
                                           tTS_LOCKED_HASH_ENTRY entry
                                           ) {
  atomic_set(&entry->waiters, 0);
  entry->freeing = 0;
  init_MUTEX_LOCKED(&entry->mutex);

  tsKernelHashStore(table, (tTS_HASH_ENTRY) entry);
}

/**
   Look up a key in a hash table.  If found, lock the hash entry and
   return it; else return NULL.  This function may not be called from
   interrupt context, since it takes the entry's mutex and may sleep.
*/
static inline tTS_LOCKED_HASH_ENTRY tsKernelLockedHashLookup(
                                                             tTS_HASH_TABLE table,
                                                             uint32_t       key
                                                             ) {
  unsigned long flags;
  tTS_LOCKED_HASH_ENTRY entry;

  spin_lock_irqsave(&table->lock, flags);
  entry = (tTS_LOCKED_HASH_ENTRY) tsKernelHashLookupUnlocked(table, key);
  if (!entry) {
    goto out;
  }
  atomic_inc(&entry->waiters);
  spin_unlock_irqrestore(&table->lock, flags);

  down(&entry->mutex);

  if (!entry->freeing) {
    atomic_dec(&entry->waiters);
    return entry;
  } else {
    up(&entry->mutex);
    atomic_dec(&entry->waiters);
    return NULL;
  }

 out:
  spin_unlock_irqrestore(&table->lock, flags);
  return NULL;
}

/**
   Look up a key in a hash table.  If found, lock the hash entry and
   return it; else return NULL.  This function may not be called from
   interrupt context, since it takes the entry's mutex and may sleep.
   This version of the function may be interrupted by a signal.  If it
   is interrupted, it will return NULL and set status to -EINTR.
*/
static inline tTS_LOCKED_HASH_ENTRY tsKernelLockedHashLookupInterruptible(
                                                                          tTS_HASH_TABLE table,
                                                                          uint32_t       key,
                                                                          int           *status
                                                                          ) {
  int ret;
  unsigned long flags;
  tTS_LOCKED_HASH_ENTRY entry;

  spin_lock_irqsave(&table->lock, flags);
  entry = (tTS_LOCKED_HASH_ENTRY) tsKernelHashLookupUnlocked(table, key);
  if (!entry) {
    goto out;
  }
  atomic_inc(&entry->waiters);
  spin_unlock_irqrestore(&table->lock, flags);

  ret = down_interruptible(&entry->mutex);

  if (!entry->freeing && !ret) {
    *status = 0;
    atomic_dec(&entry->waiters);
    return entry;
  } else {
    if (ret) {
      *status = -EINTR;
    } else {
      *status = 0;
      up(&entry->mutex);
    }
    atomic_dec(&entry->waiters);
    return NULL;
  }

 out:
  spin_unlock_irqrestore(&table->lock, flags);
  return NULL;
}

/**
   Release the lock on a hash entry.
*/
static inline void tsKernelLockedHashRelease(
                                             tTS_LOCKED_HASH_ENTRY entry
                                             ) {
  up(&entry->mutex);
}

/**
   Remove an entry from a hash table.  The caller must hold the lock
   on the entry.  Once this function returns, the entry is unlocked
   and should be freed immediately.  This function may sleep and
   cannot be called from interrupt context.
*/
static inline void tsKernelLockedHashRemove(
                                            tTS_HASH_TABLE table,
                                            tTS_LOCKED_HASH_ENTRY entry
                                            ) {
  tsKernelHashRemove(table, (tTS_HASH_ENTRY) entry);
  entry->freeing = 1;
  up(&entry->mutex);

  while (atomic_read(&entry->waiters)) {
    set_current_state(TASK_RUNNING);
    schedule();
  }
}

#endif /* _TS_KERNEL_HASH_H */
