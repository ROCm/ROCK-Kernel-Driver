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

  $Id: module_names.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _ALL_COMMON_MODULE_NAMES_H
#define _ALL_COMMON_MODULE_NAMES_H

#include "module_codes.h"

/*
  This include file is included in both common library code and the
  kernel trace module.  Don't add any more declarations or definitions
  to this file; create a new file instead!  (Of course it is OK to add
  more module names to the ModuleName array)
*/
#ifndef W2K_OS // Vipul
static const char *ModuleName[MOD_MAX + 1] = {
  [MOD_COMMON]    = "COMMON",        /* common */
  [MOD_AMF]       = "AMF",           /* application management framework */
  [MOD_SNMP]      = "SNMP",          /* snmp agent */
  [MOD_PORT]      = "PORT",          /* port manager */
  [MOD_CHAS]      = "CHAS",          /* chassis manger */
  [MOD_CLI]       = "CLI",           /* command cli interface */
  [MOD_NET]       = "NET",           /* network subsystem */
  [MOD_MGMT]      = "MGMT",          /* management module */
  [MOD_STP]       = "STP",           /* spanning tree protocol */
  [MOD_VLAN]      = "VLAN",          /* vlan manager */
  [MOD_IB]        = "IB",            /* infiniband subsystem */
  [MOD_OSPF]      = "OSPF",
  [MOD_RIP]       = "RIP",
  [MOD_VRRP]      = "VRRP",
  [MOD_HW]        = "HW",            /* All Topspin FPGAs and ASICs, TCP/IP/Socket */
  [MOD_SRP]       = "SRP",           /* SRP module */
  [MOD_SYS]       = "SYS",           /* system */
  [MOD_NOTIFIER]  = "NOTIFIER",
  [MOD_SRPTP]     = "SRPTP",         /* SRP transport provider module */
  [MOD_KERNEL_IB] = "KERNEL_IB",     /* Kernel IB core API */
  [MOD_IB_CM]     = "IB_CM",         /* IB CM */
  [MOD_IB_NET]    = "IB_NET",        /* Linux native IPoIB packet driver */
  [MOD_IPMGR]     = "IP_MGR",        /* IP manager */
  [MOD_PA_EN]     = "PA_EN",         /* Port Agent ethernet */
  [MOD_PA_IB]     = "PA_IB",         /* Port Agent IB */
  [MOD_PA_FC]     = "PA_FC",         /* Port Agent FC */
  [MOD_PA_GW]     = "PA_GW",         /* Port Agent Gateway */
  [MOD_STRM_SDP]  = "STRM_SDP",      /* Networking Gateway SDP protocol */
  [MOD_STRM_TCP]  = "STRM_TCP",      /* Networking Gateway TCP protocol */
  [MOD_STRM_USR]  = "STRM_USR",      /* Networking Gateway USR interface  protocol */
  [MOD_GW_CORE]   = "GW_CORE",       /* Networking Gateway Framework */
  [MOD_PKT_PROC]  = "PKT_PROC",      /* Networking Gateway Proc Packet Driver */
  [MOD_PKT_E1000] = "PKT_E1000",     /* Networking Gateway Intel 82544 Packet Driver*/
  [MOD_PKT_IPOIB] = "PKT_IPOIB",     /* Networking Gateway IPoIB Packet Driver */
  [MOD_IPA]       = "IP_IPA",        /* IP Agent */
  [MOD_FIB_MGR]   = "FIB_MGR",       /* FIB Manager */
  [MOD_FIB_AGENT] = "FIB_AGENT",     /* FIB Agent */
  [MOD_JNI]       = "JNI",           /* JNI HBA driver */
  [MOD_GW_TEST]   = "GW_TEST",       /* Networking Gateway test modules */
  [MOD_PKT_SHIM]  = "PKT_SHIM",      /* Networking Gateway shim Packet Driver */
  [MOD_MA_IPC]    = "MA_IPC",        /* Manager Agent IPC library */
  [MOD_SRP_LIB]   = "SRP_LIB",       /* system srp_lib */
  [MOD_SRPM]      = "SRP_MGR",       /* system srp_mgr */
  [MOD_SRPA]      = "SRP_AGENT",     /* system srp_agent */
  [MOD_PKT_RNDIS] = "PKT_RNDIS",     /* Networking Gateway RNDIS Packet Driver */
  [MOD_POLL]      = "KERNEL_POLL",   /* kernel poll loop */
  [MOD_RNDIS_LIB] = "RNDIS_LIB",     /* system rndis_lib */
  [MOD_RNDISM]    = "RNDIS_MGR",     /* system rndis_mgr */
  [MOD_RNDISA]    = "RNDIS_AGENT",   /* system rndis_agent */
  [MOD_WD]        = "WATCHDOG",      /* system watchdog */
  [MOD_PKT_IPOETH]= "PKT_IPOETH",    /* Networking Gateway IPoETH Packet Driver */
  [MOD_STRM_LNX]  = "STRM_LNX",      /* Networking Gateway LNX interface  protocol */
  [MOD_LNX_SDP]   = "MOD_LNX_SDP",
  [MOD_UDAPL]     = "UDAPL",
  [MOD_KERNEL_DM] = "KERNEL_DM",
  [MOD_PKT_EN_TRUNK] = "TRUNK_EN",
  [MOD_DIAG]      = "DIAG",          /* diagnostic */
  [MOD_FIG]       = "FIG",           /* FIG hardware access module */
  [MOD_PKT_EN_FIG]= "EN FIG",        /* FIG ethernet packet device */
  [MOD_IP2PR]     = "IP2PR ",        /* IP to Path Record resolution */
  [MOD_FFE_SWC]   = "FFE_SWC",       /* Software C based FFE */
  [MOD_WMI]       = "WMI",           /* Web Management Interface */
  [MOD_FFE_FIG]   = "FFE_FIG",       /* FIG based FFE */
  [MOD_SCORE]     = "SCORE",
  [MOD_PKT_RPOIB] = "PKT_RPOIB",     /* Redundancy protocol over IB packet device */
  [MOD_IB_SMA]    = "IB_SMA",        /* Subnet Management Agent */
  [MOD_IB_PMA]    = "IB_PMA",        /* Performance Management Agent */
  [MOD_SYSCONF]   = "SYS_CONF",      /* all/sysconf */
  [MOD_IB_LOGM]   = "IB_LOGM",       /* Infiniband subsystem log management */

  /* Add new modules above this line. */
  [MOD_MAX]      = "MAX"
};
#else /* W2K_OS*/
static const char *ModuleName[MOD_MAX + 1] = {
  "COMMON",
  "AMF",
  "SNMP",
  "PORT",
  "CHAS",
  "CLI",
  "NET",
  "MGMT",
  "STP",
  "VLAN",
  "IB",
  "OSPF",
  "RIP",
  "VRRP",
  "HW",
  "SRP",
  "SYS",
  "NOTIFIER",
  "SRPTP",
  "KERNEL_IB",
  "IB_CM",
  "IB_NET",
  "IP_MGR",
  "PA_EN",
  "PA_IB",
  "PA_FC",
  "PA_GW",
  "STRM_SDP",
  "STRM_TCP",
  "STRM_USR",
  "GW_CORE",
  "PKT_PROC",
  "PKT_E1000",
  "PKT_IPOIB",
  "IP_IPA",
  "FIB_MGR",
  "FIB_AGENT",
  "JNI",
  "GW_TEST",
  "PKT_SHIM",
  "MA_IPC",
  "SRP_LIB",
  "SRP_MGR",
  "SRP_AGENT",
  "PKT_RNDIS",
  "KERNEL_POLL",
  "RNDIS_LIB",
  "RNDIS_MGR",
  "RNDIS_AGENT",
  "WATCHDOG",
  "PKT_IPOETH",
  "STRM_LNX",
  "MOD_LNX_SDP",
  "UDAPL",
  "KERNEL_DM",
  "TRUNK_EN",
  "DIAG",
  "FIG",
  "EN-FIG",
  "IP2PR",
  "FFE_SWC",
  "WMI",
  "FFE_FIG",
  "SCORE",
  "MOD_PKT_RPOIB", /* Redundancy protocol over IB packet device */
  "MOD_IB_SMA",    /* Subnet Management Agent */
  "MOD_IB_PMA",    /* Performance Management Agent */
  "MOD_SYSCONF",
  "IB_LOGM",       /* Infiniband subsystem log management */

  /* Add new modules above this line. */
  "MAX"
};
#endif

#endif /* _ALL_COMMON_MODULE_NAMES_H */
