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

#include "e100.h"
#include "e100_config.h"

extern u16 e100_eeprom_read(struct e100_private *, u16);
extern int e100_wait_exec_cmplx(struct e100_private *, u32,u8);
extern void e100_phy_reset(struct e100_private *bdp);

static u8 e100_diag_selftest(struct net_device *);
static u8 e100_diag_eeprom(struct net_device *);
static u8 e100_diag_loopback(struct net_device *);

static u8 e100_diag_one_loopback (struct net_device *, u8);
static u8 e100_diag_rcv_loopback_pkt(struct e100_private *);
static void e100_diag_config_loopback(struct e100_private *, u8, u8, u8 *,u8 *);
static u8 e100_diag_loopback_alloc(struct e100_private *);
static void e100_diag_loopback_cu_ru_exec(struct e100_private *);
static u8 e100_diag_check_pkt(u8 *);
static void e100_diag_loopback_free(struct e100_private *);

#define LB_PACKET_SIZE 1500

/**
 * e100_run_diag - main test execution handler - checks mask of requests and calls the diag routines  
 * @dev: atapter's net device data struct
 * @test_info: array with test request mask also used to store test results
 *
 * RETURNS: updated flags field of struct ethtool_test
 */
u32
e100_run_diag(struct net_device *dev, u64 *test_info, u32 flags)
{
	struct e100_private* bdp = dev->priv;
	u8 test_result = true;

	e100_isolate_driver(bdp);

	if (flags & ETH_TEST_FL_OFFLINE) {
		u8 fail_mask;
		
		fail_mask = e100_diag_selftest(dev);
		if (fail_mask) {
			test_result = false;
			if (fail_mask & REGISTER_TEST_FAIL)
				test_info [E100_REG_TEST_FAIL] = true;
			if (fail_mask & ROM_TEST_FAIL)
				test_info [E100_ROM_TEST_FAIL] = true;
			if (fail_mask & SELF_TEST_FAIL)
				test_info [E100_MAC_TEST_FAIL] = true;
			if (fail_mask & TEST_TIMEOUT)
				test_info [E100_CHIP_TIMEOUT] = true;
		}

		fail_mask = e100_diag_loopback(dev);
		if (fail_mask) {
			test_result = false;
			if (fail_mask & PHY_LOOPBACK)
				test_info [E100_LPBK_PHY_FAIL] = true;
			if (fail_mask & MAC_LOOPBACK)
				test_info [E100_LPBK_MAC_FAIL] = true;
		}
	}

	if (!e100_diag_eeprom(dev)) {
		test_result = false;
		test_info [E100_EEPROM_TEST_FAIL] = true;
	}

	/* fully recover only if the device is open*/
	if (netif_running(dev))  {
		e100_deisolate_driver(bdp, true, false);
	} else {
    		e100_deisolate_driver(bdp, false, false);
	}
	/*Let card recover from the test*/
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ * 2);

	return flags | (test_result ? 0 : ETH_TEST_FL_FAILED);
}

/**
 * e100_diag_selftest - run hardware selftest 
 * @dev: atapter's net device data struct
 */
static u8
e100_diag_selftest(struct net_device *dev)
{
	struct e100_private *bdp = dev->priv;
	u32 st_timeout, st_result;
	u8 retval = 0;

	if (!e100_selftest(bdp, &st_timeout, &st_result)) {
		if (!st_timeout) {
			if (st_result & CB_SELFTEST_REGISTER_BIT)
				retval |= REGISTER_TEST_FAIL;
			if (st_result & CB_SELFTEST_DIAG_BIT)
				retval |= SELF_TEST_FAIL;
			if (st_result & CB_SELFTEST_ROM_BIT)
				retval |= ROM_TEST_FAIL;
		} else {
            		retval = TEST_TIMEOUT;
		}
	}

	e100_hw_reset_recover(bdp,PORT_SOFTWARE_RESET);

	return retval;
}

/**
 * e100_diag_eeprom - validate eeprom checksum correctness
 * @dev: atapter's net device data struct
 *
 */
static u8
e100_diag_eeprom (struct net_device *dev)
{
	struct e100_private *bdp = dev->priv;
	u16 i, eeprom_sum, eeprom_actual_csm;

	for (i = 0, eeprom_sum = 0; i < (bdp->eeprom_size - 1); i++) {
		eeprom_sum += e100_eeprom_read(bdp, i);
	}

	eeprom_actual_csm = e100_eeprom_read(bdp, bdp->eeprom_size - 1);

	if (eeprom_actual_csm == (u16)(EEPROM_SUM - eeprom_sum)) {
		return true;
	}

	return false;
}

/**
 * e100_diag_loopback - performs loopback test  
 * @dev: atapter's net device data struct
 */
static u8
e100_diag_loopback (struct net_device *dev)
{
	u8 rc = 0;

	if (!e100_diag_one_loopback(dev, PHY_LOOPBACK)) {
		rc |= PHY_LOOPBACK;
	}

	if (!e100_diag_one_loopback(dev, MAC_LOOPBACK)) {
		rc |= MAC_LOOPBACK;
	}

	return rc;
}

/**
 * e100_diag_loopback - performs loopback test  
 * @dev: atapter's net device data struct
 * @mode: lopback test type
 */
static u8
e100_diag_one_loopback (struct net_device *dev, u8 mode)
{
        struct e100_private *bdp = dev->priv;
        u8 res = false;
   	u8 saved_dynamic_tbd = false;
   	u8 saved_extended_tcb = false;

	if (!e100_diag_loopback_alloc(bdp))
		return false;

	/* change the config block to standard tcb and the correct loopback */
        e100_diag_config_loopback(bdp, true, mode,
				  &saved_extended_tcb, &saved_dynamic_tbd);

	e100_diag_loopback_cu_ru_exec(bdp);

        if (e100_diag_rcv_loopback_pkt(bdp)) {
		res = true;
	}

        e100_diag_loopback_free(bdp);

        /* change the config block to previous tcb mode and the no loopback */
        e100_diag_config_loopback(bdp, false, mode,
				  &saved_extended_tcb, &saved_dynamic_tbd);
	return res;
}

/**
 * e100_diag_config_loopback - setup/clear loopback before/after lpbk test
 * @bdp: atapter's private data struct
 * @set_loopback: true if the function is called to set lb
 * @loopback_mode: the loopback mode(MAC or PHY)
 * @tcb_extended: true if need to set extended tcb mode after clean loopback
 * @dynamic_tbd: true if needed to set dynamic tbd mode after clean loopback
 *
 */
void
e100_diag_config_loopback(struct e100_private* bdp,
			  u8 set_loopback,
			  u8 loopback_mode,
			  u8* tcb_extended,
			  u8* dynamic_tbd)
{
	/* if set_loopback == true - we want to clear tcb_extended/dynamic_tbd.
	 * the previous values are saved in the params tcb_extended/dynamic_tbd
	 * if set_loopback == false - we want to restore previous value.
	 */
	if (set_loopback || (*tcb_extended))
		  *tcb_extended = e100_config_tcb_ext_enable(bdp,*tcb_extended);

	if (set_loopback || (*dynamic_tbd))
		 *dynamic_tbd = e100_config_dynamic_tbd(bdp,*dynamic_tbd);

	if (set_loopback) {
		e100_config_loopback_mode(bdp,loopback_mode);
	} else {
		e100_config_loopback_mode(bdp,NO_LOOPBACK);
	}

	e100_config(bdp);

	if (loopback_mode == PHY_LOOPBACK) {
		unsigned long expires = jiffies + HZ * 5;

		if (set_loopback)
			e100_phy_reset(bdp);

		/* wait up to 5 secs for PHY loopback ON/OFF to take effect */
		while ((e100_get_link_state(bdp) != set_loopback) &&
		       time_before(jiffies, expires)) {
			yield();
		}
	} else { /* For MAC loopback wait 500 msec to take effect */
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 2);
	}
}
  
/**
 * e100_diag_loopback_alloc - alloc & initate tcb and rfd for the loopback
 * @bdp: atapter's private data struct
 *
 */
static u8
e100_diag_loopback_alloc(struct e100_private *bdp)
{
	dma_addr_t dma_handle;
	tcb_t *tcb;
	rfd_t *rfd;
	tbd_t *tbd;

	tcb = pci_alloc_consistent(bdp->pdev,
				   (sizeof (tcb_t) + sizeof (tbd_t) +
				    LB_PACKET_SIZE),
				   &dma_handle);
        if (tcb == NULL)
		return false;

	memset(tcb, 0x00, sizeof (tcb_t) + LB_PACKET_SIZE);
	tcb->tcb_phys = dma_handle;
	tcb->tcb_hdr.cb_status = 0;
	tcb->tcb_hdr.cb_cmd =
		cpu_to_le16(CB_EL_BIT | CB_TRANSMIT | CB_TX_SF_BIT);
	tcb->tcb_hdr.cb_lnk_ptr = cpu_to_le32(tcb->tcb_phys);
	tcb->tcb_tbd_ptr = cpu_to_le32(0xffffffff);
	tcb->tcb_cnt = 0;
	tcb->tcb_thrshld = bdp->tx_thld;
	tcb->tcb_tbd_num = 1;
	tcb->tcb_tbd_ptr = cpu_to_le32(tcb->tcb_phys + sizeof (tcb_t));
	tbd = (tbd_t *) ((u8 *) tcb + sizeof (tcb_t));
	tbd->tbd_buf_addr =
		cpu_to_le32(le32_to_cpu(tcb->tcb_tbd_ptr) + sizeof (tbd_t));
	tbd->tbd_buf_cnt = __constant_cpu_to_le16(1024);
	memset((void *) ((u8 *) tbd + sizeof (tbd_t)), 0xFF, 1024);
	memset((void *) ((u8 *) tbd + sizeof (tbd_t) + 512), 0xBA, 512);
	wmb();
	rfd = pci_alloc_consistent(bdp->pdev, sizeof (rfd_t), &dma_handle);

	if (rfd == NULL) {
		pci_free_consistent(bdp->pdev,
				    sizeof (tcb_t) + sizeof (tbd_t) +
				    LB_PACKET_SIZE, tcb, tcb->tcb_phys);
		return false;
	}

	memset(rfd, 0x00, sizeof (rfd_t));

	/* init all fields in rfd */
	rfd->rfd_header.cb_status = 0;
	rfd->rfd_header.cb_cmd = cpu_to_le16(RFD_EL_BIT);
	rfd->rfd_act_cnt = 0;
	rfd->rfd_sz = cpu_to_le16(ETH_FRAME_LEN + CHKSUM_SIZE);
	bdp->loopback.dma_handle = dma_handle;
	bdp->loopback.tcb = tcb;
	bdp->loopback.rfd = rfd;
	wmb();
	return true;
}

/**
 * e100_diag_loopback_cu_ru_exec - activates cu and ru to send & receive the pkt
 * @bdp: atapter's private data struct
 *
 */
static void
e100_diag_loopback_cu_ru_exec(struct e100_private *bdp)
{
	/*load CU & RU base */ 
	if (!e100_wait_exec_cmplx(bdp, 0, SCB_CUC_LOAD_BASE))
		printk("e100: SCB_CUC_LOAD_BASE failed\n");
	if(!e100_wait_exec_cmplx(bdp, 0, SCB_RUC_LOAD_BASE))
		printk("e100: SCB_RUC_LOAD_BASE failed!\n");
	if(!e100_wait_exec_cmplx(bdp, bdp->loopback.dma_handle, SCB_RUC_START))
		printk("e100: SCB_RUC_START failed!\n");

	bdp->next_cu_cmd = START_WAIT;
	e100_start_cu(bdp, bdp->loopback.tcb);
	bdp->last_tcb = NULL;
	rmb();
}
/**
 * e100_diag_check_pkt - checks if a given packet is a loopback packet
 * @bdp: atapter's private data struct
 *
 * Returns true if OK false otherwise.
 */
static u8
e100_diag_check_pkt(u8 *datap)
{
        if( (*datap)==0xFF) {
                if(*(datap + 600) == 0xBA) {
                        return true;
                }
        }
        return false;
}

/**
 * e100_diag_rcv_loopback_pkt - waits for receive and checks lpbk packet
 * @bdp: atapter's private data struct
 *
 * Returns true if OK false otherwise.
 */
static u8
e100_diag_rcv_loopback_pkt(struct e100_private* bdp) 
{    
	rfd_t *rfdp;
	u16 rfd_status;
	unsigned long expires = jiffies + HZ * 2;

        rfdp =bdp->loopback.rfd;

        rfd_status = le16_to_cpu(rfdp->rfd_header.cb_status);

        while (!(rfd_status & RFD_STATUS_COMPLETE)) { 
		if (time_before(jiffies, expires)) {
			yield();
			rmb();
			rfd_status = le16_to_cpu(rfdp->rfd_header.cb_status);
		} else {
			break;
		}
        }

        if (rfd_status & RFD_STATUS_COMPLETE)
                return e100_diag_check_pkt(((u8 *)rfdp+bdp->rfd_size));
	else
		return false;
}

/**
 * e100_diag_loopback_free - free data allocated for loopback pkt send/receive
 * @bdp: atapter's private data struct
 *
 */
static void
e100_diag_loopback_free (struct e100_private *bdp)
{
        pci_free_consistent(bdp->pdev,
			    sizeof(tcb_t) + sizeof(tbd_t) + LB_PACKET_SIZE,
			    bdp->loopback.tcb, bdp->loopback.tcb->tcb_phys);

        pci_free_consistent(bdp->pdev, sizeof(rfd_t), bdp->loopback.rfd,
			    bdp->loopback.dma_handle);
}

