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

  $Id: ip2pr_export.h 40 2004-04-10 19:24:27Z roland $
*/

#ifndef _TS_IP2PR_EXPORT_H
#define _TS_IP2PR_EXPORT_H

#include <ib_legacy_types.h>

/* ------------------------------------------------------------------------- */
/* kernel                                                                    */
/* ------------------------------------------------------------------------- */
#ifdef __KERNEL__
/*
 * address resolution ID
 */
typedef tUINT64  tIP2PR_PATH_LOOKUP_ID;
/*
 * invalid address resolution ID
 */
#define TS_IP2PR_PATH_LOOKUP_INVALID 0xffffffffffffffffull
/*
 * address resolved completion function.
 */
typedef tINT32 (* tIP2PR_PATH_LOOKUP_FUNC) (tIP2PR_PATH_LOOKUP_ID  plid,
					  tINT32               status,
					  tUINT32              src_addr,
					  tUINT32              dst_addr,
					  tTS_IB_PORT          hw_port,
					  tTS_IB_DEVICE_HANDLE ca,
					  tTS_IB_PATH_RECORD   path,
					  tPTR                 usr_arg);

typedef tINT32 (* tGID2PR_LOOKUP_FUNC) (tIP2PR_PATH_LOOKUP_ID  plid,
					  tINT32               status,
					  tTS_IB_PORT          hw_port,
					  tTS_IB_DEVICE_HANDLE ca,
					  tTS_IB_PATH_RECORD   path,
					  tPTR                 usr_arg);
/*
 * address lookup initiation.
 *
 *   dst_addr     - destination IP address (network byte order)
 *   src_addr     - source IP address (network byte order) (optional)
 *   localroute   - if not zero, do not traverse gateways.
 *   bound_dev_if - force device interface for egress.
 *   func         - callback function on completion of lookup
 *   arg          - supplied argument is returned in callback function
 *   plid         - pointer to storage for identifier of this query.
 */
tINT32 tsIp2prPathRecordLookup(
  tUINT32               dst_addr,      /* NBO */
  tUINT32               src_addr,      /* NBO */
  tUINT8                localroute,
  tINT32                bound_dev_if,
  tIP2PR_PATH_LOOKUP_FUNC func,
  tPTR                  arg,
  tIP2PR_PATH_LOOKUP_ID  *plid
  );
/*
 * address lookup cancel
 */
tINT32 tsIp2prPathRecordCancel(
  tIP2PR_PATH_LOOKUP_ID plid
  );
/*
 * Giver a Source and Destination GID, get the path record
 */
tINT32 tsGid2prLookup
(
 tTS_IB_GID     src_gid,
 tTS_IB_GID     dst_gid,
 tTS_IB_PKEY	pkey,
 tGID2PR_LOOKUP_FUNC func,
 tPTR           arg,
 tIP2PR_PATH_LOOKUP_ID  *plid
 );
tINT32 tsGid2prCancel(
  tIP2PR_PATH_LOOKUP_ID plid
  );
#endif

struct tIP2PR_LOOKUP_PARAM_STRUCT {
    tUINT32            dst_addr;
    tTS_IB_PATH_RECORD path_record;
};
struct tGID2PR_LOOKUP_PARAM_STRUCT {
    tTS_IB_GID         src_gid;
    tTS_IB_GID         dst_gid;
    tTS_IB_PKEY        pkey;
    tTS_IB_DEVICE_HANDLE device;
    tTS_IB_PORT        port;
    tTS_IB_PATH_RECORD path_record;
};
typedef struct tIP2PR_LOOKUP_PARAM_STRUCT tIP2PR_LOOKUP_PARAM_STRUCT, \
               *tIP2PR_LOOKUP_PARAM;
typedef struct tGID2PR_LOOKUP_PARAM_STRUCT tGID2PR_LOOKUP_PARAM_STRUCT, \
               *tGID2PR_LOOKUP_PARAM;

#define IP2PR_IOC_MAGIC         100
#define IP2PR_IOC_LOOKUP_REQ    _IOWR(IP2PR_IOC_MAGIC, 0, void *)
#define GID2PR_IOC_LOOKUP_REQ   _IOWR(IP2PR_IOC_MAGIC, 1, void *)

/* ------------------------------------------------------------------------- */
/* user space                                                                */
/* ------------------------------------------------------------------------- */
#ifndef __KERNEL__

#endif

#endif  /* _TS_IP2PR_EXPORT_H */
