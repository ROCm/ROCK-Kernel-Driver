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

  $Id: ts_ib_useraccess.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_USERACCESS_H
#define _TS_IB_USERACCESS_H

#include "ts_ib_mad_types.h"

#include <linux/ioctl.h>

typedef uint32_t tTS_IB_USER_MAD_FILTER_HANDLE;

typedef struct tTS_IB_USER_MAD_FILTER_STRUCT tTS_IB_USER_MAD_FILTER_STRUCT,
  *tTS_IB_USER_MAD_FILTER;
typedef struct tTS_IB_GET_PORT_INFO_IOCTL_STRUCT tTS_IB_GET_PORT_INFO_IOCTL_STRUCT,
  *tTS_IB_GET_PORT_INFO_IOCTL;
typedef struct tTS_IB_SET_PORT_INFO_IOCTL_STRUCT tTS_IB_SET_PORT_INFO_IOCTL_STRUCT,
  *tTS_IB_SET_PORT_INFO_IOCTL;
typedef struct tTS_IB_MAD_PROCESS_IOCTL_STRUCT tTS_IB_MAD_PROCESS_IOCTL_STRUCT,
  *tTS_IB_MAD_PROCESS_IOCTL;
typedef struct tTS_IB_QP_REGISTER_IOCTL_STRUCT tTS_IB_QP_REGISTER_IOCTL_STRUCT,
  *tTS_IB_QP_REGISTER_IOCTL;
typedef struct tTS_IB_PATH_RECORD_IOCTL tTS_IB_PATH_RECORD_IOCTL_STRUCT,
  *tTS_IB_PATH_RECORD_IOCTL;
typedef struct tTS_IB_GID_ENTRY_IOCTL_STRUCT tTS_IB_GID_ENTRY_IOCTL_STRUCT,
  *tTS_IB_GID_ENTRY_IOCTL;

struct tTS_IB_USER_MAD_FILTER_STRUCT {
  tTS_IB_PORT                   port;
  tTS_IB_QPN                    qpn;
  uint8_t                       mgmt_class;
  uint8_t                       r_method;
  uint16_t                      attribute_id;
  tTS_IB_MAD_DIRECTION          direction;
  tTS_IB_MAD_FILTER_MASK        mask;
  tTS_IB_USER_MAD_FILTER_HANDLE handle;
};

struct tTS_IB_GET_PORT_INFO_IOCTL_STRUCT {
  tTS_IB_PORT                   port;
  tTS_IB_PORT_PROPERTIES_STRUCT port_info;
};

struct tTS_IB_SET_PORT_INFO_IOCTL_STRUCT {
  tTS_IB_PORT                       port;
  tTS_IB_PORT_PROPERTIES_SET_STRUCT port_info;
};

struct tTS_IB_MAD_PROCESS_IOCTL_STRUCT {
  tTS_IB_MAD_STRUCT mad;
  tTS_IB_MAD_RESULT result;
};

struct tTS_IB_PATH_RECORD_IOCTL {
  uint32_t              dst_addr;
  tTS_IB_PATH_RECORD    path_record;
};

struct tTS_IB_GID_ENTRY_IOCTL_STRUCT {
  tTS_IB_PORT                   port;
  int                           index;
  tTS_IB_GID                    gid_entry;
};

/* Old useraccess module used magic 0xbb; we change it here so
   old binaries don't fail silently in strange ways. */
#define TS_IB_IOCTL_MAGIC 0xbc

/* Add a MAD filter; handle will be filled in by kernel */
#define TS_IB_IOCSMADFILTADD     _IOWR(TS_IB_IOCTL_MAGIC, 1, tTS_IB_USER_MAD_FILTER)

/* Remove a MAD filter */
#define TS_IB_IOCSMADFILTDEL     _IOW(TS_IB_IOCTL_MAGIC, 2, tTS_IB_USER_MAD_FILTER_HANDLE *)

/* Get port info */
#define TS_IB_IOCGPORTINFO       _IOR(TS_IB_IOCTL_MAGIC, 3, tTS_IB_GET_PORT_INFO_IOCTL)

/* Set port info */
#define TS_IB_IOCSPORTINFO       _IOW(TS_IB_IOCTL_MAGIC, 4, tTS_IB_SET_PORT_INFO_IOCTL)

/* Set receive queue size */
#define TS_IB_IOCSRCVQUEUELENGTH _IOW(TS_IB_IOCTL_MAGIC, 5, uint32_t *)

/* Get receive queue size */
#define TS_IB_IOCGRCVQUEUELENGTH _IOR(TS_IB_IOCTL_MAGIC, 6, uint32_t *)

/* Have MAD processed by provider */
#define TS_IB_IOCMADPROCESS      _IOWR(TS_IB_IOCTL_MAGIC, 7, tTS_IB_MAD_PROCESS_IOCTL)

/* Register a QP with the kernel */
#define TS_IB_IOCQPREGISTER      _IOWR(TS_IB_IOCTL_MAGIC, 8, tTS_IB_QP_REGISTER_IOCTL)

/* Perform path record lookup */
#define TS_IB_IOCGPATHRECORD     _IOWR(TS_IB_IOCTL_MAGIC, 9, tTS_IB_PATH_RECORD_IOCTL)

/* Fetch a GID */
#define TS_IB_IOCGGIDENTRY       _IOWR(TS_IB_IOCTL_MAGIC, 10, tTS_IB_GID_ENTRY_IOCTL)

#endif /* _TS_IB_USERACCESS_H */
