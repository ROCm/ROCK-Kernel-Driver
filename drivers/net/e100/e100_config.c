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

/**********************************************************************
*                                                                     *
* INTEL CORPORATION                                                   *
*                                                                     *
* This software is supplied under the terms of the license included   *
* above.  All use of this driver must be in accordance with the terms *
* of that license.                                                    *
*                                                                     *
* Module Name:  e100_config.c                                         *
*                                                                     *
* Abstract:     Functions for configuring the network adapter.        *
*                                                                     *
* Environment:  This file is intended to be specific to the Linux     *
*               operating system.                                     *
*                                                                     *
**********************************************************************/
#include "e100_config.h"

static void e100_config_long_rx(struct e100_private *bdp, unsigned char enable);

static const u8 def_config[] = {
	CB_CFIG_BYTE_COUNT,
	0x08, 0x00, 0x00, 0x00, 0x00, 0x32, 0x07, 0x01,
	0x00, 0x2e, 0x00, 0x60, 0x00, 0xf2, 0xc8, 0x00,
	0x40, 0xf2, 0x80, 0x3f, 0x05
};

/**
 * e100_config_init_82557 - config the 82557 adapter
 * @bdp: atapter's private data struct
 *
 * This routine will initialize the 82557 configure block.
 * All other init functions will only set values that are
 * different from the 82557 default.
 */
static void __devinit
e100_config_init_82557(struct e100_private *bdp)
{
	/* initialize config block */
	memcpy(bdp->config, def_config, sizeof (def_config));
	bdp->config[0] = CB_CFIG_BYTE_COUNT;	/* just in case */

	e100_config_ifs(bdp);

	/*
	 * Enable extended statistical counters (82558 and up) and TCO counters
	 * (82559 and up) and set the statistical counters' mode in bdp 
	 *  
	 *  stat. mode      |    TCO stat. bit (2)  |  Extended stat. bit (5)
	 * ------------------------------------------------------------------
	 *  Basic (557)     |       0               |         1
	 * ------------------------------------------------------------------
	 *  Extended (558)  |       0               |         0
	 * ------------------------------------------------------------------
	 *  TCO (559)       |       1               |         1
	 * ------------------------------------------------------------------
	 *  Reserved        |       1               |         0
	 * ------------------------------------------------------------------
	 */
	bdp->config[6] &= ~CB_CFIG_TCO_STAT;
	bdp->config[6] |= CB_CFIG_EXT_STAT_DIS;
	bdp->stat_mode = E100_BASIC_STATS;

	/* Setup for MII or 503 operation.  The CRS+CDT bit should only be set */
	/* when operating in 503 mode. */
	if (bdp->phy_addr == 32) {
		bdp->config[8] &= ~CB_CFIG_503_MII;
		bdp->config[15] |= CB_CFIG_CRS_OR_CDT;
	} else {
		bdp->config[8] |= CB_CFIG_503_MII;
		bdp->config[15] &= ~CB_CFIG_CRS_OR_CDT;
	}

	e100_config_fc(bdp);
	e100_config_force_dplx(bdp);
	e100_config_promisc(bdp, false);
	e100_config_mulcast_enbl(bdp, false);
}

static void __devinit
e100_config_init_82558(struct e100_private *bdp)
{
	/* MWI enable. This should be turned on only if the adapter is a 82558/9
	 * and if the PCI command reg. has enabled the MWI bit. */
	bdp->config[3] |= CB_CFIG_MWI_EN;

	bdp->config[6] &= ~CB_CFIG_EXT_TCB_DIS;

	if (bdp->rev_id >= D101MA_REV_ID) {
		/* this is 82559 and up - enable TCO counters */
		bdp->config[6] |= CB_CFIG_TCO_STAT;
		bdp->config[6] |= CB_CFIG_EXT_STAT_DIS;
		bdp->stat_mode = E100_TCO_STATS;

		if ((bdp->rev_id < D102_REV_ID) &&
		    (bdp->params.b_params & PRM_XSUMRX) &&
		    (bdp->pdev->device != 0x1209)) {

			bdp->flags |= DF_CSUM_OFFLOAD;
			bdp->config[9] |= 1;
		}
	} else {
		/* this is 82558 */
		bdp->config[6] &= ~CB_CFIG_TCO_STAT;
		bdp->config[6] &= ~CB_CFIG_EXT_STAT_DIS;
		bdp->stat_mode = E100_EXTENDED_STATS;
	}

	e100_config_long_rx(bdp, true);
}

static void __devinit
e100_config_init_82550(struct e100_private *bdp)
{
	/* The D102 chip allows for 32 config bytes.  This value is
	 * supposed to be in Byte 0.  Just add the extra bytes to
	 * what was already setup in the block. */
	bdp->config[0] += CB_CFIG_D102_BYTE_COUNT;

	/* now we need to enable the extended RFD.  When this is
	 * enabled, the immediated receive data buffer starts at offset
	 * 32 from the RFD base address, instead of at offset 16. */
	bdp->config[7] |= CB_CFIG_EXTENDED_RFD;

	/* put the chip into D102 receive mode.  This is neccessary
	 * for any parsing and offloading features. */
	bdp->config[22] = CB_CFIG_RECEIVE_GAMLA_MODE;

	/* set the flag if checksum offloading was enabled */
	if (bdp->params.b_params & PRM_XSUMRX) {
		bdp->flags |= DF_CSUM_OFFLOAD;
	}
}

/* Initialize the adapter's configure block */
void __devinit
e100_config_init(struct e100_private *bdp)
{
	e100_config_init_82557(bdp);

	if (bdp->flags & IS_BACHELOR)
		e100_config_init_82558(bdp);

	if (bdp->rev_id >= D102_REV_ID)
		e100_config_init_82550(bdp);
}

/**
 * e100_force_config - force a configure command
 * @bdp: atapter's private data struct
 *
 * This routine will force a configure command to the adapter.
 * The command will be executed in polled mode as interrupts
 * are _disabled_ at this time.
 *
 * Returns:
 *      true: if the configure command was successfully issued and completed
 *      false: otherwise
 */
unsigned char
e100_force_config(struct e100_private *bdp)
{
	spin_lock_bh(&(bdp->config_lock));

	bdp->config[0] = CB_CFIG_BYTE_COUNT;
	if (bdp->rev_id >= D102_REV_ID) {
		/* The D102 chip allows for 32 config bytes.  This value is
		   supposed to be in Byte 0.  Just add the extra bytes to
		   what was already setup in the block. */
		bdp->config[0] += CB_CFIG_D102_BYTE_COUNT;
	}

	spin_unlock_bh(&(bdp->config_lock));

	// although we call config outside the lock, there is no
	// race condition because config byte count has maximum value
	return e100_config(bdp);
}

/**
 * e100_config - issue a configure command
 * @bdp: atapter's private data struct
 *
 * This routine will issue a configure command to the 82557.
 * This command will be executed in polled mode as interrupts
 * are _disabled_ at this time.
 *
 * Returns:
 *      true: if the configure command was successfully issued and completed
 *      false: otherwise
 */
unsigned char
e100_config(struct e100_private *bdp)
{
	cb_header_t *pntcb_hdr;
	unsigned char res = true;
	nxmit_cb_entry_t *cmd;

	if (bdp->config[0] == 0) {
		goto exit;
	}

	if ((cmd = e100_alloc_non_tx_cmd(bdp)) == NULL) {
		res = false;
		goto exit;
	}

	pntcb_hdr = (cb_header_t *) cmd->non_tx_cmd;
	pntcb_hdr->cb_cmd = __constant_cpu_to_le16(CB_CONFIGURE);

	spin_lock_bh(&bdp->config_lock);

	if (bdp->config[0] < CB_CFIG_MIN_PARAMS) {
		bdp->config[0] = CB_CFIG_MIN_PARAMS;
	}

	/* Copy the device's config block to the device's memory */
	memcpy(cmd->non_tx_cmd->ntcb.config.cfg_byte, bdp->config,
	       bdp->config[0]);
	/* reset number of bytes to config next time */
	bdp->config[0] = 0;

	spin_unlock_bh(&bdp->config_lock);

	res = e100_exec_non_cu_cmd(bdp, cmd);

exit:
	if (netif_running(bdp->device))
		netif_wake_queue(bdp->device);
	return res;
}

/**
 * e100_config_fc - config flow-control state
 * @bdp: adapter's private data struct
 *
 * This routine will enable or disable flow control support in the adapter's
 * config block. Flow control will be enable only if requested using the command
 * line option, and if the link is flow-contorl capable (both us and the link
 * partner). But, if link partner is capable of autoneg, but not capable of
 * flow control, received PAUSE	frames are still honored.
 */
void
e100_config_fc(struct e100_private *bdp)
{
	unsigned char enable = false;
	/* 82557 doesn't support fc. Don't touch this option */
	if (!(bdp->flags & IS_BACHELOR))
		return;

	/* Enable fc if requested and if the link supports it */
	if ((bdp->params.b_params & PRM_FC) && (bdp->flags & 
		(DF_LINK_FC_CAP | DF_LINK_FC_TX_ONLY))) {
		enable = true;
	}

	spin_lock_bh(&(bdp->config_lock));

	if (enable) {
		if (bdp->flags & DF_LINK_FC_TX_ONLY) {
			/* If link partner is capable of autoneg, but  */
			/* not capable of flow control, Received PAUSE */
			/* frames are still honored, i.e.,             */
			/* transmitted frames would be paused by       */
			/* incoming PAUSE frames                       */
			bdp->config[16] = DFLT_NO_FC_DELAY_LSB;
			bdp->config[17] = DFLT_NO_FC_DELAY_MSB;
			bdp->config[19] &= ~(CB_CFIG_FC_RESTOP | CB_CFIG_FC_RESTART);
			bdp->config[19] |= CB_CFIG_FC_REJECT;
			bdp->config[19] &= ~CB_CFIG_TX_FC_DIS;
		} else {
			bdp->config[16] = DFLT_FC_DELAY_LSB;
			bdp->config[17] = DFLT_FC_DELAY_MSB;
			bdp->config[19] |= CB_CFIG_FC_OPTS;
			bdp->config[19] &= ~CB_CFIG_TX_FC_DIS;
		}
	} else {
		bdp->config[16] = DFLT_NO_FC_DELAY_LSB;
		bdp->config[17] = DFLT_NO_FC_DELAY_MSB;
		bdp->config[19] &= ~CB_CFIG_FC_OPTS;
		bdp->config[19] |= CB_CFIG_TX_FC_DIS;
	}
	E100_CONFIG(bdp, 19);
	spin_unlock_bh(&(bdp->config_lock));

	return;
}

/**
 * e100_config_promisc - configure promiscuous mode
 * @bdp: atapter's private data struct
 * @enable: should we enable this option or not
 *
 * This routine will enable or disable promiscuous mode
 * in the adapter's config block.
 */
void
e100_config_promisc(struct e100_private *bdp, unsigned char enable)
{
	spin_lock_bh(&(bdp->config_lock));

	/* if in promiscuous mode, save bad frames */
	if (enable) {

		if (!(bdp->config[6] & CB_CFIG_SAVE_BAD_FRAMES)) {
			bdp->config[6] |= CB_CFIG_SAVE_BAD_FRAMES;
			E100_CONFIG(bdp, 6);
		}

		if (bdp->config[7] & (u8) BIT_0) {
			bdp->config[7] &= (u8) (~BIT_0);
			E100_CONFIG(bdp, 7);
		}

		if (!(bdp->config[15] & CB_CFIG_PROMISCUOUS)) {
			bdp->config[15] |= CB_CFIG_PROMISCUOUS;
			E100_CONFIG(bdp, 15);
		}

	} else {		/* not in promiscuous mode */

		if (bdp->config[6] & CB_CFIG_SAVE_BAD_FRAMES) {
			bdp->config[6] &= ~CB_CFIG_SAVE_BAD_FRAMES;
			E100_CONFIG(bdp, 6);
		}

		if (!(bdp->config[7] & (u8) BIT_0)) {
			bdp->config[7] |= (u8) (BIT_0);
			E100_CONFIG(bdp, 7);
		}

		if (bdp->config[15] & CB_CFIG_PROMISCUOUS) {
			bdp->config[15] &= ~CB_CFIG_PROMISCUOUS;
			E100_CONFIG(bdp, 15);
		}
	}

	spin_unlock_bh(&(bdp->config_lock));
}

/**
 * e100_config_mulcast_enbl - configure allmulti mode
 * @bdp: atapter's private data struct
 * @enable: should we enable this option or not
 *
 * This routine will enable or disable reception of all multicast packets
 * in the adapter's config block.
 */
void
e100_config_mulcast_enbl(struct e100_private *bdp, unsigned char enable)
{
	spin_lock_bh(&(bdp->config_lock));

	/* this flag is used to enable receiving all multicast packet */
	if (enable) {
		if (!(bdp->config[21] & CB_CFIG_MULTICAST_ALL)) {
			bdp->config[21] |= CB_CFIG_MULTICAST_ALL;
			E100_CONFIG(bdp, 21);
		}

	} else {
		if (bdp->config[21] & CB_CFIG_MULTICAST_ALL) {
			bdp->config[21] &= ~CB_CFIG_MULTICAST_ALL;
			E100_CONFIG(bdp, 21);
		}
	}

	spin_unlock_bh(&(bdp->config_lock));
}

/**
 * e100_config_ifs - configure the IFS parameter
 * @bdp: atapter's private data struct
 *
 * This routine will configure the adaptive IFS value
 * in the adapter's config block. IFS values are only
 * relevant in half duplex, so set to 0 in full duplex.
 */
void
e100_config_ifs(struct e100_private *bdp)
{
	u8 value = 0;

	spin_lock_bh(&(bdp->config_lock));

	/* IFS value is only needed to be specified at half-duplex mode */
	if (bdp->cur_dplx_mode == HALF_DUPLEX) {
		value = (u8) bdp->ifs_value;
	}

	if (bdp->config[2] != value) {
		bdp->config[2] = value;
		E100_CONFIG(bdp, 2);
	}

	spin_unlock_bh(&(bdp->config_lock));
}

/**
 * e100_config_force_dplx - configure the forced full duplex mode
 * @bdp: atapter's private data struct
 *
 * This routine will enable or disable force full duplex
 * in the adapter's config block. If the PHY is 503, and
 * the duplex is full, consider the adapter forced.
 */
void
e100_config_force_dplx(struct e100_private *bdp)
{
	spin_lock_bh(&(bdp->config_lock));

	/* We must force full duplex on if we are using PHY 0, and we are */
	/* supposed to run in FDX mode. We do this because the e100 has only */
	/* one FDX# input pin, and that pin will be connected to PHY 1. */
	/* Changed the 'if' condition below to fix performance problem * at 10
	 * full. The Phy was getting forced to full duplex while the MAC * was
	 * not, because the cur_dplx_mode was not being set to 2 by SetupPhy. *
	 * This is how the condition was, initially. * This has been changed so
	 * that the MAC gets forced to full duplex * simply if the user has
	 * forced full duplex. * * if (( bdp->phy_addr == 0 ) && (
	 * bdp->cur_dplx_mode == 2 )) */
	/* The rest of the fix is in the PhyDetect code. */
	if ((bdp->params.e100_speed_duplex == E100_SPEED_10_FULL) ||
	    (bdp->params.e100_speed_duplex == E100_SPEED_100_FULL) ||
	    ((bdp->phy_addr == 32) && (bdp->cur_dplx_mode == FULL_DUPLEX))) {
		if (!(bdp->config[19] & (u8) CB_CFIG_FORCE_FDX)) {
			bdp->config[19] |= (u8) CB_CFIG_FORCE_FDX;
			E100_CONFIG(bdp, 19);
		}

	} else {
		if (bdp->config[19] & (u8) CB_CFIG_FORCE_FDX) {
			bdp->config[19] &= (u8) (~CB_CFIG_FORCE_FDX);
			E100_CONFIG(bdp, 19);
		}
	}

	spin_unlock_bh(&(bdp->config_lock));
}

/**
 * e100_config_long_rx
 * @bdp: atapter's private data struct
 * @enable: should we enable this option or not
 *
 * This routine will enable or disable reception of larger packets.
 * This is needed by VLAN implementations.
 */
static void
e100_config_long_rx(struct e100_private *bdp, unsigned char enable)
{
	if (enable) {
		if (!(bdp->config[18] & CB_CFIG_LONG_RX_OK)) {
			bdp->config[18] |= CB_CFIG_LONG_RX_OK;
			E100_CONFIG(bdp, 18);
		}

	} else {
		if ((bdp->config[18] & CB_CFIG_LONG_RX_OK)) {
			bdp->config[18] &= ~CB_CFIG_LONG_RX_OK;
			E100_CONFIG(bdp, 18);
		}
	}
}

/**
 * e100_config_wol
 * @bdp: atapter's private data struct
 *
 * This sets configuration options for Wake On LAN functionality (WOL) in the
 * config record. WOL options are retrieved from wolinfo_wolopts in @bdp
 */
void
e100_config_wol(struct e100_private *bdp)
{
	spin_lock_bh(&(bdp->config_lock));

	if (bdp->wolopts & WAKE_PHY) {
		bdp->config[9] |= CB_LINK_STATUS_WOL;
		E100_CONFIG(bdp, 9);
	}

	if (!(bdp->wolopts & WAKE_MAGIC)) {
		bdp->config[19] |= CB_DISABLE_MAGPAK_WAKE;
		E100_CONFIG(bdp, 19);
	}

	spin_unlock_bh(&(bdp->config_lock));
}

/**
 * e100_config_loopback_mode
 * @bdp: atapter's private data struct
 * @mode: loopback mode(phy/mac/none)
 *
 */
unsigned char
e100_config_loopback_mode(struct e100_private *bdp, u8 mode)
{
	unsigned char bc_changed = false;
	u8 config_byte;

	spin_lock_bh(&(bdp->config_lock));

	switch (mode) {
	case NO_LOOPBACK:
		config_byte = CB_CFIG_LOOPBACK_NORMAL;
		break;
	case MAC_LOOPBACK:
		config_byte = CB_CFIG_LOOPBACK_INTERNAL;
		break;
	case PHY_LOOPBACK:
		config_byte = CB_CFIG_LOOPBACK_EXTERNAL;
		break;
	default:
		printk(KERN_NOTICE "e100: e100_config_loopback_mode: "
		       "Invalid argument 'mode': %d\n", mode);
		goto exit;
	}

	if ((bdp->config[10] & CB_CFIG_LOOPBACK_MODE) != config_byte) {

		bdp->config[10] &= (~CB_CFIG_LOOPBACK_MODE);
		bdp->config[10] |= config_byte;
		E100_CONFIG(bdp, 10);
		bc_changed = true;
	}

exit:
	spin_unlock_bh(&(bdp->config_lock));
	return bc_changed;
}
unsigned char
e100_config_tcb_ext_enable(struct e100_private *bdp, unsigned char enable)
{
        unsigned char bc_changed = false;
 
        spin_lock_bh(&(bdp->config_lock));
 
        if (enable) {
                if (bdp->config[6] & CB_CFIG_EXT_TCB_DIS) {
 
                        bdp->config[6] &= (~CB_CFIG_EXT_TCB_DIS);
                        E100_CONFIG(bdp, 6);
                        bc_changed = true;
                }
 
        } else {
                if (!(bdp->config[6] & CB_CFIG_EXT_TCB_DIS)) {
 
                        bdp->config[6] |= CB_CFIG_EXT_TCB_DIS;
                        E100_CONFIG(bdp, 6);
                        bc_changed = true;
                }
        }
        spin_unlock_bh(&(bdp->config_lock));
 
        return bc_changed;
}
unsigned char
e100_config_dynamic_tbd(struct e100_private *bdp, unsigned char enable)
{
        unsigned char bc_changed = false;
 
        spin_lock_bh(&(bdp->config_lock));
 
        if (enable) {
                if (!(bdp->config[7] & CB_CFIG_DYNTBD_EN)) {
 
                        bdp->config[7] |= CB_CFIG_DYNTBD_EN;
                        E100_CONFIG(bdp, 7);
                        bc_changed = true;
                }
 
        } else {
                if (bdp->config[7] & CB_CFIG_DYNTBD_EN) {
 
                        bdp->config[7] &= (~CB_CFIG_DYNTBD_EN);
                        E100_CONFIG(bdp, 7);
                        bc_changed = true;
                }
        }
        spin_unlock_bh(&(bdp->config_lock));
 
        return bc_changed;
}

