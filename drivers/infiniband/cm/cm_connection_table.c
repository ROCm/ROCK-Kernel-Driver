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

  $Id: cm_connection_table.c,v 1.9 2004/02/25 00:35:10 roland Exp $
*/

#include "cm_priv.h"
#include "cm_packet.h"
#include "ts_ib_core.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/config.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#else
#include <os_dep/win/linux/string.h>
#include <os_dep/win/linux/spinlock.h>
#endif

enum {
  TS_IB_CM_CONNECTION_HASH_BITS = 10,
  TS_IB_CM_CONNECTION_HASH_MASK = (1 << TS_IB_CM_CONNECTION_HASH_BITS) - 1
};

#ifndef W2K_OS
static kmem_cache_t *connection_cache;
#else
static PNPAGED_LOOKASIDE_LIST connection_cache;
#endif

static uint32_t          qp_hash_initval;
static tTS_IB_CM_COMM_ID cur_comm_id;
#ifdef W2K_OS
/* XXX where is this initialized?? */
static spinlock_t        comm_id_lock;
#else
static spinlock_t        comm_id_lock = SPIN_LOCK_UNLOCKED;
#endif

static tTS_HASH_TABLE connection_table;
static tTS_HASH_TABLE remote_qp_table;
static tTS_HASH_TABLE remote_comm_id_table;

tTS_IB_CM_CONNECTION tsIbCmConnectionFind(
                                          tTS_IB_CM_COMM_ID local_comm_id
                                          ) {
  tTS_LOCKED_HASH_ENTRY entry
    = tsKernelLockedHashLookup(connection_table, local_comm_id);

  /* XXX this #if must be removed */
#ifndef W2K_OS
  return entry
    ? TS_KERNEL_HASH_ENTRY(entry, tTS_IB_CM_CONNECTION_STRUCT, entry)
    : NULL;
#else
  return entry
    ?(tTS_IB_CM_CONNECTION)((char*)entry-offsetof(tTS_IB_CM_CONNECTION_STRUCT, entry))
    : NULL;
#endif
}

tTS_IB_CM_CONNECTION tsIbCmConnectionFindInterruptible(
                                                       tTS_IB_CM_COMM_ID local_comm_id,
                                                       int              *status
                                                       ) {
  tTS_LOCKED_HASH_ENTRY entry
    = tsKernelLockedHashLookupInterruptible(connection_table, local_comm_id, status);

  /* XXX this #if must be removed */
#ifndef W2K_OS
  return entry
    ? TS_KERNEL_HASH_ENTRY(entry, tTS_IB_CM_CONNECTION_STRUCT, entry)
    : NULL;
#else
  return entry
    ?(tTS_IB_CM_CONNECTION)((char*)entry-offsetof(tTS_IB_CM_CONNECTION_STRUCT, entry))
    : NULL;
#endif
}

tTS_IB_CM_CONNECTION tsIbCmConnectionFindRemoteQp(
                                                  tTS_IB_GID port_gid,
                                                  tTS_IB_QPN qpn
                                                  ) {
  uint32_t hash;
  tTS_HASH_NODE node;
  tTS_IB_CM_COMM_ID comm_id = TS_IB_CM_COMM_ID_INVALID;
  tTS_IB_CM_CONNECTION conn;
  TS_WINDOWS_SPINLOCK_FLAGS

  hash = tsKernelHashString((uint32_t *) port_gid,
                            sizeof (tTS_IB_GID) / 4,
                            qp_hash_initval);
  hash = tsKernelHashString((uint32_t *) &qpn,
                            sizeof qpn / 4,
                            hash);
  hash &= TS_IB_CM_CONNECTION_HASH_MASK;

  spin_lock(&remote_qp_table->lock);

  /* XXX this #if must be removed */
#ifndef W2K_OS
  TS_KERNEL_HASH_FOR_EACH(node, &remote_qp_table->bucket[hash]) {
    conn = TS_KERNEL_HASH_ENTRY(node, tTS_IB_CM_CONNECTION_STRUCT, remote_qp_node);
#else
  for (node = remote_qp_table->bucket[hash].first; node; node = node->next)
   {
     conn = (tTS_IB_CM_CONNECTION)((char*)node - offsetof(tTS_IB_CM_CONNECTION_STRUCT,remote_qp_node));
#endif

    if (conn->remote_qpn == qpn &&
        !memcmp(port_gid, conn->primary_path.dgid, sizeof (tTS_IB_GID))) {
      comm_id = conn->local_comm_id;
      break;
    }
  }

  spin_unlock(&remote_qp_table->lock);

  if (comm_id == TS_IB_CM_COMM_ID_INVALID) {
    return NULL;
  }

  return tsIbCmConnectionFind(comm_id);
}

tTS_IB_CM_CONNECTION tsIbCmConnectionFindRemoteId(
                                                  tTS_IB_CM_COMM_ID remote_comm_id
                                                  ) {
  uint32_t hash;
  tTS_HASH_NODE node;
  tTS_IB_CM_COMM_ID comm_id = TS_IB_CM_COMM_ID_INVALID;
  tTS_IB_CM_CONNECTION conn;
  TS_WINDOWS_SPINLOCK_FLAGS

  hash = tsKernelHashFunction(remote_comm_id, TS_IB_CM_CONNECTION_HASH_MASK);

  spin_lock(&remote_comm_id_table->lock);

  /* XXX this #if must be removed */
#ifndef W2K_OS
  TS_KERNEL_HASH_FOR_EACH(node, &remote_comm_id_table->bucket[hash]) {
    conn = TS_KERNEL_HASH_ENTRY(node, tTS_IB_CM_CONNECTION_STRUCT, remote_comm_id_node);
#else
  for (node = remote_comm_id_table->bucket[hash].first; node; node = node->next)
   {
     conn = (tTS_IB_CM_CONNECTION)((char *)node - offsetof(tTS_IB_CM_CONNECTION_STRUCT,remote_comm_id_node));
#endif

    if (conn->remote_comm_id == remote_comm_id) {
      comm_id = conn->local_comm_id;
      break;
    }
  }

  spin_unlock(&remote_comm_id_table->lock);

  if (comm_id == TS_IB_CM_COMM_ID_INVALID) {
    return NULL;
  }

  return tsIbCmConnectionFind(comm_id);
}

tTS_IB_CM_CONNECTION tsIbCmConnectionNew(
                                         void
                                         ) {
  TS_WINDOWS_SPINLOCK_FLAGS
  /* XXX this #if must be removed */
#ifndef W2K_OS
  tTS_IB_CM_CONNECTION conn = kmem_cache_alloc(connection_cache, GFP_KERNEL);
#else
  tTS_IB_CM_CONNECTION conn;
  conn = ExAllocateFromNPagedLookasideList(connection_cache);
#endif

  if (!conn) {
    return NULL;
  }

  spin_lock(&comm_id_lock);

  while (++cur_comm_id == TS_IB_CM_COMM_ID_INVALID) { /* nothing */ }
  conn->local_comm_id  = cur_comm_id;
  conn->entry.key      = cur_comm_id;
  conn->timer.arg      = (void *) (unsigned long) cur_comm_id;

  spin_unlock(&comm_id_lock);

  conn->peer_to_peer_service = NULL;
  conn->local_qp             = TS_IB_HANDLE_INVALID;
  conn->callbacks_running    = 0;
  conn->lap_pending          = 0;
  conn->establish_pending    = 0;

  tsKernelLockedHashStore(connection_table, &conn->entry);

  return conn;
}

void tsIbCmConnectionInsertRemote(
                                  tTS_IB_CM_CONNECTION connection
                                  ) {
  uint32_t hash;
  TS_WINDOWS_SPINLOCK_FLAGS

  hash = tsKernelHashString((uint32_t *) connection->primary_path.dgid,
                            sizeof (tTS_IB_GID) / 4,
                            qp_hash_initval);
  hash = tsKernelHashString((uint32_t *) &connection->remote_qpn,
                            sizeof connection->remote_qpn / 4,
                            hash);
  hash &= TS_IB_CM_CONNECTION_HASH_MASK;

  spin_lock(&remote_qp_table->lock);

  tsKernelHashNodeAdd(&connection->remote_qp_node,
                      &remote_qp_table->bucket[hash]);

  spin_unlock(&remote_qp_table->lock);

  hash = tsKernelHashFunction(connection->remote_comm_id, TS_IB_CM_CONNECTION_HASH_MASK);

  spin_lock(&remote_comm_id_table->lock);

  tsKernelHashNodeAdd(&connection->remote_comm_id_node,
                      &remote_comm_id_table->bucket[hash]);

  spin_unlock(&remote_comm_id_table->lock);
}

void tsIbCmConnectionFree(
                          tTS_IB_CM_CONNECTION connection
                          ) {
  TS_WINDOWS_SPINLOCK_FLAGS
  tsKernelTimerRemove(&connection->timer);
  tsKernelLockedHashRemove(connection_table, &connection->entry);

  if (!tsKernelHashNodeUnhashed(&connection->remote_qp_node)) {
    spin_lock(&remote_qp_table->lock);
    tsKernelHashNodeRemove(&connection->remote_qp_node);
    spin_unlock(&remote_qp_table->lock);
  }

  if (connection->peer_to_peer_service) {
    connection->peer_to_peer_service->peer_to_peer_comm_id = TS_IB_CM_COMM_ID_INVALID;
    connection->peer_to_peer_service = NULL;
  }

  /* XXX this #if must be removed */
#ifndef W2K_OS
  kmem_cache_free(connection_cache, connection);
#else
  ExFreeToNPagedLookasideList(connection_cache,connection);
#endif
}

static void _tsIbCmConnectionConstructor(
                                         void *connection_ptr,
  /* XXX this #if must be removed */
#ifndef W2K_OS
                                         kmem_cache_t *cache,
#else
                                         PNPAGED_LOOKASIDE_LIST cache,
#endif
                                         unsigned long flags
                                         ) {
  tTS_IB_CM_CONNECTION connection = connection_ptr;

  tsKernelTimerInit(&connection->timer);

  connection->mad.sqpn                  = TS_IB_GSI_QP;
  connection->mad.completion_func       = NULL;
  connection->remote_qp_node.pprev      = NULL;
  connection->remote_comm_id_node.pprev = NULL;
  connection->cm_function               = NULL;
  connection->cm_arg                    = NULL;
}

void tsIbCmConnectionTableInit(
                               void
                               ) {
  get_random_bytes(&qp_hash_initval, sizeof qp_hash_initval);
  get_random_bytes(&cur_comm_id,     sizeof cur_comm_id);

#define TS_WORD_ALIGN(x) (((x) + sizeof (void *) - 1) & ~(sizeof (void *) - 1))
  /* XXX this #if must be removed */
#ifndef W2K_OS
  connection_cache = kmem_cache_create("ib_cm_conn",
                                       TS_WORD_ALIGN(sizeof (tTS_IB_CM_CONNECTION_STRUCT)),
                                       0,
                                       SLAB_HWCACHE_ALIGN,
                                       _tsIbCmConnectionConstructor,
                                       NULL);
#else
   connection_cache = kmalloc(sizeof(NPAGED_LOOKASIDE_LIST),NULL);
   ExInitializeNPagedLookasideList(connection_cache, NULL, NULL, 0,
                              sizeof(tTS_IB_CM_CONNECTION_STRUCT),'mc',0);
#endif

  if (tsKernelHashTableCreate(TS_IB_CM_CONNECTION_HASH_BITS, &connection_table)) {
    TS_REPORT_FATAL(MOD_IB_CM,
                    "Couldn't allocate main connection hash table");
  }

  if (tsKernelHashTableCreate(TS_IB_CM_CONNECTION_HASH_BITS, &remote_comm_id_table)) {
    TS_REPORT_FATAL(MOD_IB_CM,
                    "Couldn't allocate remote comm ID connection hash table");
  }

  if (tsKernelHashTableCreate(TS_IB_CM_CONNECTION_HASH_BITS, &remote_qp_table)) {
    TS_REPORT_FATAL(MOD_IB_CM,
                    "Couldn't allocate remote QP connection hash table");
  }
}

void tsIbCmConnectionTableCleanup(
                                  void
                                  ) {
  /* XXX this #if must be removed */
#ifndef W2K_OS
  if (kmem_cache_destroy(connection_cache)) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "Failed to destroy CM connection slab cache (memory leak?)");
  }
#else
  ExDeleteNPagedLookasideList(connection_cache);
#endif
}
