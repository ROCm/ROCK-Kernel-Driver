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

  $Id: module_codes.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _ALL_COMMON_MODULE_CODES_H
#define _ALL_COMMON_MODULE_CODES_H

/*
  This include file is included in both common library code and the
  kernel trace module.  Don't add any more declarations or definitions
  to this file; create a new file instead!
*/

typedef enum
{
  /*
    If you add a new module to this list, you'll also need to update
    the name array ModuleNames[] in all/common/include/module_names.h

    Then let the maintainer of the CLI know that the module needs to
    be added.  Currently the maintainer is Nancy Chang (nancy@topspin.com).
  */

  MOD_COMMON    = 0,          /* common */
  MOD_AMF       = 1,          /* application management framework */
  MOD_SNMP      = 2,          /* snmp agent */
  MOD_PORT      = 3,          /* port manager */
  MOD_CHAS      = 4,          /* chassis manger */
  MOD_CLI       = 5,          /* command cli interface */
  MOD_NET       = 6,          /* network subsystem */
  MOD_MGMT      = 7,          /* management module */
  MOD_STP       = 8,          /* spanning tree protocol */
  MOD_VLAN      = 9,          /* vlan manager */
  MOD_IB        = 10,         /* infiniband subsystem */
  MOD_OSPF      = 11,         /* ospf */
  MOD_RIP       = 12,         /* rip */
  MOD_VRRP      = 13,         /* vrrp */
  MOD_HW        = 14,         /* All Topspin FPGAs and ASICs, TCP/IP/Socket */
  MOD_SRP       = 15,         /* SRP module */
  MOD_SYS       = 16,         /* system */
  MOD_NOTIFIER  = 17,         /* system */
  MOD_SRPTP     = 18,         /* SRP transport provider module */
  MOD_KERNEL_IB = 19,         /* Kernel IB core API */
  MOD_IB_CM     = 20,         /* IB CM */
  MOD_IB_NET    = 21,         /* Linux native IPoIB packet driver */
  MOD_IPMGR     = 22,         /* IP manager */
  MOD_PA_EN     = 23,         /* Port Agent ethernet */
  MOD_PA_IB     = 24,         /* Port Agent IB */
  MOD_PA_FC     = 25,         /* Port Agent FC */
  MOD_PA_GW     = 26,         /* Port Agent Gateway */
  MOD_STRM_SDP  = 27,         /* Networking Gateway SDP protocol */
  MOD_STRM_TCP  = 28,         /* Networking Gateway TCP protocol */
  MOD_STRM_USR  = 29,         /* Networking Gateway USR interface  protocol */
  MOD_GW_CORE   = 30,         /* Networking Gateway Framework */
  MOD_PKT_PROC  = 31,         /* Networking Gateway Proc Packet Driver */
  MOD_PKT_E1000 = 32,         /* Networking Gateway Intel 82544 Packet Driver*/
  MOD_PKT_IPOIB = 33,         /* Networking Gateway IPoIB Packet Driver */
  MOD_IPA       = 34,         /* IP Agent */
  MOD_FIB_MGR   = 35,         /* FIB Manager */
  MOD_FIB_AGENT = 36,         /* FIB Agent */
  MOD_JNI       = 37,         /* JNI HBA driver */
  MOD_GW_TEST   = 38,         /* Networking Gateway test modules */
  MOD_PKT_SHIM  = 39,         /* Networking Gateway shim Packet Driver */
  MOD_MA_IPC    = 40,         /* Manager Agent IPC library */
  MOD_SRP_LIB   = 41,         /* system srp_lib */
  MOD_SRPM      = 42,         /* system srp_mgr */
  MOD_SRPA      = 43,         /* system srp_agent */
  MOD_PKT_RNDIS = 44,         /* Networking Gateway RNDIS Packet Driver */
  MOD_POLL      = 45,         /* kernel poll loop */
  MOD_RNDIS_LIB = 46,         /* system rndis_lib */
  MOD_RNDISM    = 47,         /* system rndis_mgr */
  MOD_RNDISA    = 48,         /* system rndis_agent */
  MOD_WD        = 49,         /* system watchdog */
  MOD_PKT_IPOETH= 50,         /* Networking Gateway IPoETH Packet Driver */
  MOD_STRM_LNX  = 51,         /* Networking Gateway LNX interface  protocol */
  MOD_LNX_SDP   = 52,         /* Linux host SDP INET module */
  MOD_UDAPL     = 53,         /* uDAPL kernel support driver */
  MOD_KERNEL_DM = 54,         /* Kernel IB DM */
  MOD_PKT_EN_TRUNK = 55,      /* Networking Gateway Ethernet Trunk Packet Driver */
  MOD_DIAG      = 56,         /* Diagnostic */
  MOD_FIG       = 57,         /* FIG Hardware access module */
  MOD_PKT_EN_FIG= 58,         /* FIG Ethernet packet driver */
  MOD_IP2PR     = 59,         /* IP to Path Record resolution */
  MOD_FFE_SWC   = 60,         /* Software C based FFE */
  MOD_WMI       = 61,         /* Web Management Interface */
  MOD_FFE_FIG   = 62,         /* FIG based FFE */
  MOD_SCORE     = 63,         /* SCore */
  MOD_PKT_RPOIB = 64,         /* Networking Gateway RPoIB Packet Driver */
  MOD_IB_SMA    = 65,         /* Subnet Management Agent */
  MOD_IB_PMA    = 66,         /* Performance Management Agent */
  MOD_SYSCONF   = 67,         /* all/sysconf module */
  MOD_IB_LOGM   = 68,         /* Infiniband subsystem log management */

    /* Add above this line */
  MOD_MAX                     /* range is between MOD_FIRST and MOD_MAX */

} tMODULE_ID;

#endif /* _ALL_COMMON_MODULE_CODES_H */
