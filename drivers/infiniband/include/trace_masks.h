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

  $Id: trace_masks.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TRACE_MASKS_H
#define _TRACE_MASKS_H

/*
  Standard masks shared by all modules take the high-order 16 bits of
  the 32-bit flow mask; each module is free to define its own flows
  using the lower 16 bits.
*/

/* Standard masks */

#define TRACE_FLOW_FATAL             0x00010000UL /* fatal error */
#define TRACE_FLOW_WARN              0x00020000UL /* non-fatal error */
#define TRACE_FLOW_CONFIG            0x00040000UL /* configuration change */
#define TRACE_FLOW_INOUT             0x00080000UL /* enter/exit function */
#define TRACE_FLOW_INIT              0x00100000UL /* module initialization */
#define TRACE_FLOW_CLEANUP           0x00200000UL /* module cleanup */
#define TRACE_FLOW_STAGE             0x00400000UL /* significant execution stages */
#define TRACE_FLOW_DATA              0x00800000UL /* data path execution */
#define TRACE_FLOW_EVENTLOOP         0x01000000UL /* data path execution */
#define TRACE_FLOW_ALL               0xffffffffUL


/* Event message values */
#define TRACE_EVENT_CONFIG           0x00000000UL /* configuration event */
#define TRACE_EVENT_INFO             0x00000001UL /* informational event */
#define TRACE_EVENT_WARN             0x00000002UL /* warning event - nothing to do */
#define TRACE_EVENT_ERROR            0x00000003UL /* non-fatal event - do something */
#define TRACE_EVENT_FATAL            0x00000004UL /* fatal event */


/* Common Module */
#define TRACE_COM_GEN           0x00000001
#define TRACE_COM_MAIN          0x00000002
#define TRACE_COM_ALL           0x0000ffff


/* SysConf Module */
#define TRACE_SYSCONF_INFO      0x00000001


/* AMF Module */
#define TRACE_AMF_GEN           0x00000001  /* general func enter/exit */
#define TRACE_AMF_API           0x00000002  /* api flow */
#define TRACE_AMF_SEND          0x00000004  /* sending flow */
#define TRACE_AMF_RECV          0x00000008  /* receive flow */
#define TRACE_AMF_MSG           0x00000010  /* message flow */
#define TRACE_AMF_HELLO_MSG     0x00000020  /* hello-message flow */
#define TRACE_AMF_ALL           0x0000ffff  /* everything */


/* SNMP Module */
#define TRACE_SNMP_ENTER        0x00000001
#define TRACE_SNMP_EXIT         0x00000002
#define TRACE_SNMP_GEN          0x00000004
#define TRACE_SNMP_GET          0x00000008
#define TRACE_SNMP_GETNEXT      0x00000010
#define TRACE_SNMP_TEST         0x00000020
#define TRACE_SNMP_SET          0x00000040
#define TRACE_SNMP_IPC          0x00000080
#define TRACE_SNMP_SEND         0x00000100
#define TRACE_SNMP_RECV         0x00000200
#define TRACE_SNMP_ALL          0x0000ffff


/* Port Manager Module */
#define TRACE_PORT_INOUT        0x00000001
#define TRACE_PORT_ENTER        0x00000001
#define TRACE_PORT_GEN          0x00000002
#define TRACE_PORT_INSTR        0x00000004
#define TRACE_PORT_POLL         0x00000008
#define TRACE_PORT_TBL          0x00000010
#define TRACE_PORT_MSG          0x00000020
#define TRACE_PORT_TXQUEUE      0x00000040
#define TRACE_PORT_ALL          0x0000ffff


/* Chassis Manager Module */
#define TRACE_CHAS_INOUT        0x00000001
#define TRACE_CHAS_ENTER        0x00000001
#define TRACE_CHAS_EXIT         0x00000002
#define TRACE_CHAS_RECV         0x00000004
#define TRACE_CHAS_SEND         0x00000008
#define TRACE_CHAS_GEN          0x00000010
#define TRACE_CHAS_CARD         0x00000020
#define TRACE_CHAS_POLL         0x00000040
#define TRACE_CHAS_DEV          0x00000080
#define TRACE_CHAS_USER         0x00000100
#define TRACE_CHAS_CARD_AGENT   0x00000200
#define TRACE_CHAS_UPGRADE      0x00000400
#define TRACE_CHAS_CARD_EXT     0x00000800
#define TRACE_CHAS_EVENTS       0x00001000
#define TRACE_CHAS_FILE         0x00002000
#define TRACE_CHAS_ALL          0x0000ffff


/* CLI Module */
#define TRACE_CLI_GEN           0x00000001
#define TRACE_CLI_RCC           0x00000002  /* rapidlogic code */
#define TRACE_CLI_FTP           0x00000004
#define TRACE_CLI_SHOW_RUNNING_CONFIG 0x00000008
#define TRACE_CLI_FILESYSTEM    0x00000010
#define TRACE_CLI_ALL           0x0000ffff


/* Network Subsystem Module */
#define TRACE_NET_GEN           0x00000001
#define TRACE_NET_API           0x00000002
#define TRACE_NET_ALL           0x0000ffff


/* Management Module */
#define TRACE_MGMT_ENTER        0x00000001
#define TRACE_MGMT_EXIT         0x00000002
#define TRACE_MGMT_GEN          0x00000004
#define TRACE_MGMT_IPC          0x00000008
#define TRACE_MGMT_ALL          0x0000ffff


/* TB and Spanning Tree Protocol Module */
#define TRACE_STP_ENTER         0x00000001
#define TRACE_STP_EXIT          0x00000002
#define TRACE_STP_GEN           0x00000004
#define TRACE_STP_IPC           0x00000008
#define TRACE_TBSTP_MGMT_INIT   0x00000010
#define TRACE_TBSTP_MGMT_START  0x00000020
#define TRACE_TBSTP_MGMT_EXIT   0x00000040
#define TRACE_TBSTP_TIMER       0x00000080
#define TRACE_TB_ENTER          0x00000100
#define TRACE_TB_EXIT           0x00000200
#define TRACE_TB_GEN            0x00000400
#define TRACE_TB_IPC            0x00000800
#define TRACE_TBSTP_ALL         0x0000ffff


/* VLAN Manager Module */
#define TRACE_VLAN_ENTER        0x00000001
#define TRACE_VLAN_EXIT         0x00000002
#define TRACE_VLAN_GEN          0x00000004
#define TRACE_VLAN_IPC          0x00000008
#define TRACE_VLAN_UTIL         0x00000010
#define TRACE_VLAN_ALL          0x0000ffff


/* Infiniband Module */
#define TRACE_IB_SM             0x00000001
#define TRACE_IB_SA             0x00000002
#define TRACE_IB_TRAP           0x00000004
#define TRACE_IB_REDUNDANT      0x00000008
#define TRACE_IB_RMPP           0x00000010
#define TRACE_IB_PARTITION      0x00000020
#define TRACE_IB_GEN            0x00001000
#define TRACE_IB_TS_MGMT        0x00002000
#define TRACE_IB_MAD_PROC       0x00008000
#define TRACE_IB_ALL            0x0000ffff


/* Infiniband Module Logging Management */
#define TRACE_IB_LOGM_NO_TS_LOG               0x00000001
#define TRACE_IB_LOGM_TS_LOG_NO_DISPLAY       0x00000002
#define TRACE_IB_LOGM_LOG_TO_RAM              0x00000004
#define TRACE_IB_LOGM_OUTPUT_RAM_LOG          0x00001000


/* OSPF Module */
#define TRACE_OSPF_GEN          0x00000001
#define TRACE_OSPF_RECV         0x00000002
#define TRACE_OSPF_SEND         0x00000004


/* RIP Module */
#define TRACE_RIP_GEN           0x00000001
#define TRACE_RIP_ALL           0x0000ffff


/* VRRP Module */
#define TRACE_VRRP_GEN          0x00000001
#define TRACE_VRRP_ALL          0x0000ffff


/* TopSpin FPGA and ASIC modules */
#define TRACE_HW_GEN            0x00000001
#define TRACE_HW_SIM            0x00000002
#define TRACE_HW_INFO           0x00000004
#define TRACE_HW_ALL            0x0000ffff


/* SRP Module */
#define TRACE_SRP_INOUT         0x00000001
#define TRACE_SRP_INFO          0x00000002
#define TRACE_SRP_AGENT         0x00000004
#define TRACE_SRP_RECV          0x00000008
#define TRACE_SRP_SEND          0x00000010
#define TRACE_SRP_STAT          0x00000020
#define TRACE_SRP_SUPRESS_AUTOCREATE        0x00000040   // do not configlog entries by auto detect (srpm)
#define TRACE_SRP_SUPRESS_ITL_STAT          0x00000080   // do not send itl stat to mgr    (srpa)
#define TRACE_SRP_SUPRESS_GLOBAL_STAT       0x00000100   // do not send global stat to mgr (srpa)
#define TRACE_SRP_GET_DBG                   0x00000200   // do not send global stat to mgr (srpa)

#define TRACE_SRP_ALL           0x0000ffff


/* SRPTP Module */
#define TRACE_SRPTP_INOUT       0x00000001   /* enter/exit */
#define TRACE_SRPTP_STAGE       0x00000002   /* significant execution stages */
#define TRACE_SRPTP_WARN        0x00000004   /* warning */
#define TRACE_SRPTP_FATAL       0x00000008   /* fatal error */
#define TRACE_SRPTP_ALL         0x0000ffff


/* System Module */
#define TRACE_SYS_GEN           0x00000001
#define TRACE_SYS_ALL           0x0000ffff

/* Notifier Module */
#define TRACE_NOTIFIER_ENTER    0x00000001
#define TRACE_NOTIFIER_EXIT     0x00000002
#define TRACE_NOTIFIER_RECV     0x00000004
#define TRACE_NOTIFIER_SEND     0x00000008
#define TRACE_NOTIFIER_INTRN    0x00000010
#define TRACE_NOTIFIER_ALL      0x0000ffff

/* Kernel IB core API */
#define TRACE_KERNEL_IB_GEN     0x00000001  /* general messages */
#define TRACE_KERNEL_IB_ALL     0x0000ffff

/* Kernel IB CM */
#define TRACE_IB_CM_GEN         0x00000001  /* general messages */
#define TRACE_IB_CM_ACTIVE      0x00000002  /* active side of connection */
#define TRACE_IB_CM_PASSIVE     0x00000004  /* passive side of connection */
#define TRACE_IB_CM_ALL         0x0000ffff

/* IP Manager Module */
#define TRACE_IPMGR_ENTER        0x00000001
#define TRACE_IPMGR_GEN          0x00000002
#define TRACE_IPMGR_INSTR        0x00000004
#define TRACE_IPMGR_POLL         0x00000008
#define TRACE_IPMGR_TBL          0x00000010
#define TRACE_IPMGR_ALL          0x0000ffff

/* Kernel IPoIB net driver */

#define TRACE_IB_NET_GEN         0x00000001
#define TRACE_IB_NET_ARP         0x00000002
#define TRACE_IB_NET_MULTICAST   0x00000004

/* Port Agent - Ethernet Module */
#define TRACE_PA_EN_GEN          0x00000001
#define TRACE_PA_EN_LINK_EVENT   0x00000002
#define TRACE_PA_EN_IF_STAT      0x00000004
#define TRACE_PA_EN_FC_STAT      0x00000008
#define TRACE_PA_EN_ALL          0x0000ffff

/* Port Agent - Fibre Channel */
#define TRACE_PA_FC_GEN          0x00000001
#define TRACE_PA_FC_LINK_EVENT   0x00000002
#define TRACE_PA_FC_IF_STAT      0x00000004
#define TRACE_PA_FC_FC_STAT      0x00000008
#define TRACE_PA_FC_ALL          0x0000ffff

/* Gateway Core */
#define TRACE_GW_CORE_GEN        0x00000001  /* general messages */
#define TRACE_GW_ARP             0x00000002  /* ARP related */
#define TRACE_GW_PROC_READ       0x00000004  /* proc read interface */
#define TRACE_GW_PROC_WRITE      0x00000008  /* proc write interface */
#define TRACE_GW_EVENTS          0x00000010  /* Events (callbacks, etc) */
#define TRACE_GW_BRIDGE          0x00000020  /* Bridge related */
#define TRACE_GW_FFE             0x00000040  /* Fast forwarding engine support */
#define TRACE_GW_TSRP            0x00000080  /* Topspin Redundancy Protocol */


/* IP Agent */
#define TRACE_IPA_ENTER          0x00000001
#define TRACE_IPA_EXIT           0x00000002
#define TRACE_IPA_GEN            0x00000004
#define TRACE_IPA_IPC            0x00000008
#define TRACE_IPA_MSG            0x00000010
#define TRACE_IPA_UTIL           0x00000020
#define TRACE_IPA_TIMER          0x00000040
#define TRACE_IPA_ALL            0x0000ffff


/* FIB Manager */
#define TRACE_FIBM_ENTER         0x00000001
#define TRACE_FIBM_EXIT          0x00000002
#define TRACE_FIBM_GEN           0x00000004
#define TRACE_FIBM_IPC           0x00000008
#define TRACE_FIBM_MSG           0x00000010
#define TRACE_FIBM_ERROR         0x00000020
#define TRACE_FIBM_TIMER         0x00000040
#define TRACE_FIBM_ALL           0x0000ffff

/* FIB Agent */
#define TRACE_FIBA_ENTER         0x00000001
#define TRACE_FIBA_EXIT          0x00000002
#define TRACE_FIBA_GEN           0x00000004
#define TRACE_FIBA_IPC           0x00000008
#define TRACE_FIBA_MSG           0x00000010
#define TRACE_FIBA_UTIL          0x00000020
#define TRACE_FIBA_TIMER         0x00000040
#define TRACE_FIBA_ALL           0x0000ffff

/* Manager Agent IPC */
#define TRACE_MA_IPC_ENTER       0x00000001
#define TRACE_MA_IPC_EXIT        0x00000002
#define TRACE_MA_IPC_GEN         0x00000004
#define TRACE_MA_IPC_IPC         0x00000008
#define TRACE_MA_IPC_ALL         0x0000ffff

/* system srp-mgr and srp-agent common */
#define TRACE_SRP_PI             0x00000001    /* policy initiator */

/* system srp-agent */
#define TRACE_SRPA_PI            0x00000001    /* policy initiator */
#define TRACE_SRPA_GEN           0x00000004
#define TRACE_SRPA_IPC           0x00000008
#define TRACE_SRPA_MSG           0x00000010
#define TRACE_SRPA_ALL           0x0000ffff

/* system rem-mgr and rem-agent */
#define TRACE_WD_LOG             0x00000001
#define TRACE_WD_CODEFLOW        0x00000002
#define TRACE_WD_IPC             0x00000004
#define TRACE_WD_PULSE           0x00000008
#define TRACE_WD_STOP_MONITORING 0x00001000
#define TRACE_WD_NOTIFY          0x00002000
#define TRACE_WD_ALL             0x0000ffff

/* Kernel IB core API */
#define TRACE_KERNEL_IB_DM_GEN   0x00000001  /* general messages */
#define TRACE_KERNEL_IB_DM_ALL   0x0000ffff

/* Diagnostic Manager + Diagnostic Agent */
#define TRACE_DIAG_INSTR         0x00000001
#define TRACE_DIAG_PORT_TBL      0x00000002
#define TRACE_DIAG_PORT_INSTR    0x00000004
#define TRACE_DIAG_POLL          0x00000008
#define TRACE_DIAG_MSG           0x00000010
#define TRACE_DIAG_AGENT         0x00000020
#define TRACE_DIAG_CARD_TBL      0x00000040
#define TRACE_DIAG_CARD_INSTR    0x00000080
#define TRACE_DIAG_CHASSIS_GRP   0x00000100
#define TRACE_DIAG_CHASSIS_INSTR 0x00000200
#define TRACE_DIAG_ALL           0xffffffff

/* FIG Hardware Access */
#define TRACE_FIG_GEN            0x00000001  /* General messages            */
#define TRACE_FIG_HWIF           0x00000002  /* Hardware access library     */
#define TRACE_FIG_DEV_IB         0x00000004  /* Infiband device module      */
#define TRACE_FIG_DEV_EN         0x00000008  /* Ethernet device module      */
#define TRACE_FIG_DEV_FW         0x00000010  /* Forwarding device module    */
#define TRACE_FIG_DEV_MISC       0x00000020  /* Miscellaneous device module */
#define TRACE_FIG_PROC           0x00000040  /* Proc file system module     */
#define TRACE_FIG_HWIF_REGS      0x00000080  /* HWIF Register access        */
#define TRACE_FIG_HWIF_FUNC      0x00000100  /* HWIF control flow           */
#define TRACE_FIG_DEV_EN_FUNC    0x00000200  /* EN device control flow      */
#define TRACE_FIG_DEV_FW_FUNC    0x00000400  /* FW device control flow      */
#define TRACE_FIG_DEV_MISC_FUNC  0x00000800  /* Misc module control flow    */
#define TRACE_FIG_DEV_IB_FUNC    0x00001000  /* IB device control flow      */
#define TRACE_FIG_ALL            0xffffffff

/* SMA */
#define TRACE_SMA_GEN            0x00000001  /* General messages            */
#define TRACE_SMA_COMPLIANCE     0x00000002  /* Compliance testing          */
#define TRACE_SMA_ALL            0x0000ffff

/* PMA */
#define TRACE_PMA_GEN            0x00000001  /* General messages            */
#define TRACE_PMA_COMPLIANCE     0x00000002  /* Compliance testing          */
#define TRACE_PMA_ALL            0x0000ffff

/* system default values */
#define TS_TRACE_MASK_USER_DEFVAL          0x0
#define TS_TRACE_MASK_KERNEL_DEFVAL        TRACE_FLOW_ALL


#endif /* _TRACE_MASKS_H */
