/*******************************************************************************

This software program is available to you under a choice of one of two 
licenses. You may choose to be licensed under either the GNU General Public 
License 2.0, June 1991, available at http://www.fsf.org/copyleft/gpl.html, 
or the Intel BSD + Patent License, the text of which follows:

Recipient has requested a license and Intel Corporation ("Intel") is willing
to grant a license for the software entitled Linux Base Driver for the 
Intel(R) PRO/100 Family of Adapters (e100) (the "Software") being provided 
by Intel Corporation. The following definitions apply to this license:

"Licensed Patents" means patent claims licensable by Intel Corporation which 
are necessarily infringed by the use of sale of the Software alone or when 
combined with the operating system referred to below.

"Recipient" means the party to whom Intel delivers this Software.

"Licensee" means Recipient and those third parties that receive a license to 
any operating system available under the GNU General Public License 2.0 or 
later.

Copyright (c) 1999 - 2002 Intel Corporation.
All rights reserved.

The license is provided to Recipient and Recipient's Licensees under the 
following terms.

Redistribution and use in source and binary forms of the Software, with or 
without modification, are permitted provided that the following conditions 
are met:

Redistributions of source code of the Software may retain the above 
copyright notice, this list of conditions and the following disclaimer.

Redistributions in binary form of the Software may reproduce the above 
copyright notice, this list of conditions and the following disclaimer in 
the documentation and/or materials provided with the distribution.

Neither the name of Intel Corporation nor the names of its contributors 
shall be used to endorse or promote products derived from this Software 
without specific prior written permission.

Intel hereby grants Recipient and Licensees a non-exclusive, worldwide, 
royalty-free patent license under Licensed Patents to make, use, sell, offer 
to sell, import and otherwise transfer the Software, if any, in source code 
and object code form. This license shall include changes to the Software 
that are error corrections or other minor changes to the Software that do 
not add functionality or features when the Software is incorporated in any 
version of an operating system that has been distributed under the GNU 
General Public License 2.0 or later. This patent license shall apply to the 
combination of the Software and any operating system licensed under the GNU 
General Public License 2.0 or later if, at the time Intel provides the 
Software to Recipient, such addition of the Software to the then publicly 
available versions of such operating systems available under the GNU General 
Public License 2.0 or later (whether in gold, beta or alpha form) causes 
such combination to be covered by the Licensed Patents. The patent license 
shall not apply to any other combinations which include the Software. NO 
hardware per se is licensed hereunder.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
IMPLIED WARRANTIES OF MECHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR IT CONTRIBUTORS BE LIABLE FOR ANY 
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
(INCLUDING, BUT NOT LIMITED, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
ANY LOSS OF USE; DATA, OR PROFITS; OR BUSINESS INTERUPTION) HOWEVER CAUSED 
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR 
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

#ifndef _E100_CONFIG_INC_
#define _E100_CONFIG_INC_

#include "e100.h"

#define E100_CONFIG(bdp, X) ((bdp)->config[0] = max_t(u8, (bdp)->config[0], (X)+1))

#define CB_CFIG_MIN_PARAMS         8

/* byte 0 bit definitions*/
#define CB_CFIG_BYTE_COUNT_MASK     BIT_0_5	/* Byte count occupies bit 5-0 */

/* byte 1 bit definitions*/
#define CB_CFIG_RXFIFO_LIMIT_MASK   BIT_0_4	/* RxFifo limit mask */
#define CB_CFIG_TXFIFO_LIMIT_MASK   BIT_4_7	/* TxFifo limit mask */

/* byte 2 bit definitions -- ADAPTIVE_IFS*/

/* word 3 bit definitions -- RESERVED*/
/* Changed for 82558 enhancements */
/* byte 3 bit definitions */
#define CB_CFIG_MWI_EN      BIT_0	/* Enable MWI on PCI bus */
#define CB_CFIG_TYPE_EN     BIT_1	/* Type Enable */
#define CB_CFIG_READAL_EN   BIT_2	/* Enable Read Align */
#define CB_CFIG_TERMCL_EN   BIT_3	/* Cache line write  */

/* byte 4 bit definitions*/
#define CB_CFIG_RX_MIN_DMA_MASK     BIT_0_6	/* Rx minimum DMA count mask */

/* byte 5 bit definitions*/
#define CB_CFIG_TX_MIN_DMA_MASK BIT_0_6	/* Tx minimum DMA count mask */
#define CB_CFIG_DMBC_EN         BIT_7	/* Enable Tx/Rx min. DMA counts */

/* Changed for 82558 enhancements */
/* byte 6 bit definitions*/
#define CB_CFIG_LATE_SCB           BIT_0	/* Update SCB After New Tx Start */
#define CB_CFIG_DIRECT_DMA_DIS     BIT_1	/* Direct DMA mode */
#define CB_CFIG_TNO_INT            BIT_2	/* Tx Not OK Interrupt */
#define CB_CFIG_TCO_STAT           BIT_2	/* TCO statistics in 559 and above */
#define CB_CFIG_CI_INT             BIT_3	/* Command Complete Interrupt */
#define CB_CFIG_EXT_TCB_DIS        BIT_4	/* Extended TCB */
#define CB_CFIG_EXT_STAT_DIS       BIT_5	/* Extended Stats */
#define CB_CFIG_SAVE_BAD_FRAMES    BIT_7	/* Save Bad Frames Enabled */

/* byte 7 bit definitions*/
#define CB_CFIG_DISC_SHORT_FRAMES   BIT_0	/* Discard Short Frames */
#define CB_CFIG_DYNTBD_EN           BIT_7	/* Enable dynamic TBD */
/* Enable extended RFD's on D102 */
#define CB_CFIG_EXTENDED_RFD        BIT_5

/* byte 8 bit definitions*/
#define CB_CFIG_503_MII             BIT_0	/* 503 vs. MII mode */

/* byte 9 bit definitions -- pre-defined all zeros*/
#define CB_LINK_STATUS_WOL	BIT_5

/* byte 10 bit definitions*/
#define CB_CFIG_NO_SRCADR       BIT_3	/* No Source Address Insertion */
#define CB_CFIG_PREAMBLE_LEN    BIT_4_5	/* Preamble Length */
#define CB_CFIG_LOOPBACK_MODE   BIT_6_7	/* Loopback Mode */
#define CB_CFIG_LOOPBACK_NORMAL 0
#define CB_CFIG_LOOPBACK_INTERNAL BIT_6
#define CB_CFIG_LOOPBACK_EXTERNAL BIT_6_7

/* byte 11 bit definitions*/
#define CB_CFIG_LINEAR_PRIORITY     BIT_0_2	/* Linear Priority */

/* byte 12 bit definitions*/
#define CB_CFIG_LINEAR_PRI_MODE     BIT_0	/* Linear Priority mode */
#define CB_CFIG_IFS_MASK            BIT_4_7	/* Interframe Spacing mask */

/* byte 13 bit definitions -- pre-defined all zeros*/

/* byte 14 bit definitions -- pre-defined 0xf2*/

/* byte 15 bit definitions*/
#define CB_CFIG_PROMISCUOUS         BIT_0	/* Promiscuous Mode Enable */
#define CB_CFIG_BROADCAST_DIS       BIT_1	/* Broadcast Mode Disable */
#define CB_CFIG_CRS_OR_CDT          BIT_7	/* CRS Or CDT */

/* byte 16 bit definitions -- pre-defined all zeros*/
#define DFLT_FC_DELAY_LSB  0x1f	/* Delay for outgoing Pause frames */
#define DFLT_NO_FC_DELAY_LSB  0x00	/* no flow control default value */

/* byte 17 bit definitions -- pre-defined 0x40*/
#define DFLT_FC_DELAY_MSB  0x01	/* Delay for outgoing Pause frames */
#define DFLT_NO_FC_DELAY_MSB  0x40	/* no flow control default value */

/* byte 18 bit definitions*/
#define CB_CFIG_STRIPPING           BIT_0	/* Padding Disabled */
#define CB_CFIG_PADDING             BIT_1	/* Padding Disabled */
#define CB_CFIG_CRC_IN_MEM          BIT_2	/* Transfer CRC To Memory */

/* byte 19 bit definitions*/
#define CB_CFIG_TX_ADDR_WAKE        BIT_0	/* Address Wakeup */
#define CB_DISABLE_MAGPAK_WAKE      BIT_1	/* Magic Packet Wakeup disable */
/* Changed TX_FC_EN to TX_FC_DIS because 0 enables, 1 disables. Jul 8, 1999 */
#define CB_CFIG_TX_FC_DIS           BIT_2	/* Tx Flow Control Disable */
#define CB_CFIG_FC_RESTOP           BIT_3	/* Rx Flow Control Restop */
#define CB_CFIG_FC_RESTART          BIT_4	/* Rx Flow Control Restart */
#define CB_CFIG_FC_REJECT           BIT_5	/* Rx Flow Control Restart */
#define CB_CFIG_FC_OPTS (CB_CFIG_FC_RESTOP | CB_CFIG_FC_RESTART | CB_CFIG_FC_REJECT)

/* end 82558/9 specifics */

#define CB_CFIG_FORCE_FDX           BIT_6	/* Force Full Duplex */
#define CB_CFIG_FDX_ENABLE          BIT_7	/* Full Duplex Enabled */

/* byte 20 bit definitions*/
#define CB_CFIG_MULTI_IA            BIT_6	/* Multiple IA Addr */

/* byte 21 bit definitions*/
#define CB_CFIG_MULTICAST_ALL       BIT_3	/* Multicast All */

/* byte 22 bit defines */
#define CB_CFIG_RECEIVE_GAMLA_MODE  BIT_0	/* D102 receive mode */
#define CB_CFIG_VLAN_DROP_ENABLE    BIT_1	/* vlan stripping */

#define CB_CFIG_LONG_RX_OK	    BIT_3

#define NO_LOOPBACK	0	
#define MAC_LOOPBACK	0x01
#define PHY_LOOPBACK	0x02

/* function prototypes */
extern void e100_config_init(struct e100_private *bdp);
extern unsigned char e100_force_config(struct e100_private *bdp);
extern unsigned char e100_config(struct e100_private *bdp);
extern void e100_config_fc(struct e100_private *bdp);
extern void e100_config_promisc(struct e100_private *bdp, unsigned char enable);
extern void e100_config_brdcast_dsbl(struct e100_private *bdp);
extern void e100_config_mulcast_enbl(struct e100_private *bdp,
				     unsigned char enable);
extern void e100_config_ifs(struct e100_private *bdp);
extern void e100_config_force_dplx(struct e100_private *bdp);
extern u8 e100_config_loopback_mode(struct e100_private *bdp, u8 mode);
extern u8 e100_config_dynamic_tbd(struct e100_private *bdp, u8 enable);
extern u8 e100_config_tcb_ext_enable(struct e100_private *bdp, u8 enable);

#endif /* _E100_CONFIG_INC_ */
