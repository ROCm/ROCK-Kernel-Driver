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

  $Id: sa_client_service.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sa_client.h"

#include "ts_ib_mad.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/byteorder.h>
#endif

#define TS_IB_SA_SR_SET_COMPONENT_MASK      0x57
#define TS_IB_SA_ATS_SET_COMPONENT_MASK     0x57
#define TS_IB_SA_ATS_GET_IP_COMPONENT_MASK  0x43
#define TS_IB_SA_ATS_GET_GID_COMPONENT_MASK 0x7FFFC1
#define TS_IB_SA_ATS_DAPL_SERVICE_ID        0x10000CE100415453ull   /* DAPL ATS */
#define TS_IB_SA_ATS_DEFAULT_PKEY           0xFFFF
#define TS_IB_SA_ATS_DEFAULT_LEASE          0xFFFFFFFF  /* indefinite */
#define TS_IB_SA_ATS_SERVICE_NAME           "DAPL Address Translation Service"

typedef struct tTS_IB_SA_SERVICE_STRUCT tTS_IB_SA_SERVICE_STRUCT,
  *tTS_IB_SA_SERVICE;

typedef struct tTS_IB_SA_SERVICE_QUERY_STRUCT tTS_IB_SA_SERVICE_QUERY_STRUCT,
  *tTS_IB_SA_SERVICE_QUERY;

typedef struct tTS_IB_SA_SERVICE_GET_GID_QUERY_STRUCT tTS_IB_SA_SERVICE_GET_GID_QUERY_STRUCT,
  *tTS_IB_SA_SERVICE_GET_GID_QUERY;

typedef struct tTS_IB_SA_SERVICE_GET_IP_QUERY_STRUCT tTS_IB_SA_SERVICE_GET_IP_QUERY_STRUCT,
  *tTS_IB_SA_SERVICE_GET_IP_QUERY;

struct tTS_IB_SA_SERVICE_STRUCT {
  uint64_t        service_id;
  tTS_IB_GID      service_gid;
  tTS_IB_PKEY     service_pkey;

  uint16_t        reserved;

  uint32_t        service_lease;

  uint8_t         service_key[16];

  uint8_t         service_name[64];

  uint8_t         service_data8[16];
  uint16_t        service_data16[8];
  uint32_t        service_data32[4];
  uint64_t        service_data64[2];
} __attribute__((packed));

struct tTS_IB_SA_SERVICE_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_SERVICE_SET_COMPLETION_FUNC    completion_func;
  void *                                    completion_arg;
};

struct tTS_IB_SA_SERVICE_GET_GID_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_SERVICE_GET_GID_COMPLETION_FUNC    completion_func;
  void *                                    completion_arg;
};

struct tTS_IB_SA_SERVICE_GET_IP_QUERY_STRUCT {
  tTS_IB_CLIENT_QUERY_TID                   transaction_id;
  tTS_IB_SERVICE_GET_IP_COMPLETION_FUNC     completion_func;
  void *                                    completion_arg;
};

static void _tsIbServiceResponse(
				tTS_IB_CLIENT_RESPONSE_STATUS status,
				tTS_IB_MAD packet,
				void *query_ptr
) {
	tTS_IB_SA_SERVICE_QUERY query = query_ptr;

	switch (status) {
	case TS_IB_CLIENT_RESPONSE_OK:
	{
		tTS_IB_SA_PAYLOAD sa_payload =
			(tTS_IB_SA_PAYLOAD) &packet->payload;
		tTS_IB_SA_SERVICE mad_service =
			(tTS_IB_SA_SERVICE) sa_payload->admin_data;
		tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT service;

		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
				"SA client set service  status OK\n");

		memcpy(service.service_gid, mad_service->service_gid,
				sizeof(tTS_IB_GID));
		memcpy(service.service_key, mad_service->service_key,
				16);
		memcpy(service.service_name, mad_service->service_name,
				16);
		service.service_pkey =
			be16_to_cpu(mad_service->service_pkey);
		service.service_id =
			be64_to_cpu(mad_service->service_id);
		service.service_lease =
			be32_to_cpu(mad_service->service_lease);


		if (query->completion_func) {
			query->completion_func(query->transaction_id,
			0,
			&service,
			query->completion_arg);
		}
	}
	break;

	case TS_IB_CLIENT_RESPONSE_ERROR:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
			"SA client service  MAD status 0x%04x",
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
			"SA client service  query canceled");
	break;

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			"Unknown status %d", status);
		break;
	}

	kfree(query);
}

static int _tsIbServiceSet(
			tTS_IB_DEVICE_HANDLE device,
			tTS_IB_PORT port,
			tTS_IB_COMMON_ATTRIB_SERVICE service,
			int timeout_jiffies,
			tTS_IB_SERVICE_SET_COMPLETION_FUNC completion_func,
			void *completion_arg,
			tTS_IB_CLIENT_QUERY_TID *transaction_id
 ) {
	tTS_IB_MAD_STRUCT	mad;
	tTS_IB_SA_PAYLOAD	sa_payload
				= (tTS_IB_SA_PAYLOAD) &mad.payload;
	tTS_IB_SA_SERVICE	mad_service
				= (tTS_IB_SA_SERVICE) sa_payload->admin_data;
	tTS_IB_SA_SERVICE_QUERY	query;

	/* MAD initialization */
	tsIbSaClientMadInit(&mad, device, port);
	mad.r_method           = TS_IB_MGMT_METHOD_SET;
	mad.attribute_id       = cpu_to_be16(TS_IB_SA_ATTRIBUTE_SERVICE_RECORD);
	mad.attribute_modifier = 0;

	/* SA header */
	sa_payload->sa_header.component_mask = cpu_to_be64(TS_IB_SA_SR_SET_COMPONENT_MASK);

	/* fill in data */
	memcpy(mad_service->service_gid, service->service_gid,
			sizeof(tTS_IB_GID));
	mad_service->service_id	= cpu_to_be64(service->service_id);
	mad_service->service_pkey	= cpu_to_be16(service->service_pkey);
	mad_service->service_lease	= -1; /* 0xffffffffff */
	memcpy(mad_service->service_name, service->service_name, 64);
	memcpy(mad_service->service_key, service->service_key, 16);

	*transaction_id = mad.transaction_id;

 	/* construct query */
	query = kmalloc(sizeof *query, GFP_ATOMIC);
	if (!query) {
		return -ENOMEM;
	}

	query->transaction_id  = mad.transaction_id;
	query->completion_func = completion_func;
	query->completion_arg  = completion_arg;

	tsIbClientQuery(&mad, timeout_jiffies, _tsIbServiceResponse, query);

	return 0;
}

static void _tsIbServiceAtsGetGidResponse(
                                          tTS_IB_CLIENT_RESPONSE_STATUS status,
                                          tTS_IB_MAD packet,
                                          void *query_ptr
                                          )
{
	tTS_IB_SA_SERVICE_GET_GID_QUERY query = query_ptr;
  tTS_IB_GID gid;

  memset(gid, 0, sizeof(gid));

	switch (status) {
	case TS_IB_CLIENT_RESPONSE_OK:
	{
		tTS_IB_SA_PAYLOAD sa_payload =
			(tTS_IB_SA_PAYLOAD) &packet->payload;
		tTS_IB_SA_SERVICE mad_service =
			(tTS_IB_SA_SERVICE) sa_payload->admin_data;

		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
				"SA client tsIbServiceAtsGetGidResponse() status OK\n");

    memcpy(gid, mad_service->service_gid, sizeof(gid));

		if (query->completion_func) {
			query->completion_func(query->transaction_id,
                             0,
                             gid,
                             query->completion_arg);
		}
	}
	break;

	case TS_IB_CLIENT_RESPONSE_ERROR:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client tsIbServiceAtsGetGidResponse status 0x%04x",
             be16_to_cpu(packet->status));
		if (query->completion_func) {
			query->completion_func(query->transaction_id,
                             -EINVAL,
                             gid,
                             query->completion_arg);
		}
		break;

	case TS_IB_CLIENT_RESPONSE_TIMEOUT:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
			"SA client tsIbServiceAtsGetGidResponse() query timed out");
		if (query->completion_func) {
			query->completion_func(query->transaction_id,
                             -ETIMEDOUT,
                             gid,
                             query->completion_arg);
		}
		break;

	case TS_IB_CLIENT_RESPONSE_CANCEL:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
			"SA client service  query canceled");
	break;

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			"Unknown status %d", status);
		break;
	}

	kfree(query);
}

static void _tsIbServiceAtsGetIpResponse(
                                         tTS_IB_CLIENT_RESPONSE_STATUS status,
                                         tTS_IB_MAD packet,
                                         void *query_ptr
                                         )
{
	tTS_IB_SA_SERVICE_GET_IP_QUERY query = query_ptr;
  tTS_IB_ATS_IP_ADDR ip_addr;

  memset(ip_addr, 0, sizeof(ip_addr));

	switch (status) {
	case TS_IB_CLIENT_RESPONSE_OK:
	{
		tTS_IB_SA_PAYLOAD sa_payload =
			(tTS_IB_SA_PAYLOAD) &packet->payload;
		tTS_IB_SA_SERVICE mad_service =
			(tTS_IB_SA_SERVICE) sa_payload->admin_data;

		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
				"SA client tsIbServiceAtsGetIpResponse() status OK\n");

    memcpy(ip_addr, mad_service->service_data8, sizeof(ip_addr));

		if (query->completion_func) {
			query->completion_func(query->transaction_id,
                             0,
                             ip_addr,
                             query->completion_arg);
		}
	}
	break;

	case TS_IB_CLIENT_RESPONSE_ERROR:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "SA client tsIbServiceAtsGetIpResponse status 0x%04x",
             be16_to_cpu(packet->status));
		if (query->completion_func) {
			query->completion_func(query->transaction_id,
                             -EINVAL,
                             ip_addr,
                             query->completion_arg);
		}
		break;

	case TS_IB_CLIENT_RESPONSE_TIMEOUT:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
			"SA client tsIbServiceAtsGetIpResponse() query timed out");
		if (query->completion_func) {
			query->completion_func(query->transaction_id,
                             -ETIMEDOUT,
                             ip_addr,
                             query->completion_arg);
		}
		break;

	case TS_IB_CLIENT_RESPONSE_CANCEL:
		TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
			"SA client service  query canceled");
	break;

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			"Unknown status %d", status);
		break;
	}

	kfree(query);
}

int tsIbServiceSet(
			tTS_IB_DEVICE_HANDLE	device,
			tTS_IB_PORT		port,
			uint64_t		service_id,
			tTS_IB_GID		gid,
			tTS_IB_PKEY		service_pkey,
			uint8_t			*service_name,
			int 			timeout_jiffies,
			tTS_IB_SERVICE_SET_COMPLETION_FUNC
						completion_func,
			void			*completion_arg,
			tTS_IB_CLIENT_QUERY_TID	*transaction_id
			)
{
	int rc;
	tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT service;

	TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
		"SA client register service record: "
		"gid=%02x%02x%02x%02x%02x%02x%02x%02x"
		"%02x%02x%02x%02x%02x%02x%02x%02x "
		"service_id=0x%Lx, service_name=%s\n",
		gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
		gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14],
		gid[15],
		service_id, service_name);

	/* Construct service */
	memset(&service, 0, sizeof(tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT));
	memcpy(service.service_gid, gid, sizeof(tTS_IB_GID));
	service.service_id	= service_id;
	service.service_pkey	= service_pkey;
	memcpy(&service.service_name, service_name, 64);

	/* Subscribe/unsubscribe to SA */
	rc = _tsIbServiceSet(device,
				port,
				&service,
				timeout_jiffies,
				completion_func,
				completion_arg,
				transaction_id);

	return rc;
}


int tsIbAtsServiceSet(
                      tTS_IB_DEVICE_HANDLE device,
                      tTS_IB_PORT port,
                      tTS_IB_GID gid,
                      tTS_IB_ATS_IP_ADDR ip_addr,
                      int timeout_jiffies,
                      tTS_IB_SERVICE_SET_COMPLETION_FUNC completion_func,
                      void *completion_arg,
                      tTS_IB_CLIENT_QUERY_TID *transaction_id
                     )
{
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
	tTS_IB_SA_SERVICE service = (tTS_IB_SA_SERVICE) sa_payload->admin_data;
	tTS_IB_SA_SERVICE_QUERY	query;

	TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client register ATS service record: "
           "GID=%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x "
           "IP=%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x\n",
           gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
           gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15],
           ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], ip_addr[4],
           ip_addr[5], ip_addr[6], ip_addr[7], ip_addr[8], ip_addr[9],
           ip_addr[10], ip_addr[11], ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15]);

  /* MAD initialization */
  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method = TS_IB_MGMT_METHOD_SET;
  mad.attribute_id = cpu_to_be16(TS_IB_SA_ATTRIBUTE_SERVICE_RECORD);
  mad.attribute_modifier = 0;

  /* SA header */
  sa_payload->sa_header.component_mask = cpu_to_be64(TS_IB_SA_ATS_SET_COMPONENT_MASK);

	/* Construct service */
	memset(service, 0, sizeof(tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT));
	service->service_id	= cpu_to_be64(TS_IB_SA_ATS_DAPL_SERVICE_ID);
	memcpy(service->service_gid, gid, sizeof(tTS_IB_GID));
  service->service_pkey = cpu_to_be16(TS_IB_SA_ATS_DEFAULT_PKEY);
  service->service_lease = cpu_to_be32(TS_IB_SA_ATS_DEFAULT_LEASE);
  strcpy(service->service_name, TS_IB_SA_ATS_SERVICE_NAME);
  memcpy(service->service_data8, ip_addr, sizeof(tTS_IB_ATS_IP_ADDR));

  /* construct query */
  query = kmalloc(sizeof *query, GFP_ATOMIC);
  if (!query) {
    return -ENOMEM;
  }
  query->transaction_id = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg = completion_arg;

	/* Subscribe/unsubscribe to SA */
  *transaction_id = mad.transaction_id;
  tsIbClientQuery(&mad, timeout_jiffies, _tsIbServiceResponse, query);

	return 0;
}

int tsIbAtsServiceGetGid(
                         tTS_IB_DEVICE_HANDLE device,
                         tTS_IB_PORT port,
                         tTS_IB_ATS_IP_ADDR ip_addr,
                         int timeout_jiffies,
                         tTS_IB_SERVICE_GET_GID_COMPLETION_FUNC completion_func,
                         void *completion_arg,
                         tTS_IB_CLIENT_QUERY_TID *transaction_id)
{
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
	tTS_IB_SA_SERVICE service = (tTS_IB_SA_SERVICE) sa_payload->admin_data;
	tTS_IB_SA_SERVICE_GET_GID_QUERY	query;

	TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client get ATS GID: "
           "IP=%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x\n",
           ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3], ip_addr[4],
           ip_addr[5], ip_addr[6], ip_addr[7], ip_addr[8], ip_addr[9],
           ip_addr[10], ip_addr[11], ip_addr[12], ip_addr[13], ip_addr[14], ip_addr[15]);

  /* MAD initialization */
  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method = TS_IB_MGMT_METHOD_GET;
  mad.attribute_id = cpu_to_be16(TS_IB_SA_ATTRIBUTE_SERVICE_RECORD);
  mad.attribute_modifier = 0;

  /* SA header */
  sa_payload->sa_header.component_mask = cpu_to_be64(TS_IB_SA_ATS_GET_GID_COMPONENT_MASK);

	/* Construct service */
	memset(service, 0, sizeof(tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT));
	service->service_id	= cpu_to_be64(TS_IB_SA_ATS_DAPL_SERVICE_ID);
  strcpy(service->service_name, TS_IB_SA_ATS_SERVICE_NAME);
  memcpy(service->service_data8, ip_addr, sizeof(tTS_IB_ATS_IP_ADDR));

  /* construct query */
  query = kmalloc(sizeof *query, GFP_ATOMIC);
  if (!query) {
    return -ENOMEM;
  }
  query->transaction_id = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg = completion_arg;

	/* Subscribe/unsubscribe to SA */
  *transaction_id = mad.transaction_id;
  tsIbClientQuery(&mad, timeout_jiffies, _tsIbServiceAtsGetGidResponse, query);

	return 0;
}

int tsIbAtsServiceGetIp(
                        tTS_IB_DEVICE_HANDLE device,
                        tTS_IB_PORT port,
                        tTS_IB_GID gid,
                        int timeout_jiffies,
                        tTS_IB_SERVICE_GET_IP_COMPLETION_FUNC completion_func,
                        void *completion_arg,
                        tTS_IB_CLIENT_QUERY_TID *transaction_id)
{
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_SA_PAYLOAD sa_payload = (tTS_IB_SA_PAYLOAD) &mad.payload;
	tTS_IB_SA_SERVICE service = (tTS_IB_SA_SERVICE) sa_payload->admin_data;
	tTS_IB_SA_SERVICE_GET_IP_QUERY query;

	TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "SA client register ATS service record: "
           "GID=%02x%02x%02x%02x%02x%02x%02x%02x"
           "%02x%02x%02x%02x%02x%02x%02x%02x ",
           gid[0], gid[1], gid[2], gid[3], gid[4], gid[5], gid[6], gid[7],
           gid[8], gid[9], gid[10], gid[11], gid[12], gid[13], gid[14], gid[15]);

  /* MAD initialization */
  tsIbSaClientMadInit(&mad, device, port);
  mad.r_method = TS_IB_MGMT_METHOD_GET;
  mad.attribute_id = cpu_to_be16(TS_IB_SA_ATTRIBUTE_SERVICE_RECORD);
  mad.attribute_modifier = 0;

  /* SA header */
  sa_payload->sa_header.component_mask = cpu_to_be64(TS_IB_SA_ATS_GET_IP_COMPONENT_MASK);

  /* Construct service */
  memset(service, 0, sizeof(tTS_IB_COMMON_ATTRIB_SERVICE_STRUCT));
  service->service_id	= cpu_to_be64(TS_IB_SA_ATS_DAPL_SERVICE_ID);
  strcpy(service->service_name, TS_IB_SA_ATS_SERVICE_NAME);
  memcpy(service->service_gid, gid, sizeof(tTS_IB_GID));

  /* construct query */
  query = kmalloc(sizeof *query, GFP_ATOMIC);
  if (!query) {
    return -ENOMEM;
  }
  query->transaction_id = mad.transaction_id;
  query->completion_func = completion_func;
  query->completion_arg = completion_arg;

	/* Subscribe/unsubscribe to SA */
  *transaction_id = mad.transaction_id;
  tsIbClientQuery(&mad, timeout_jiffies, _tsIbServiceAtsGetIpResponse, query);

	return 0;
}
