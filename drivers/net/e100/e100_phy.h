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

#ifndef _E100_PHY_INC_
#define _E100_PHY_INC_

#include "e100.h"

#include <linux/mii.h>

/*
 * Auto-polarity enable/disable
 * e100_autopolarity = 0 => disable auto-polarity
 * e100_autopolarity = 1 => enable auto-polarity
 * e100_autopolarity = 2 => let software determine
 */
#define E100_AUTOPOLARITY 2

#define IS_NC3133(bdp) (((bdp)->pdev->subsystem_vendor == 0x0E11) && \
                        ((bdp)->pdev->subsystem_device == 0xB0E1))

#define PHY_503                 0
#define PHY_100_A               0x000003E0
#define PHY_100_C               0x035002A8
#define PHY_NSC_TX              0x5c002000
#define PHY_82562ET             0x033002A8
#define PHY_82562EM             0x032002A8
#define PHY_82562EH             0x017002A8
#define PHY_82555_TX            0x015002a8	/* added this for 82555 */
#define PHY_OTHER               0xFFFF
#define MAX_PHY_ADDR            31
#define MIN_PHY_ADDR            0

#define PHY_MODEL_REV_ID_MASK   0xFFF0FFFF

#define PHY_DEFAULT_ADDRESS 1
#define PHY_ADDRESS_503 32

/* MDI Control register bit definitions */
#define MDI_PHY_READY	    BIT_28	/* PHY is ready for next MDI cycle */

#define MDI_NC3133_CONFIG_REG           0x19
#define MDI_NC3133_100FX_ENABLE         BIT_2
#define MDI_NC3133_INT_ENABLE_REG       0x17
#define MDI_NC3133_INT_ENABLE           BIT_1

/* MDI Control register opcode definitions */
#define MDI_WRITE 1		/* Phy Write */
#define MDI_READ  2		/* Phy read */

/* MDI register set*/
#define AUTO_NEG_NEXT_PAGE_REG	    0x07	/* Auto-negotiation next page xmit */
#define EXTENDED_REG_0		    0x10	/* Extended reg 0 (Phy 100 modes) */
#define EXTENDED_REG_1		    0x14	/* Extended reg 1 (Phy 100 error indications) */
#define NSC_CONG_CONTROL_REG	    0x17	/* National (TX) congestion control */
#define NSC_SPEED_IND_REG	    0x19	/* National (TX) speed indication */

#define HWI_CONTROL_REG             0x1D	/* HWI Control register */
/* MDI/MDI-X Control Register bit definitions */
#define MDI_MDIX_RES_TIMER          BIT_0_3	/* minimum slot time for resolution timer */
#define MDI_MDIX_CONFIG_IS_OK       BIT_4	/* 1 = resolution algorithm completes OK */
#define MDI_MDIX_STATUS             BIT_5	/* 1 = MDIX (croos over), 0 = MDI (straight through) */
#define MDI_MDIX_SWITCH             BIT_6	/* 1 = Forces to MDIX, 0 = Forces to MDI */
#define MDI_MDIX_AUTO_SWITCH_ENABLE BIT_7	/* 1 = MDI/MDI-X feature enabled */
#define MDI_MDIX_CONCT_CONFIG       BIT_8	/* Sets the MDI/MDI-X connectivity configuration (test prupose only) */
#define MDI_MDIX_CONCT_TEST_ENABLE  BIT_9	/* 1 = Enables connectivity testing */
#define MDI_MDIX_RESET_ALL_MASK     0x0000

/* HWI Control Register bit definitions */
#define HWI_TEST_DISTANCE           BIT_0_8	/* distance to cable problem */
#define HWI_TEST_HIGHZ_PROBLEM      BIT_9	/* 1 = Open Circuit */
#define HWI_TEST_LOWZ_PROBLEM       BIT_10	/* 1 = Short Circuit */
#define HWI_TEST_RESERVED           (BIT_11 | BIT_12)	/* reserved */
#define HWI_TEST_EXECUTE            BIT_13	/* 1 = Execute the HWI test on the PHY */
#define HWI_TEST_ABILITY            BIT_14	/* 1 = test passed */
#define HWI_TEST_ENABLE             BIT_15	/* 1 = Enables the HWI feature */
#define HWI_RESET_ALL_MASK          0x0000

/* ############Start of 82555 specific defines################## */

/* Intel 82555 specific registers */
#define PHY_82555_CSR		    0x10	/* 82555 CSR */
#define PHY_82555_SPECIAL_CONTROL   0x11	/* 82555 special control register */

#define PHY_82555_RCV_ERR	    0x15	/* 82555 100BaseTx Receive Error
						 * Frame Counter */
#define PHY_82555_SYMBOL_ERR	    0x16	/* 82555 RCV Symbol Error Counter */
#define PHY_82555_PREM_EOF_ERR	    0x17	/* 82555 100BaseTx RCV Premature End
						 * of Frame Error Counter */
#define PHY_82555_EOF_COUNTER	    0x18	/* 82555 end of frame error counter */
#define PHY_82555_MDI_EQUALIZER_CSR 0x1a	/* 82555 specific equalizer reg. */

/* 82555 CSR bits */
#define PHY_82555_SPEED_BIT    BIT_1
#define PHY_82555_POLARITY_BIT BIT_8

/* 82555 equalizer reg. opcodes */
#define ENABLE_ZERO_FORCING  0x2010	/* write to ASD conf. reg. 0 */
#define DISABLE_ZERO_FORCING 0x2000	/* write to ASD conf. reg. 0 */

/* 82555 special control reg. opcodes */
#define DISABLE_AUTO_POLARITY 0x0010
#define EXTENDED_SQUELCH_BIT  BIT_2

/* ############End of 82555 specific defines##################### */

/* Auto-Negotiation advertisement register bit definitions*/
#define NWAY_AD_FC_SUPPORTED    0x0400	/* Flow Control supported */

/* Auto-Negotiation link partner ability register bit definitions*/
#define NWAY_LP_ABILITY	        0x07e0	/* technologies supported */

/* PHY 100 Extended Register 0 bit definitions*/
#define PHY_100_ER0_FDX_INDIC	BIT_0	/* 1 = FDX, 0 = half duplex */
#define PHY_100_ER0_SPEED_INDIC BIT_1	/* 1 = 100Mbps, 0= 10Mbps */

/* National Semiconductor TX phy congestion control register bit definitions*/
#define NSC_TX_CONG_TXREADY  BIT_10	/* Makes TxReady an input */
#define NSC_TX_CONG_ENABLE   BIT_8	/* Enables congestion control */

/* National Semiconductor TX phy speed indication register bit definitions*/
#define NSC_TX_SPD_INDC_SPEED BIT_6	/* 0 = 100Mbps, 1=10Mbps */

/************* function prototypes ************/
extern unsigned char e100_phy_init(struct e100_private *bdp);
extern unsigned char e100_update_link_state(struct e100_private *bdp);
extern unsigned char e100_phy_check(struct e100_private *bdp);
extern void e100_phy_set_speed_duplex(struct e100_private *bdp,
				      unsigned char force_restart);
extern void e100_phy_reset(struct e100_private *bdp);
extern void e100_mdi_write(struct e100_private *, u32, u32, u16);
extern void e100_mdi_read(struct e100_private *, u32, u32, u16 *);

#endif
