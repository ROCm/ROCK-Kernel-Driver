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

  $Id: sa_client_multicast.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sa_client.h"

#include "ts_ib_core.h"
#include "ts_ib_mad.h"
#include "ts_ib_rmpp_mad_types.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

typedef struct tTS_IB_SA_MULTICAST_MEMBER_STRUCT tTS_IB_SA_MULTICAST_MEMBER_STRUCT,
  *tTS_IB_SA_MULTICAST_MEMBER;

typedef struct tTS_IB_SA_MULTICAST_MEMBER_QUERY_STRUCT tTS_IB_SA_MULTICAST_MEMBER_QUERY_STRUCT,
  *tTS_IB_SA_MULTICAST_MEMBER_QUERY;

typedef struct tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY_STRUCT tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY_STRUCT,
  *tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY;
#ifdef W2K_OS // Vipul
  #pragma pack (push, 1)
#endif

struct tTS_IB_SA_MULTICAST_MEMBER_STRUCT {
  tTS_IB_GID    mgid;
  tTS_IB_GID    port_gid;
  tTS_IB_QKEY   qkey;
  tTS_IB_LID    mlid;
  uint8_t       mtu;
  uint8_t       tclass;
  tTS_IB_PKEY   pkey;
  uint8_t       rate;
  uint8_t       packet_life;
  uint32_t      sl_flowlabel_hoplimit;
  uint8_t       scope_joinstate;
  uint8_t       reserved[3];
} __attribute__((packed));

#ifdef W2K_OS // Vipul
#pragma pack (pop)
#endif

struct tTS_IB_SA_MULTICAST_MEMBER_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_MULTICAST_JOIN_COMPLETION_FUNC     completion_func;
  void *                                    completion_arg;
};

struct tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_MCAST_GROUP_QUERY_COMPLETION_FUNC  completion_func;
  void *                                    completion_arg;
};

static void _tsIbMulticastJoinResponse(
                                       tTS_IB_CLIENT_RESPONSE_STATUS status,
                                       tTS_IB_MAD packet,
                                       void *query_ptr
                                       ) {
  tTS_IB_SA_MULTICAST_MEMBER_QUERY query = query_ptr;

  switch (status) {
  case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &packet->payload;
      tTS_IB_SA_MULTICAST_MEMBER mc_ptr = (tTS_IB_SA_MULTICAST_MEMBER) sa_payload->admin_data;
      tTS_IB_MULTICAST_MEMBER_STRUCT mc_member;

      memcpy(mc_member.mgid, mc_ptr->mgid, sizeof mc_member.mgid);

      mc_member.qkey        = be32_to_cpu(mc_ptr->qkey);
      mc_member.mlid        = be16_to_cpu(mc_ptr->mlid);

      if (mc_member.mlid < 0xc000 || mc_member.mlid > 0xfffe) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Multicast join succeeded but returned MLID 0x%04x",
                       mc_member.mlid);
      }

      mc_member.mtu         = mc_ptr->mtu & 0x2f;
      mc_member.tclass      = mc_ptr->tclass;
      mc_member.pkey        = be16_to_cpu(mc_ptr->pkey);
      mc_member.rate        = mc_ptr->rate & 0x2f;
      mc_member.packet_life = mc_ptr->packet_life & 0x2f;
      mc_member.sl          = be32_to_cpu(mc_ptr->sl_flowlabel_hoplimit) >> 28;
      mc_member.flowlabel   =
        cpu_to_be32((be32_to_cpu(mc_ptr->sl_flowlabel_hoplimit) >> 8) & 0xfffff);
      mc_member.hoplmt      = be32_to_cpu(mc_ptr->sl_flowlabel_hoplimit) & 0xff;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client multicast response: "
               "mlid=0x%04x",
               mc_member.mlid);

      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               0,
                               &mc_member,
                               query->completion_arg);
      }
    }
    break;

  case TS_IB_CLIENT_RESPONSE_ERROR:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client multicast member MAD status 0x%04x",
             be16_to_cpu(packet->status));
    if (query->completion_func) {
      query->completion_func(query->transaction_id,
                             -EINVAL,
                             NULL,
                             query->completion_arg);
    }
    break;

  case TS_IB_CLIENT_RESPONSE_TIMEOUT:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client multicast member query timed out");
    if (query->completion_func) {
      query->completion_func(query->transaction_id,
                             -ETIMEDOUT,
                             NULL,
                             query->completion_arg);
    }
    break;

  case TS_IB_CLIENT_RESPONSE_CANCEL:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client path multicast member query canceled");
    break;

  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Unknown status %d", status);
    break;
  }

  kfree(query);
}

static void _tsIbMcastGroupTableResponse(tTS_IB_CLIENT_RESPONSE_STATUS status,
                                         uint8_t *header,
                                         uint8_t *data,
                                         uint32_t data_size,
                                         void* query_ptr)
{
  tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY query = (tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY) query_ptr;

  switch (status) {
    case TS_IB_CLIENT_RESPONSE_OK:
    {
      tTS_IB_SA_HEADER sa_header = (tTS_IB_SA_HEADER)header;
      int16_t attrib_offset = be16_to_cpu(sa_header->attrib_offset) * 8;
      int32_t mcast_member_size = sizeof(tTS_IB_SA_MULTICAST_MEMBER_STRUCT);
      int32_t num_entries = data_size / attrib_offset;
      int32_t num_remaining_entries = num_entries;
      int32_t i;

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "_tsIbMcastGroupTableResponse(data_size= %d, mcast-member-size= %d, attrib_offset= %d, num-entries= %d)\n",
               data_size, mcast_member_size, attrib_offset, num_entries);

      for (i = 0; i < num_entries; i++)
      {
        tTS_IB_SA_MULTICAST_MEMBER mc_ptr = (tTS_IB_SA_MULTICAST_MEMBER) (data + i * attrib_offset);
        tTS_IB_MULTICAST_MEMBER_STRUCT mc_member;

        memcpy(mc_member.mgid, mc_ptr->mgid, sizeof mc_member.mgid);

        mc_member.qkey        = be32_to_cpu(mc_ptr->qkey);
        mc_member.mlid        = be16_to_cpu(mc_ptr->mlid);
        mc_member.mtu         = mc_ptr->mtu & 0x2f;
        mc_member.tclass      = mc_ptr->tclass;
        mc_member.pkey        = be16_to_cpu(mc_ptr->pkey);
        mc_member.rate        = mc_ptr->rate & 0x2f;
        mc_member.packet_life = mc_ptr->packet_life & 0x2f;
        mc_member.sl          = be32_to_cpu(mc_ptr->sl_flowlabel_hoplimit) >> 28;
        mc_member.flowlabel   =
          cpu_to_be32((be32_to_cpu(mc_ptr->sl_flowlabel_hoplimit) >> 8) & 0xfffff);
        mc_member.hoplmt      = be32_to_cpu(mc_ptr->sl_flowlabel_hoplimit) & 0xff;

        TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
                 "SA client multicast group GetTable response: "
                 "mgid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x "
                 "mlid=0x%04x",
                 mc_member.mgid[0], mc_member.mgid[1], mc_member.mgid[2], mc_member.mgid[3],
                 mc_member.mgid[4], mc_member.mgid[5], mc_member.mgid[6], mc_member.mgid[7],
                 mc_member.mgid[8], mc_member.mgid[9], mc_member.mgid[10], mc_member.mgid[11],
                 mc_member.mgid[12], mc_member.mgid[13], mc_member.mgid[14], mc_member.mgid[15],
                 mc_member.mlid);

        if (query->completion_func) {
          num_remaining_entries = num_entries - (i + 1);
          query->completion_func(query->transaction_id,
                                 0,
                                 num_remaining_entries,
                                 &mc_member,
                                 query->completion_arg);
        }
      }
    }
    break;

    case TS_IB_CLIENT_RESPONSE_ERROR:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client multicast group table MAD response error");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -EINVAL,
                               0,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_TIMEOUT:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client multicast group table query timed out\n");
      if (query->completion_func) {
        query->completion_func(query->transaction_id,
                               -ETIMEDOUT,
                               0,
                               NULL,
                               query->completion_arg);
      }
      break;

    case TS_IB_CLIENT_RESPONSE_CANCEL:
      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "SA client multicast group table query canceled\n");
      break;

    default:
      TS_REPORT_WARN(MOD_KERNEL_IB,
                     "Unknown status %d", status);
      break;
  }

  tsIbRmppClientFree(data);

  kfree(query);
}

int tsIbMulticastGroupJoin(
                           tTS_IB_DEVICE_HANDLE device,
                           tTS_IB_PORT port,
                           tTS_IB_GID mgid,
                           tTS_IB_PKEY pkey,
                           tTS_IB_MULTICAST_JOIN_STATE join_state,
                           int timeout_jiffies,
                           tTS_IB_MULTICAST_JOIN_COMPLETION_FUNC completion_func,
                           void *completion_arg,
                           tTS_IB_CLIENT_QUERY_TID *transaction_id
                           ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
  tTS_IB_SA_MULTICAST_MEMBER mc_member = (tTS_IB_SA_MULTICAST_MEMBER) sa_payload->admin_data;
  tTS_IB_SA_MULTICAST_MEMBER_QUERY query;

#ifdef W2K_OS // Vipul
  query = kmalloc(sizeof *query, GFP_KERNEL);
#else
  query = kmalloc(sizeof *query, GFP_ATOMIC);
#endif
  if (!query) {
    return -ENOMEM;
  }

  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method           = TS_IB_MGMT_METHOD_SET;
  mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_MC_MEMBER_RECORD);
  mad.attribute_modifier = 0xffffffff; /* match attributes */

  query->transaction_id  = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg  = completion_arg;

  /* mgid, portgid, pkey, join state */
#ifdef W2K_OS // Vipul
  sa_payload->sa_header.component_mask = cpu_to_be64((0x1UL << 0)
                                           | (0x1UL << 1)
                                           | (0x1UL << 7)
                                           | (0x1UL << 16));
#else
    #if defined(TS_ppc440_en_sun) || defined(TS_ppc440_fc_sun)
      #if defined(__SOLARIS_IBSM__) /* old prototype Solaris IBSRM */
        /* Simulate the behavior of the Solaris IPoIB driver - > 0x1b0c7 */
        sa_payload->sa_header.component_mask = cpu_to_be64((0x1ULL << 0)   /* MGID      */
                                                         | (0x1ULL << 1)   /* PortGID   */
                                                      /* | (0x1ULL << 2) *//* Q_Key     */
                                                         | (0x1ULL << 6)   /* TClass    */
                                                         | (0x1ULL << 7)   /* P_Key     */
                                                         | (0x1ULL << 12)  /* SL        */
                                                         | (0x1ULL << 13)  /* FlowLabel */
                                                         | (0x1ULL << 15)  /* Scope     */
                                                         | (0x1ULL << 16)); /* Join State */
      #endif /* __SOLARIS_IBSM__ */
      #define __SOLARIS_IBSRM__
      #if defined(__SOLARIS_IBSRM__) /* official IBSRM */
        /* Per SUN IBSRM group requirment the component mask has to be 0x10003 */
        sa_payload->sa_header.component_mask = cpu_to_be64((0x1ULL << 0)   /* MGID       */
                                                         | (0x1ULL << 1)   /* PortGID    */
                                                         | (0x1ULL << 16));/* Join State */
      #endif /* __SOLARIS_IBSRM__ */
    #else
        sa_payload->sa_header.component_mask = cpu_to_be64((0x1ULL << 0)
                                                         | (0x1ULL << 1)
                                                         | (0x1ULL << 7)
                                                         | (0x1ULL << 16));
    #endif
#endif
  memcpy(mc_member->mgid, mgid, sizeof (tTS_IB_GID));
  mc_member->pkey = cpu_to_be16(pkey);

  tsIbCachedGidGet(device,
                   port,
                   0,
                   mc_member->port_gid);

#if defined(TS_ppc440_en_sun) || defined(TS_ppc440_fc_sun)
  #if defined(__SOLARIS_IBSM__)
    mc_member->tclass = 0;
    mc_member->sl_flowlabel_hoplimit = 0;
    mc_member->scope_joinstate = 0x20 | join_state; /* scope = Link-Local */
  #endif /* __SOLARIS_IBSM__ */
  #if defined(__SOLARIS_IBSRM__) /* official IBSRM */
    mc_member->scope_joinstate = join_state;
  #endif /* __SOLARIS_IBSRM__ */
#else
  mc_member->scope_joinstate = join_state;
#endif

  tsIbClientQuery(&mad, timeout_jiffies, _tsIbMulticastJoinResponse, query);

  *transaction_id = mad.transaction_id;

  return 0;
}

int tsIbMulticastGroupLeave(
                            tTS_IB_DEVICE_HANDLE device,
                            tTS_IB_PORT port,
                            tTS_IB_GID mgid
                            ) {
  /* XXX implement */
  return 0;
}

int tsIbMulticastGroupTableQuery(
                                 tTS_IB_DEVICE_HANDLE device,
                                 tTS_IB_PORT port,
                                 int timeout_jiffies,
                                 tTS_IB_PKEY partition,
                                 tTS_IB_MCAST_GROUP_QUERY_COMPLETION_FUNC completion_func,
                                 void *completion_arg,
                                 tTS_IB_CLIENT_QUERY_TID *transaction_id
                                 ) {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
  tTS_IB_SA_MULTICAST_MEMBER mc_member = (tTS_IB_SA_MULTICAST_MEMBER) sa_payload->admin_data;
  tTS_IB_SA_MULTICAST_GROUP_TABLE_QUERY query;
  tTS_IB_CLIENT_RMPP_MAD rmpp_mad;

  query = kmalloc(sizeof *query, GFP_ATOMIC);
  if (!query) {
    return -ENOMEM;
  }

  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method            = TS_IB_SA_METHOD_GET_TABLE;
  mad.attribute_id        = cpu_to_be16(TS_IB_SA_ATTRIBUTE_MC_MEMBER_RECORD);
  mad.attribute_modifier  = 0;

  /* rmpp header init */
  rmpp_mad = (tTS_IB_CLIENT_RMPP_MAD)&mad;
  rmpp_mad->version = 1;
  rmpp_mad->type = TS_IB_CLIENT_RMPP_TYPE_DATA;

  query->transaction_id   = mad.transaction_id;
  query->completion_func  = completion_func;
  query->completion_arg   = completion_arg;

  /* PKEY */
#ifdef W2K_OS
  sa_payload->sa_header.component_mask = cpu_to_be64(0x1UL << 7);
#else
  sa_payload->sa_header.component_mask = cpu_to_be64(0x1ULL << 7);
#endif
  mc_member->pkey = cpu_to_be16(partition);

  *transaction_id = mad.transaction_id;

  tsIbRmppClientQuery(&mad, timeout_jiffies, sizeof(tTS_IB_SA_HEADER_STRUCT),
                      _tsIbMcastGroupTableResponse, query);

  return 0;
}
