/************************************************************************
 * s2io.c: A Linux PCI-X Ethernet driver for S2IO 10GbE Server NIC
 * Copyright(c) 2002-2005 S2IO Technologies

 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * Credits:
 * Jeff Garzik		: For pointing out the improper error condition 
 *			  check in the s2io_xmit routine and also some 
 * 			  issues in the Tx watch dog function. Also for
 *			  patiently answering all those innumerable 
 *			  questions regaring the 2.6 porting issues.
 * Stephen Hemminger	: Providing proper 2.6 porting mechanism for some
 *			  macros available only in 2.6 Kernel.
 * Francois Romieu	: For pointing out all code part that were 
 *			  deprecated and also styling related comments.
 * Grant Grundler	: For helping me get rid of some Architecture 
 *			  dependent code.
 * Christopher Hellwig	: Some more 2.6 specific issues in the driver.
 *			  	
 * The module loadable parameters that are supported by the driver and a brief
 * explaination of all the variables.
 * ring_num : This can be used to program the number of receive rings used 
 * in the driver.  					
 * frame_len: This is an array of size 8. Using this we can set the maximum 
 * size of the received frame that can be steered into the corrsponding 
 * receive ring.
 * ring_len: This defines the number of descriptors each ring can have. This 
 * is also an array of size 8.
 * fifo_num: This defines the number of Tx FIFOs thats used int the driver.
 * fifo_len: This too is an array of 8. Each element defines the number of 
 * Tx descriptors that can be associated with each corresponding FIFO.
 * latency_timer: This input is programmed into the Latency timer register
 * in PCI Configuration space.
 ************************************************************************/

#include<linux/config.h>
#include<linux/module.h>
#include<linux/types.h>
#include<linux/errno.h>
#include<linux/ioport.h>
#include<linux/pci.h>
#include<linux/kernel.h>
#include<linux/netdevice.h>
#include<linux/etherdevice.h>
#include<linux/skbuff.h>
#include<linux/init.h>
#include<linux/delay.h>
#include<linux/stddef.h>
#include<linux/ioctl.h>
#include<linux/timex.h>
#include<linux/sched.h>
#include<linux/ethtool.h>
#include<asm/system.h>
#include<asm/uaccess.h>
#include<linux/version.h>
#include<asm/io.h>
#include<linux/proc_fs.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include<linux/tqueue.h>
#else
#include<linux/workqueue.h>
#endif

/* Macros to ensure the code is backward compatible with 2.4.x kernels. */
#ifndef SET_NETDEV_DEV
#define SET_NETDEV_DEV(a, b)	do {} while(0)
#endif

#ifndef HAVE_FREE_NETDEV
#define free_netdev(x) kfree(x)
#endif

#ifndef IRQ_NONE
typedef void irqreturn_t;
#define	IRQ_NONE
#define	IRQ_HANDLED
#define	IRQ_RETVAL(x)
#endif

/* local include */
#include "s2io.h"
#include "s2io-regs.h"

/* VENDOR and DEVICE ID of XENA. */
#ifndef PCI_VENDOR_ID_S2IO
#define PCI_VENDOR_ID_S2IO      0x17D5
#define PCI_DEVICE_ID_S2IO_WIN  0x5731
#define PCI_DEVICE_ID_S2IO_UNI  0x5831
#endif

/* S2io Driver name & version. */
static char s2io_driver_name[] = "s2io";
static char s2io_driver_version[] = "Version 1.0";

/* Cards with following subsystem_id have a link state indication
 * problem, 600B, 600C, 600D, 640B, 640C and 640D.
 * macro below identifies these cards given the subsystem_id.
 */
#define CARDS_WITH_FAULTY_LINK_INDICATORS(subid) \
		(((subid >= 0x600B) && (subid <= 0x600D)) || \
		 ((subid >= 0x640B) && (subid <= 0x640D))) ? 1 : 0

#define LINK_IS_UP(val64) (!(val64 & (ADAPTER_STATUS_RMAC_REMOTE_FAULT | \
				      ADAPTER_STATUS_RMAC_LOCAL_FAULT)))
#define TASKLET_IN_USE test_and_set_bit(0, \
				(unsigned long *)(&sp->tasklet_status))
#define PANIC	1
#define LOW	2
static inline int rx_buffer_level(nic_t * sp, int rxb_size, int ring)
{
	int level = 0;
	if ((sp->pkt_cnt[ring] - rxb_size) > 16) {
		level = LOW;
		if (rxb_size < sp->pkt_cnt[ring] / 8)
			level = PANIC;
	}

	return level;
}

/* Ethtool related variables and Macros. */
#ifndef SET_ETHTOOL_OPS
static int s2io_ethtool(struct net_device *dev, struct ifreq *rq);
#endif
static char s2io_gstrings[][ETH_GSTRING_LEN] = {
	"Register test\t(offline)",
	"Eeprom test\t(offline)",
	"Link test\t(online)",
	"RLDRAM test\t(offline)",
	"BIST Test\t(offline)"
};

#ifdef ETHTOOL_GSTATS
static char ethtool_stats_keys[][ETH_GSTRING_LEN] = {
	{"tmac_frms"},
	{"tmac_data_octets"},
	{"tmac_drop_frms"},
	{"tmac_mcst_frms"},
	{"tmac_bcst_frms"},
	{"tmac_pause_ctrl_frms"},
	{"tmac_any_err_frms"},
	{"tmac_vld_ip_octets"},
	{"tmac_vld_ip"},
	{"tmac_drop_ip"},
	{"tmac_icmp"},
	{"tmac_rst_tcp"},
	{"tmac_tcp"},
	{"tmac_udp"},
	{"rmac_vld_frms"},
	{"rmac_data_octets"},
	{"rmac_fcs_err_frms"},
	{"rmac_drop_frms"},
	{"rmac_vld_mcst_frms"},
	{"rmac_vld_bcst_frms"},
	{"rmac_in_rng_len_err_frms"},
	{"rmac_long_frms"},
	{"rmac_pause_ctrl_frms"},
	{"rmac_discarded_frms"},
	{"rmac_usized_frms"},
	{"rmac_osized_frms"},
	{"rmac_frag_frms"},
	{"rmac_jabber_frms"},
	{"rmac_ip"},
	{"rmac_ip_octets"},
	{"rmac_hdr_err_ip"},
	{"rmac_drop_ip"},
	{"rmac_icmp"},
	{"rmac_tcp"},
	{"rmac_udp"},
	{"rmac_err_drp_udp"},
	{"rmac_pause_cnt"},
	{"rmac_accepted_ip"},
	{"rmac_err_tcp"},
};

#define S2IO_STAT_LEN sizeof(ethtool_stats_keys)/ ETH_GSTRING_LEN
#define S2IO_STAT_STRINGS_LEN S2IO_STAT_LEN * ETH_GSTRING_LEN
#endif

#define S2IO_TEST_LEN	sizeof(s2io_gstrings) / ETH_GSTRING_LEN
#define S2IO_STRINGS_LEN	S2IO_TEST_LEN * ETH_GSTRING_LEN


/* Constants to be programmed into the Xena's registers to configure
 * the XAUI.
 */

#define SWITCH_SIGN	0xA5A5A5A5A5A5A5A5ULL
#define	END_SIGN	0x0

static u64 default_mdio_cfg[] = {
	/* Reset PMA PLL */
	0xC001010000000000ULL, 0xC0010100000000E0ULL,
	0xC0010100008000E4ULL,
	/* Remove Reset from PMA PLL */
	0xC001010000000000ULL, 0xC0010100000000E0ULL,
	0xC0010100000000E4ULL,
	END_SIGN
};

static u64 default_dtx_cfg[] = {
	0x8000051500000000ULL, 0x80000515000000E0ULL,
	0x80000515D93500E4ULL, 0x8001051500000000ULL,
	0x80010515000000E0ULL, 0x80010515001E00E4ULL,
	0x8002051500000000ULL, 0x80020515000000E0ULL,
	0x80020515F21000E4ULL,
	/* Set PADLOOPBACKN */
	0x8002051500000000ULL, 0x80020515000000E0ULL,
	0x80020515B20000E4ULL, 0x8003051500000000ULL,
	0x80030515000000E0ULL, 0x80030515B20000E4ULL,
	0x8004051500000000ULL, 0x80040515000000E0ULL,
	0x80040515B20000E4ULL, 0x8005051500000000ULL,
	0x80050515000000E0ULL, 0x80050515B20000E4ULL,
	SWITCH_SIGN,
	/* Remove PADLOOPBACKN */
	0x8002051500000000ULL, 0x80020515000000E0ULL,
	0x80020515F20000E4ULL, 0x8003051500000000ULL,
	0x80030515000000E0ULL, 0x80030515F20000E4ULL,
	0x8004051500000000ULL, 0x80040515000000E0ULL,
	0x80040515F20000E4ULL, 0x8005051500000000ULL,
	0x80050515000000E0ULL, 0x80050515F20000E4ULL,
	END_SIGN
};

#if USE_OLD_PHY

static u64 oldphy_mdio_cfg[] = {
	0x0018040000000000ULL, 0x00180400000000E0ULL,
	0x00180400000000ECULL,
	SWITCH_SIGN,
	0x0018040000000000ULL, 0x00180400000000E0ULL,
	0x00180400000000ECULL,
	END_SIGN
};

static u64 oldphy_dtx_cfg[] = {
	0x8000051500000000ULL, 0x80000515000000E0ULL,
	0x80000515D93500E4ULL, 0x8001051500000000ULL,
	0x80010515000000E0ULL, 0x80010515001E00E4ULL,
	0x8002051500000000ULL, 0x80020515000000E0ULL,
	0x80020515F21000E4ULL, 0x8000051500000000ULL,
	0x80000515000000E0ULL, 0x80000515D93500ECULL,
	0x8001051500000000ULL, 0x80010515000000E0ULL,
	0x80010515000000ECULL, 0x8002051500000000ULL,
	0x80020515000000E0ULL, 0x80020515000000ECULL,
	SWITCH_SIGN,
	0x0000051500000000ULL, 0x00000515604000E0ULL,
	0x00000515604000E4ULL, 0x00000515204000E4ULL,
	0x00000515204000ECULL,
	END_SIGN
};

#endif

/* Constants for Fixing the MacAddress problem seen mostly on
 * Alpha machines.
 */
static u64 fix_mac[] = {
	0x0060000000000000ULL, 0x0060600000000000ULL,
	0x0040600000000000ULL, 0x0000600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0060600000000000ULL,
	0x0020600000000000ULL, 0x0000600000000000ULL,
	0x0040600000000000ULL, 0x0060600000000000ULL,
	END_SIGN
};

/* Module Loadable parameters. */
static u32 frame_len[MAX_RX_RINGS];
static u32 rx_prio;
static u32 tx_prio;

static unsigned int lso_enable = 1;
static unsigned int indicate_max_pkts = 0;
static unsigned int cksum_offload_enable = 1;
static unsigned int TxFifoNum = 1;
static unsigned int TxFIFOLen_0 = DEFAULT_FIFO_LEN;
static unsigned int TxFIFOLen_1 = 0;
static unsigned int TxFIFOLen_2 = 0;
static unsigned int TxFIFOLen_3 = 0;
static unsigned int TxFIFOLen_4 = 0;
static unsigned int TxFIFOLen_5 = 0;
static unsigned int TxFIFOLen_6 = 0;
static unsigned int TxFIFOLen_7 = 0;
static unsigned int MaxTxDs = MAX_SKB_FRAGS;
static unsigned int RxRingNum = 1;
static unsigned int RxRingSz_0 = SMALL_BLK_CNT;
static unsigned int RxRingSz_1 = 0;
static unsigned int RxRingSz_2 = 0;
static unsigned int RxRingSz_3 = 0;
static unsigned int RxRingSz_4 = 0;
static unsigned int RxRingSz_5 = 0;
static unsigned int RxRingSz_6 = 0;
static unsigned int RxRingSz_7 = 0;
static unsigned int Stats_refresh_time = 4;
static unsigned int rmac_pause_time = 65535;
static unsigned int mc_pause_threshold_q0q3 = 187;
static unsigned int mc_pause_threshold_q4q7 = 187;
static unsigned int shared_splits = 0;
#if defined(__ia64__)
static unsigned int max_splits_trans = XENA_THREE_SPLIT_TRANSACTION;
#else
static unsigned int max_splits_trans = XENA_TWO_SPLIT_TRANSACTION;
#endif
static unsigned int tmac_util_period = 5;
static unsigned int rmac_util_period = 5;
static unsigned int tx_timer_val = 0xFFF;
static unsigned int tx_utilz_periodic = 1;
static unsigned int rx_timer_val = 0xFFF;
static unsigned int rx_utilz_periodic = 1;
static unsigned int tx_urange_a = 0xA;
static unsigned int tx_ufc_a = 0x10;
static unsigned int tx_urange_b = 0x10;
static unsigned int tx_ufc_b = 0x20;
static unsigned int tx_urange_c = 0x30;
static unsigned int tx_ufc_c = 0x40;
static unsigned int tx_ufc_d = 0x80;
static unsigned int rx_urange_a = 0xA;
static unsigned int rx_ufc_a = 0x1;
static unsigned int rx_urange_b = 0x10;
static unsigned int rx_ufc_b = 0x2;
static unsigned int rx_urange_c = 0x30;
static unsigned int rx_ufc_c = 0x40;
static unsigned int rx_ufc_d = 0x80;
static u8 latency_timer = 0;

/* 
 * S2IO device table.
 * This table lists all the devices that this driver supports. 
 */
static struct pci_device_id s2io_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_S2IO, PCI_DEVICE_ID_S2IO_WIN,
	 PCI_ANY_ID, PCI_ANY_ID},
	{PCI_VENDOR_ID_S2IO, PCI_DEVICE_ID_S2IO_UNI,
	 PCI_ANY_ID, PCI_ANY_ID},
	{0,}
};

MODULE_DEVICE_TABLE(pci, s2io_tbl);

static struct pci_driver s2io_driver = {
      name:"S2IO",
      id_table:s2io_tbl,
      probe:s2io_init_nic,
      remove:__devexit_p(s2io_rem_nic),
};

/**
 * init_shared_mem - Allocation and Initialization of Memory
 * @nic: Device private variable.
 * Description: The function allocates the all memory areas shared 
 * between the NIC and the driver. This includes Tx descriptors, 
 * Rx descriptors and the statistics block.
 */

static int init_shared_mem(struct s2io_nic *nic)
{
	u32 size;
	void *tmp_v_addr, *tmp_v_addr_next;
	dma_addr_t tmp_p_addr, tmp_p_addr_next;
	RxD_block_t *pre_rxd_blk = NULL;
	int i, j, blk_cnt;
	struct net_device *dev = nic->dev;
	struct config_param *config;
#ifdef MAC
    mac_info_tx_t *mac_control_tx;
    mac_info_rx_t *mac_control_rx;
    mac_info_st_t *mac_control_st;
    mac_control_tx = &nic->mac_control_tx;
    mac_control_rx = &nic->mac_control_rx;
    mac_control_st = &nic->mac_control_st;
#else
	mac_info_t *mac_control;
	mac_control = &nic->mac_control;
#endif

	config = &nic->config;


	/* Allocation and initialization of TXDLs in FIOFs */
	size = 0;
	for (i = 0; i < config->TxFIFONum; i++) {
		size += config->TxCfg[i].FifoLen;
	}
	if (size > MAX_AVAILABLE_TXDS) {
		DBG_PRINT(ERR_DBG, "%s: Total number of Tx FIFOs ",
			  dev->name);
		DBG_PRINT(ERR_DBG, "exceeds the maximum value ");
		DBG_PRINT(ERR_DBG, "that can be used\n");
		return FAILURE;
	}
#ifdef TXDBD
    size = (size * (sizeof(TxD_t)) * config->MaxTxDs);
#ifdef MAC
    mac_control_tx->txd_fifo_mem_sz = size;
     /* Initialize the put and get information */
     for (i = 0; i < config->TxFIFONum; i++) {
              mac_control_tx->tx_curr_put_info[i].block_index = 0;
              mac_control_tx->tx_curr_put_info[i].offset = 0;
              mac_control_tx->tx_curr_put_info[i].fifo_len =
                   config->TxCfg[i].FifoLen ;

          mac_control_tx->tx_curr_get_info[i].block_index = 0;
          mac_control_tx->tx_curr_get_info[i].offset = 0;
          mac_control_tx->tx_curr_get_info[i].fifo_len =
              config->TxCfg[i].FifoLen ;
          blk_cnt = config->TxCfg[i].FifoLen * config->MaxTxDs/
                (mac_control_tx->max_txds_per_block);
              nic->tx_block_count[i] = blk_cnt;
	  nic->tx_blocks[i] = NULL;
          nic->tx_blocks[i]=(struct tx_block_info *)kmalloc
                (sizeof(struct tx_block_info ) * blk_cnt, GFP_KERNEL);
	  if(!nic->tx_blocks[i])
	  {
		for( j= 0; j< i ; ++j)
		{
			kfree(nic->tx_blocks[j]);
			nic->tx_blocks[j] = NULL;
		}
		return -ENOMEM;
	  }
          for (j = 0; j < blk_cnt; j++) {
              size = (mac_control_tx->max_txds_per_block ) * (sizeof(TxD_t));
              tmp_v_addr = pci_alloc_consistent(nic->pdev, size,
                                 &tmp_p_addr);
                  if (tmp_v_addr == NULL) {
                             return -ENOMEM;
                   }
                   memset(tmp_v_addr, 0, size);
                       nic->tx_blocks[i][j].block_virt_addr = tmp_v_addr;
                       nic->tx_blocks[i][j].block_dma_addr = tmp_p_addr;
              }
       }

#else
    mac_control->txd_fifo_mem_sz = size;
     /* Initialize the put and get information */
     for (i = 0; i < config->TxFIFONum; i++) {
              mac_control->tx_curr_put_info[i].block_index = 0;
              mac_control->tx_curr_put_info[i].offset = 0;
              mac_control->tx_curr_put_info[i].fifo_len =
                   config->TxCfg[i].FifoLen ;

          mac_control->tx_curr_get_info[i].block_index = 0;
          mac_control->tx_curr_get_info[i].offset = 0;
          mac_control->tx_curr_get_info[i].fifo_len =
              config->TxCfg[i].FifoLen ;
          blk_cnt = config->TxCfg[i].FifoLen * config->MaxTxDs/
                (mac_control->max_txds_per_block);
              nic->tx_block_count[i] = blk_cnt;
	  nic->tx_blocks[i]= NULL;
          nic->tx_blocks[i]=(struct tx_block_info *)kmalloc
                (sizeof(struct tx_block_info ) * blk_cnt, GFP_KERNEL);
	  if(!nic->tx_blocks[i])
	  {
		for( j= 0; j< i ; ++j)
		{
			kfree(nic->tx_blocks[j]);
			nic->tx_blocks[j] = NULL;
		}
		return -ENOMEM;
	  }
          for (j = 0; j < blk_cnt; j++) {
              size = (mac_control->max_txds_per_block ) * (sizeof(TxD_t));
              tmp_v_addr = pci_alloc_consistent(nic->pdev, size,
                                 &tmp_p_addr);
                  if (tmp_v_addr == NULL) {
                             return -ENOMEM;
                   }
                   memset(tmp_v_addr, 0, size);
                       nic->tx_blocks[i][j].block_virt_addr = tmp_v_addr;
                       nic->tx_blocks[i][j].block_dma_addr = tmp_p_addr;
              }
       }
#endif
#else
	size *= (sizeof(TxD_t) * config->MaxTxDs);
#ifdef MAC
    mac_control_tx->txd_list_mem = pci_alloc_consistent
        (nic->pdev, size, &mac_control_tx->txd_list_mem_phy);
    if (!mac_control_tx->txd_list_mem) {
        return -ENOMEM;
    }
    mac_control_tx->txd_list_mem_sz = size;

    tmp_v_addr = mac_control_tx->txd_list_mem;
    tmp_p_addr = mac_control_tx->txd_list_mem_phy;
    memset(tmp_v_addr, 0, size);

    DBG_PRINT(INIT_DBG, "%s:List Mem PHY: 0x%llx\n", dev->name,
          (unsigned long long) tmp_p_addr);

    for (i = 0; i < config->TxFIFONum; i++) {
        mac_control_tx->txdl_start_phy[i] = tmp_p_addr;
        mac_control_tx->txdl_start[i] = (TxD_t *) tmp_v_addr;
        mac_control_tx->tx_curr_put_info[i].offset = 0;
        mac_control_tx->tx_curr_put_info[i].fifo_len =
            config->TxCfg[i].FifoLen - 1;
        mac_control_tx->tx_curr_get_info[i].offset = 0;
        mac_control_tx->tx_curr_get_info[i].fifo_len =
            config->TxCfg[i].FifoLen - 1;

        tmp_p_addr +=
            (config->TxCfg[i].FifoLen * (sizeof(TxD_t)) *
             config->MaxTxDs);
        tmp_v_addr +=
            (config->TxCfg[i].FifoLen * (sizeof(TxD_t)) *
             config->MaxTxDs);
    }

#else
	mac_control->txd_list_mem = pci_alloc_consistent
	    (nic->pdev, size, &mac_control->txd_list_mem_phy);
	if (!mac_control->txd_list_mem) {
		return -ENOMEM;
	}
	mac_control->txd_list_mem_sz = size;

	tmp_v_addr = mac_control->txd_list_mem;
	tmp_p_addr = mac_control->txd_list_mem_phy;
	memset(tmp_v_addr, 0, size);

	DBG_PRINT(INIT_DBG, "%s:List Mem PHY: 0x%llx\n", dev->name,
		  (unsigned long long) tmp_p_addr);

	for (i = 0; i < config->TxFIFONum; i++) {
		mac_control->txdl_start_phy[i] = tmp_p_addr;
		mac_control->txdl_start[i] = (TxD_t *) tmp_v_addr;
		mac_control->tx_curr_put_info[i].offset = 0;
		mac_control->tx_curr_put_info[i].fifo_len =
		    config->TxCfg[i].FifoLen - 1;
		mac_control->tx_curr_get_info[i].offset = 0;
		mac_control->tx_curr_get_info[i].fifo_len =
		    config->TxCfg[i].FifoLen - 1;

		tmp_p_addr +=
		    (config->TxCfg[i].FifoLen * (sizeof(TxD_t)) *
		     config->MaxTxDs);
		tmp_v_addr +=
		    (config->TxCfg[i].FifoLen * (sizeof(TxD_t)) *
		     config->MaxTxDs);
	}
#endif
#endif

	/* Allocation and initialization of RXDs in Rings */
	size = 0;
	for (i = 0; i < config->RxRingNum; i++) {
		if (config->RxCfg[i].NumRxd % (MAX_RXDS_PER_BLOCK + 1)) {
			DBG_PRINT(ERR_DBG, "%s: RxD count of ", dev->name);
			DBG_PRINT(ERR_DBG, "Ring%d is not a multiple of ",
				  i);
			DBG_PRINT(ERR_DBG, "RxDs per Block");
			return FAILURE;
		}
		size += config->RxCfg[i].NumRxd;
		nic->block_count[i] =
		    config->RxCfg[i].NumRxd / (MAX_RXDS_PER_BLOCK + 1);
		nic->pkt_cnt[i] =
		    config->RxCfg[i].NumRxd - nic->block_count[i];
	}
	size = (size * (sizeof(RxD_t)));
#ifdef MAC
    mac_control_rx->rxd_ring_mem_sz = size;

    for (i = 0; i < config->RxRingNum; i++) {
        mac_control_rx->rx_curr_get_info[i].block_index = 0;
        mac_control_rx->rx_curr_get_info[i].offset = 0;
        mac_control_rx->rx_curr_get_info[i].ring_len =
            config->RxCfg[i].NumRxd - 1;
        mac_control_rx->rx_curr_put_info[i].block_index = 0;
        mac_control_rx->rx_curr_put_info[i].offset = 0;
        mac_control_rx->rx_curr_put_info[i].ring_len =
            config->RxCfg[i].NumRxd - 1;
#else
	mac_control->rxd_ring_mem_sz = size;

	for (i = 0; i < config->RxRingNum; i++) {
		mac_control->rx_curr_get_info[i].block_index = 0;
		mac_control->rx_curr_get_info[i].offset = 0;
		mac_control->rx_curr_get_info[i].ring_len =
		    config->RxCfg[i].NumRxd - 1;
		mac_control->rx_curr_put_info[i].block_index = 0;
		mac_control->rx_curr_put_info[i].offset = 0;
		mac_control->rx_curr_put_info[i].ring_len =
		    config->RxCfg[i].NumRxd - 1;
#endif
		blk_cnt =
		    config->RxCfg[i].NumRxd / (MAX_RXDS_PER_BLOCK + 1);
		/*  Allocating all the Rx blocks */
		for (j = 0; j < blk_cnt; j++) {
#ifndef CONFIG_2BUFF_MODE
			size = (MAX_RXDS_PER_BLOCK + 1) * (sizeof(RxD_t));
#else
			size = SIZE_OF_BLOCK;
#endif
			tmp_v_addr = pci_alloc_consistent(nic->pdev, size,
							  &tmp_p_addr);
			if (tmp_v_addr == NULL) {
				/* In case of failure, free_shared_mem() 
				 * is called, which should free any 
				 * memory that was alloced till the 
				 * failure happened.
				 */
				nic->rx_blocks[i][j].block_virt_addr =
				    tmp_v_addr;
				return -ENOMEM;
			}
			memset(tmp_v_addr, 0, size);
			nic->rx_blocks[i][j].block_virt_addr = tmp_v_addr;
			nic->rx_blocks[i][j].block_dma_addr = tmp_p_addr;
		}
		/* Interlinking all Rx Blocks */
		for (j = 0; j < blk_cnt; j++) {
			tmp_v_addr = nic->rx_blocks[i][j].block_virt_addr;
			tmp_v_addr_next =
			    nic->rx_blocks[i][(j + 1) %
					      blk_cnt].block_virt_addr;
			tmp_p_addr = nic->rx_blocks[i][j].block_dma_addr;
			tmp_p_addr_next =
			    nic->rx_blocks[i][(j + 1) %
					      blk_cnt].block_dma_addr;

			pre_rxd_blk = (RxD_block_t *) tmp_v_addr;
			pre_rxd_blk->reserved_1 = END_OF_BLOCK;	/* last RxD 
								 * marker.
								 */
#ifndef	CONFIG_2BUFF_MODE
			pre_rxd_blk->reserved_2_pNext_RxD_block =
			    (unsigned long) tmp_v_addr_next;
#endif
			pre_rxd_blk->pNext_RxD_Blk_physical =
			    (u64) tmp_p_addr_next;
		}
	}

	/* Allocation and initialization of Statistics block */
	size = sizeof(StatInfo_t);
#ifdef MAC
    mac_control_st->stats_mem = pci_alloc_consistent
        (nic->pdev, size, &mac_control_st->stats_mem_phy);

    if (!mac_control_st->stats_mem) {
        /* In case of failure, free_shared_mem() is called, which 
         * should free any memory that was alloced till the 
         * failure happened.
         */
        return -ENOMEM;
    }
    mac_control_st->stats_mem_sz = size;

    tmp_v_addr = mac_control_st->stats_mem;
    mac_control_st->StatsInfo = (StatInfo_t *) tmp_v_addr;
#else

	mac_control->stats_mem = pci_alloc_consistent
	    (nic->pdev, size, &mac_control->stats_mem_phy);

	if (!mac_control->stats_mem) {
		/* In case of failure, free_shared_mem() is called, which 
		 * should free any memory that was alloced till the 
		 * failure happened.
		 */
		return -ENOMEM;
	}
	mac_control->stats_mem_sz = size;

	tmp_v_addr = mac_control->stats_mem;
	mac_control->StatsInfo = (StatInfo_t *) tmp_v_addr;
#endif
	memset(tmp_v_addr, 0, size);
#ifdef SNMP_SUPPORT
#ifdef TXDBD
#ifdef MAC
        nic->nMemorySize = mac_control_tx->txd_fifo_mem_sz +
               mac_control_st->stats_mem_sz + mac_control_rx->rxd_ring_mem_sz;
#else
        nic->nMemorySize = mac_control->txd_fifo_mem_sz +
                 mac_control->stats_mem_sz + mac_control->rxd_ring_mem_sz;
#endif
#else
#ifdef MAC
        nic->nMemorySize = mac_control_tx->txd_list_mem_sz +
                 mac_control_st->stats_mem_sz + mac_control_rx->rxd_ring_mem_sz;
#else
        nic->nMemorySize = mac_control->txd_list_mem_sz +
                 mac_control->stats_mem_sz + mac_control->rxd_ring_mem_sz;
#endif
#endif
#endif

	DBG_PRINT(INIT_DBG, "%s:Ring Mem PHY: 0x%llx\n", dev->name,
		  (unsigned long long) tmp_p_addr);

	return SUCCESS;
}

/**  
 * free_shared_mem - Free the allocated Memory 
 * @nic:  Device private variable.
 * Description: This function is to free all memory locations allocated by
 * the init_shared_mem() function and return it to the kernel.
 */

static void free_shared_mem(struct s2io_nic *nic)
{
	int i, j, blk_cnt, size;
	void *tmp_v_addr;
	dma_addr_t tmp_p_addr;
	struct config_param *config;
#ifdef MAC
    mac_info_tx_t *mac_control_tx;
    mac_info_st_t *mac_control_st;
#else
    mac_info_t *mac_control;
#endif

	if (!nic)
		return;

	config = &nic->config;
#ifdef MAC
    mac_control_tx = &nic->mac_control_tx;
    mac_control_st = &nic->mac_control_st;
#else
	mac_control = &nic->mac_control;
#endif
#ifdef TXDBD
#ifdef MAC
    size = (mac_control_tx->max_txds_per_block) * (sizeof(TxD_t));
    for (i = 0; i < config->TxFIFONum; i++) {
          blk_cnt = nic->tx_block_count[i];
          for (j = 0; j < blk_cnt; j++) {
              tmp_v_addr = nic->tx_blocks[i][j].block_virt_addr;
              tmp_p_addr = nic->tx_blocks[i][j].block_dma_addr;
              if (tmp_v_addr == NULL)
                      break;
              pci_free_consistent(nic->pdev, size,
                   tmp_v_addr, tmp_p_addr);
          }
          kfree(nic->tx_blocks[i]);
     }
#else
    size = (mac_control->max_txds_per_block) * (sizeof(TxD_t));
    for (i = 0; i < config->TxFIFONum; i++) {
          blk_cnt = nic->tx_block_count[i];
          for (j = 0; j < blk_cnt; j++) {
              tmp_v_addr = nic->tx_blocks[i][j].block_virt_addr;
              tmp_p_addr = nic->tx_blocks[i][j].block_dma_addr;
              if (tmp_v_addr == NULL)
                      break;
              pci_free_consistent(nic->pdev, size,
                   tmp_v_addr, tmp_p_addr);
          }
          kfree(nic->tx_blocks[i]);
     }
#endif
#else
#ifdef MAC
    if (mac_control_tx->txd_list_mem) {
        pci_free_consistent(nic->pdev,
                    mac_control_tx->txd_list_mem_sz,
                    mac_control_tx->txd_list_mem,
                    mac_control_tx->txd_list_mem_phy);
    }

#else

	if (mac_control->txd_list_mem) {
		pci_free_consistent(nic->pdev,
				    mac_control->txd_list_mem_sz,
				    mac_control->txd_list_mem,
				    mac_control->txd_list_mem_phy);
	}
#endif
#endif

#ifndef CONFIG_2BUFF_MODE
	size = (MAX_RXDS_PER_BLOCK + 1) * (sizeof(RxD_t));
#else
	size = SIZE_OF_BLOCK;
#endif
	for (i = 0; i < config->RxRingNum; i++) {
		blk_cnt = nic->block_count[i];
		for (j = 0; j < blk_cnt; j++) {
			tmp_v_addr = nic->rx_blocks[i][j].block_virt_addr;
			tmp_p_addr = nic->rx_blocks[i][j].block_dma_addr;
			if (tmp_v_addr == NULL)
				break;
			pci_free_consistent(nic->pdev, size,
					    tmp_v_addr, tmp_p_addr);
		}
	}
#ifdef MAC
    if (mac_control_st->stats_mem) {
        pci_free_consistent(nic->pdev,
                    mac_control_st->stats_mem_sz,
                    mac_control_st->stats_mem,
                    mac_control_st->stats_mem_phy);
    }

#else
	if (mac_control->stats_mem) {
		pci_free_consistent(nic->pdev,
				    mac_control->stats_mem_sz,
				    mac_control->stats_mem,
				    mac_control->stats_mem_phy);
	}
#endif
}

/**  
 *  init_nic - Initialization of hardware 
 *  @nic: device peivate variable
 *  Description: The function sequentially configures every block 
 *  of the H/W from their reset values. 
 *  Returns:  SUCCESS on success and 
 *  '-1' on failure (endian settings incorrect).
 */

static int init_nic(struct s2io_nic *nic)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	struct net_device *dev = nic->dev;
	register u64 val64 = 0;
	void *add;
	u32 time;
	int i, j;
#ifdef MAC
    mac_info_rx_t *mac_control_rx;
    mac_info_st_t *mac_control_st;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
	int mdio_cnt = 0, dtx_cnt = 0;
	unsigned long long print_var, mem_share;

	config = &nic->config;
#ifdef MAC
    mac_control_rx = &nic->mac_control_rx;
    mac_control_st = &nic->mac_control_st;
#else
	mac_control = &nic->mac_control;
#endif
	/*  Set proper endian settings and verify the same by 
	 *  reading the PIF Feed-back register.
	 */
#ifdef  __BIG_ENDIAN
	/* The device by default set to a big endian format, so 
	 * a big endian driver need not set anything.
	 */
	writeq(0xffffffffffffffffULL, &bar0->swapper_ctrl);
	val64 = (SWAPPER_CTRL_PIF_R_FE |
		 SWAPPER_CTRL_PIF_R_SE |
		 SWAPPER_CTRL_PIF_W_FE |
		 SWAPPER_CTRL_PIF_W_SE |
		 SWAPPER_CTRL_TXP_FE |
		 SWAPPER_CTRL_TXP_SE |
		 SWAPPER_CTRL_TXD_R_FE |
		 SWAPPER_CTRL_TXD_W_FE |
		 SWAPPER_CTRL_TXF_R_FE |
		 SWAPPER_CTRL_RXD_R_FE |
		 SWAPPER_CTRL_RXD_W_FE |
		 SWAPPER_CTRL_RXF_W_FE |
		 SWAPPER_CTRL_XMSI_FE |
		 SWAPPER_CTRL_XMSI_SE |
		 SWAPPER_CTRL_STATS_FE | SWAPPER_CTRL_STATS_SE);
	writeq(val64, &bar0->swapper_ctrl);
#else
	/* Initially we enable all bits to make it accessible by 
	 * the driver, then we selectively enable only those bits 
	 * that we want to set.
	 */
	writeq(0xffffffffffffffffULL, &bar0->swapper_ctrl);
	val64 = (SWAPPER_CTRL_PIF_R_FE |
		 SWAPPER_CTRL_PIF_R_SE |
		 SWAPPER_CTRL_PIF_W_FE |
		 SWAPPER_CTRL_PIF_W_SE |
		 SWAPPER_CTRL_TXP_FE |
		 SWAPPER_CTRL_TXP_SE |
		 SWAPPER_CTRL_TXD_R_FE |
		 SWAPPER_CTRL_TXD_R_SE |
		 SWAPPER_CTRL_TXD_W_FE |
		 SWAPPER_CTRL_TXD_W_SE |
		 SWAPPER_CTRL_TXF_R_FE |
		 SWAPPER_CTRL_RXD_R_FE |
		 SWAPPER_CTRL_RXD_R_SE |
		 SWAPPER_CTRL_RXD_W_FE |
		 SWAPPER_CTRL_RXD_W_SE |
		 SWAPPER_CTRL_RXF_W_FE |
		 SWAPPER_CTRL_XMSI_FE |
		 SWAPPER_CTRL_XMSI_SE |
		 SWAPPER_CTRL_STATS_FE | SWAPPER_CTRL_STATS_SE);
	writeq(val64, &bar0->swapper_ctrl);
#endif

	/* Verifying if endian settings are accurate by reading 
	 * a feedback register.
	 */
	val64 = readq(&bar0->pif_rd_swapper_fb);
	if (val64 != 0x0123456789ABCDEFULL) {
		/* Endian settings are incorrect, calls for another dekko. */
		print_var = (unsigned long long) val64;
		DBG_PRINT(INIT_DBG, "%s: Endian settings are wrong",
			  dev->name);
		DBG_PRINT(ERR_DBG, ", feedback read %llx\n", print_var);

		return FAILURE;
	}

	/* Remove XGXS from reset state */
	val64 = 0;
	writeq(val64, &bar0->sw_reset);
	val64 = readq(&bar0->sw_reset);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 2);

	/*  Enable Receiving broadcasts */
	val64 = readq(&bar0->mac_cfg);
	val64 |= MAC_RMAC_BCAST_ENABLE;
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writeq(val64, &bar0->mac_cfg);

	/* Read registers in all blocks */
	val64 = readq(&bar0->mac_int_mask);
	val64 = readq(&bar0->mc_int_mask);
	val64 = readq(&bar0->xgxs_int_mask);

	/*  Set MTU */
	val64 = dev->mtu;
	writeq(vBIT(val64, 2, 14), &bar0->rmac_max_pyld_len);

	/* Configuring the XAUI Interface of Xena. 
	 *****************************************
	 * To Configure the Xena's XAUI, one has to write a series 
	 * of 64 bit values into two registers in a particular 
	 * sequence. Hence a macro 'SWITCH_SIGN' has been defined 
	 * which will be defined in the array of configuration values 
	 * (default_dtx_cfg & default_mdio_cfg) at appropriate places 
	 * to switch writing from one regsiter to another. We continue 
	 * writing these values until we encounter the 'END_SIGN' macro.
	 * For example, After making a series of 21 writes into 
	 * dtx_control register the 'SWITCH_SIGN' appears and hence we 
	 * start writing into mdio_control until we encounter END_SIGN.
	 */
	while (1) {
	      dtx_cfg:
		while (default_dtx_cfg[dtx_cnt] != END_SIGN) {
			if (default_dtx_cfg[dtx_cnt] == SWITCH_SIGN) {
				dtx_cnt++;
				goto mdio_cfg;
			}
			writeq(default_dtx_cfg[dtx_cnt],
			       &bar0->dtx_control);
			val64 = readq(&bar0->dtx_control);
			dtx_cnt++;
		}
	      mdio_cfg:
		while (default_mdio_cfg[mdio_cnt] != END_SIGN) {
			if (default_mdio_cfg[mdio_cnt] == SWITCH_SIGN) {
				mdio_cnt++;
				goto dtx_cfg;
			}
			writeq(default_mdio_cfg[mdio_cnt],
			       &bar0->mdio_control);
			val64 = readq(&bar0->mdio_control);
			mdio_cnt++;
		}
		if ((default_dtx_cfg[dtx_cnt] == END_SIGN) &&
		    (default_mdio_cfg[mdio_cnt] == END_SIGN)) {
			break;
		} else {
			goto dtx_cfg;
		}
	}

	/*  Tx DMA Initialization */
	val64 = 0;
	writeq(val64, &bar0->tx_fifo_partition_0);
	writeq(val64, &bar0->tx_fifo_partition_1);
	writeq(val64, &bar0->tx_fifo_partition_2);
	writeq(val64, &bar0->tx_fifo_partition_3);


	for (i = 0, j = 0; i < config->TxFIFONum; i++) {
		val64 |=
		    vBIT(config->TxCfg[i].FifoLen - 1, ((i * 32) + 19),
			 13) | vBIT(config->TxCfg[i].FifoPriority,
				    ((i * 32) + 5), 3);

		if (i == (config->TxFIFONum - 1)) {
			if (i % 2 == 0)
				i++;
		}

		switch (i) {
		case 1:
			writeq(val64, &bar0->tx_fifo_partition_0);
			val64 = 0;
			break;
		case 3:
			writeq(val64, &bar0->tx_fifo_partition_1);
			val64 = 0;
			break;
		case 5:
			writeq(val64, &bar0->tx_fifo_partition_2);
			val64 = 0;
			break;
		case 7:
			writeq(val64, &bar0->tx_fifo_partition_3);
			break;
		}
	}

	/* Enable Tx FIFO partition 0. */
	val64 = readq(&bar0->tx_fifo_partition_0);
	val64 |= BIT(0);	/* To enable the FIFO partition. */
	writeq(val64, &bar0->tx_fifo_partition_0);

	val64 = readq(&bar0->tx_fifo_partition_0);
	DBG_PRINT(INIT_DBG, "Fifo partition at: 0x%p is: 0x%llx\n",
		  &bar0->tx_fifo_partition_0, (unsigned long long) val64);

	/* 
	 * Initialization of Tx_PA_CONFIG register to ignore packet 
	 * integrity checking.
	 */
	val64 = readq(&bar0->tx_pa_cfg);
	val64 |= TX_PA_CFG_IGNORE_FRM_ERR | TX_PA_CFG_IGNORE_SNAP_OUI |
	    TX_PA_CFG_IGNORE_LLC_CTRL | TX_PA_CFG_IGNORE_L2_ERR;
	writeq(val64, &bar0->tx_pa_cfg);

	/* Rx DMA intialization. */
	val64 = 0;
	for (i = 0; i < config->RxRingNum; i++) {
		val64 |=
		    vBIT(config->RxCfg[i].RingPriority, (5 + (i * 8)), 3);
	}
	writeq(val64, &bar0->rx_queue_priority);

	/* Allocating equal share of memory to all the configured 
	 * Rings.
	 */
	val64 = 0;
	for (i = 0; i < config->RxRingNum; i++) {
		switch (i) {
		case 0:
			mem_share = (64 / config->RxRingNum +
				     64 % config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q0_SZ(mem_share);
			continue;
		case 1:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q1_SZ(mem_share);
			continue;
		case 2:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q2_SZ(mem_share);
			continue;
		case 3:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q3_SZ(mem_share);
			continue;
		case 4:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q4_SZ(mem_share);
			continue;
		case 5:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q5_SZ(mem_share);
			continue;
		case 6:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q6_SZ(mem_share);
			continue;
		case 7:
			mem_share = (64 / config->RxRingNum);
			val64 |= RX_QUEUE_CFG_Q7_SZ(mem_share);
			continue;
		}
	}
	writeq(val64, &bar0->rx_queue_cfg);

	/* Initializing the Tx round robin registers to 0.
	 * Filling Tx and Rx round robin registers as per the 
	 * number of FIFOs and Rings is still TODO.
	 */
	writeq(0, &bar0->tx_w_round_robin_0);
	writeq(0, &bar0->tx_w_round_robin_1);
	writeq(0, &bar0->tx_w_round_robin_2);
	writeq(0, &bar0->tx_w_round_robin_3);
	writeq(0, &bar0->tx_w_round_robin_4);

	/* Disable Rx steering. Hard coding all packets be steered to
	 * Queue 0 for now. 
	 * TODO*/
	if (rx_prio) {
		u64 def = 0x8000000000000000ULL, tmp;
		for (i = 0; i < MAX_RX_RINGS; i++) {
			tmp = (u64) (def >> (i % config->RxRingNum));
			val64 |= (u64) (tmp >> (i * 8));
		}
		writeq(val64, &bar0->rts_qos_steering);
	} else {
		val64 = 0x8080808080808080ULL;
		writeq(val64, &bar0->rts_qos_steering);
	}

	/* UDP Fix */
	val64 = 0;
	for (i = 1; i < 8; i++)
		writeq(val64, &bar0->rts_frm_len_n[i]);

	/* Set rts_frm_len register for fifo 0 */
	writeq(MAC_RTS_FRM_LEN_SET(dev->mtu + 22),
	       &bar0->rts_frm_len_n[0]);

	/* Enable statistics */
#ifdef MAC
    writeq(mac_control_st->stats_mem_phy, &bar0->stat_addr);
#else
	writeq(mac_control->stats_mem_phy, &bar0->stat_addr);
#endif
	val64 = SET_UPDT_PERIOD(Stats_refresh_time) |
	    STAT_CFG_STAT_RO | STAT_CFG_STAT_EN;
	writeq(val64, &bar0->stat_cfg);

	/* Initializing the sampling rate for the device to calculate the
	 * bandwidth utilization.
	 */
	val64 = MAC_TX_LINK_UTIL_VAL(tmac_util_period) |
	    MAC_RX_LINK_UTIL_VAL(rmac_util_period);
	writeq(val64, &bar0->mac_link_util);


	/* Initializing the Transmit and Receive Traffic Interrupt 
	 * Scheme.
	 */
	/* TTI Initialization */
	val64 = TTI_DATA1_MEM_TX_TIMER_VAL(tx_timer_val) |
	    TTI_DATA1_MEM_TX_URNG_A(tx_urange_a) |
	    TTI_DATA1_MEM_TX_URNG_B(tx_urange_b) |
	    TTI_DATA1_MEM_TX_URNG_C(tx_urange_c);
	if (tx_utilz_periodic)
		val64 |= TTI_DATA1_MEM_TX_TIMER_AC_EN;
	writeq(val64, &bar0->tti_data1_mem);

	val64 = TTI_DATA2_MEM_TX_UFC_A(tx_ufc_a) |
	    TTI_DATA2_MEM_TX_UFC_B(tx_ufc_b) |
	    TTI_DATA2_MEM_TX_UFC_C(tx_ufc_c) |
	    TTI_DATA2_MEM_TX_UFC_D(tx_ufc_d);
	writeq(val64, &bar0->tti_data2_mem);

	val64 = TTI_CMD_MEM_WE | TTI_CMD_MEM_STROBE_NEW_CMD;
	writeq(val64, &bar0->tti_command_mem);

	/* Once the operation completes, the Strobe bit of the command
	 * register will be reset. We poll for this particular condition
	 * We wait for a maximum of 500ms for the operation to complete,
	 * if it's not complete by then we return error.
	 */
	time = 0;
	while (TRUE) {
		val64 = readq(&bar0->tti_command_mem);
		if (!(val64 & TTI_CMD_MEM_STROBE_NEW_CMD)) {
			break;
		}
		if (time > 10) {
			DBG_PRINT(ERR_DBG, "%s: TTI init Failed\n",
				  dev->name);
			return -1;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
		time++;
	}

	/* RTI Initialization */
	val64 = RTI_DATA1_MEM_RX_TIMER_VAL(rx_timer_val) |
	    RTI_DATA1_MEM_RX_URNG_A(rx_urange_a) |
	    RTI_DATA1_MEM_RX_URNG_B(rx_urange_b) |
	    RTI_DATA1_MEM_RX_URNG_C(rx_urange_c);
	if (rx_utilz_periodic)
		val64 |= RTI_DATA1_MEM_RX_TIMER_AC_EN;

	writeq(val64, &bar0->rti_data1_mem);

	val64 = RTI_DATA2_MEM_RX_UFC_A(rx_ufc_a) |
	    RTI_DATA2_MEM_RX_UFC_B(rx_ufc_b) |
	    RTI_DATA2_MEM_RX_UFC_C(rx_ufc_c) |
	    RTI_DATA2_MEM_RX_UFC_D(rx_ufc_d);
	writeq(val64, &bar0->rti_data2_mem);

	val64 = RTI_CMD_MEM_WE | RTI_CMD_MEM_STROBE_NEW_CMD;
	writeq(val64, &bar0->rti_command_mem);

	/* Once the operation completes, the Strobe bit of the command
	 * register will be reset. We poll for this particular condition
	 * We wait for a maximum of 500ms for the operation to complete,
	 * if it's not complete by then we return error.
	 */
	time = 0;
	while (TRUE) {
		val64 = readq(&bar0->rti_command_mem);
		if (!(val64 & TTI_CMD_MEM_STROBE_NEW_CMD)) {
			break;
		}
		if (time > 10) {
			DBG_PRINT(ERR_DBG, "%s: RTI init Failed\n",
				  dev->name);
			return -1;
		}
		time++;
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
	}

	/* Initializing proper values as Pause threshold into all 
	 * the 8 Queues on Rx side.
	 */
	writeq(0xffbbffbbffbbffbbULL, &bar0->mc_pause_thresh_q0q3);
	writeq(0xffbbffbbffbbffbbULL, &bar0->mc_pause_thresh_q4q7);

	/* Disable RMAC PAD STRIPPING */
	add = (void *) &bar0->mac_cfg;
	val64 = readq(&bar0->mac_cfg);
	val64 &= ~(MAC_CFG_RMAC_STRIP_PAD);
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32) (val64), add);
	writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
	writel((u32) (val64 >> 32), (add + 4));
	val64 = readq(&bar0->mac_cfg);

	/*Set the time value  to be inserted in the pause frame generated by xena */
	val64 = readq(&bar0->rmac_pause_cfg);
	val64 &= ~(RMAC_PAUSE_HG_PTIME(0xffff));
#ifdef MAC
    val64 |= RMAC_PAUSE_HG_PTIME(nic->mac_control_rx.rmac_pause_time);
#else
	val64 |= RMAC_PAUSE_HG_PTIME(nic->mac_control.rmac_pause_time);
#endif
	writeq(val64, &bar0->rmac_pause_cfg);

	/* Set the Threshold Limit for Generating the pause frame
	 * If the amount of data in any Queue exceeds ratio of
	 * (mac_control.mc_pause_threshold_q0q3 or q4q7)/256
	 * pause frame is generated
	 */
#ifdef MAC
    val64 = 0;
    for (i = 0; i < 4; i++) {
        val64 |=
            (((u64) 0xFF00 | nic->mac_control_rx.
              mc_pause_threshold_q0q3)
             << (i * 2 * 8));
    }
    writeq(val64, &bar0->mc_pause_thresh_q0q3);

    val64 = 0;
    for (i = 0; i < 4; i++) {
        val64 |=
            (((u64) 0xFF00 | nic->mac_control_rx.
              mc_pause_threshold_q4q7)
             << (i * 2 * 8));
    }
#else
	val64 = 0;
	for (i = 0; i < 4; i++) {
		val64 |=
		    (((u64) 0xFF00 | nic->mac_control.
		      mc_pause_threshold_q0q3)
		     << (i * 2 * 8));
	}
	writeq(val64, &bar0->mc_pause_thresh_q0q3);

	val64 = 0;
	for (i = 0; i < 4; i++) {
		val64 |=
		    (((u64) 0xFF00 | nic->mac_control.
		      mc_pause_threshold_q4q7)
		     << (i * 2 * 8));
	}
#endif

	writeq(val64, &bar0->mc_pause_thresh_q4q7);

	/* TxDMA will stop Read request if the number of read split has exceeded
	 * the limit pointed by shared_splits
	 */
	val64 = readq(&bar0->pic_control);
	val64 |= PIC_CNTL_SHARED_SPLITS(shared_splits);
	writeq(val64, &bar0->pic_control);

	return SUCCESS;
}

/**  
 *  en_dis_able_nicintrs - Enable or Disable the interrupts 
 *  @nic: device private variable,
 *  @mask: A mask indicating which Intr block must be modified and,
 *  @flag: A flag indicating whether to enable or disable the Intrs.
 *  Description: This function will either disable or enable the interrupts
 *  depending on the flag argument. The mask argument can be used to 
 *  enable/disable any Intr block. 
 *  Return Value: NONE.
 */

static void en_dis_able_nic_intrs(struct s2io_nic *nic, u16 mask, int flag)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	register u64 val64 = 0, temp64 = 0;

	/*  Top level interrupt classification */
	/*  PIC Interrupts */
	if ((mask & (TX_PIC_INTR | RX_PIC_INTR))) {
		/*  Enable PIC Intrs in the general intr mask register */
		val64 = TXPIC_INT_M | PIC_RX_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/*  Disabled all PCIX, Flash, MDIO, IIC and GPIO
			 *  interrupts for now. 
			 * TODO */
			writeq(DISABLE_ALL_INTRS, &bar0->pic_int_mask);
			/*  No MSI Support is available presently, so TTI and
			 * RTI interrupts are also disabled.
			 */
		} else if (flag == DISABLE_INTRS) {
			/*  Disable PIC Intrs in the general intr mask register 
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->pic_int_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  DMA Interrupts */
	/*  Enabling/Disabling Tx DMA interrupts */
	if (mask & TX_DMA_INTR) {
		/*  Enable TxDMA Intrs in the general intr mask register */
		val64 = TXDMA_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* Keep all interrupts other than PFC interrupt 
			 * and PCC interrupt disabled in DMA level.
			 */
			val64 = DISABLE_ALL_INTRS & ~(TXDMA_PFC_INT_M | 
						TXDMA_PCC_INT_M);
			writeq(val64, &bar0->txdma_int_mask);
			/* Enable only the MISC error 1 interrupt in PFC block 
			 */
			val64 = DISABLE_ALL_INTRS & (~PFC_MISC_ERR_1);
			writeq(val64, &bar0->pfc_err_mask);
			/* Enable only the FB_ECC error interrupt in PCC block 
			 */
			val64 = DISABLE_ALL_INTRS & (~PCC_FB_ECC_ERR);
			writeq(val64, &bar0->pcc_err_mask);
		} else if (flag == DISABLE_INTRS) {
			/*  Disable TxDMA Intrs in the general intr mask 
			 *  register */
			writeq(DISABLE_ALL_INTRS, &bar0->txdma_int_mask);
			writeq(DISABLE_ALL_INTRS, &bar0->pfc_err_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  Enabling/Disabling Rx DMA interrupts */
	if (mask & RX_DMA_INTR) {
		/*  Enable RxDMA Intrs in the general intr mask register */
		val64 = RXDMA_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* All RxDMA block interrupts are disabled for now 
			 * TODO */
			writeq(DISABLE_ALL_INTRS, &bar0->rxdma_int_mask);
		} else if (flag == DISABLE_INTRS) {
			/*  Disable RxDMA Intrs in the general intr mask 
			 *  register */
			writeq(DISABLE_ALL_INTRS, &bar0->rxdma_int_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  MAC Interrupts */
	/*  Enabling/Disabling MAC interrupts */
	if (mask & (TX_MAC_INTR | RX_MAC_INTR)) {
		val64 = TXMAC_INT_M | RXMAC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* All MAC block error interrupts are disabled for now 
			 * except the link status change interrupt.
			 * TODO*/
			val64 = MAC_INT_STATUS_RMAC_INT;
			temp64 = readq(&bar0->mac_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->mac_int_mask);

			val64 = readq(&bar0->mac_rmac_err_mask);
			val64 &= ~((u64) RMAC_LINK_STATE_CHANGE_INT);
			writeq(val64, &bar0->mac_rmac_err_mask);
		} else if (flag == DISABLE_INTRS) {
			/*  Disable MAC Intrs in the general intr mask register 
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->mac_int_mask);
			writeq(DISABLE_ALL_INTRS,
			       &bar0->mac_rmac_err_mask);

			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  XGXS Interrupts */
	if (mask & (TX_XGXS_INTR | RX_XGXS_INTR)) {
		val64 = TXXGXS_INT_M | RXXGXS_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* All XGXS block error interrupts are disabled for now
			 *  TODO */
			writeq(DISABLE_ALL_INTRS, &bar0->xgxs_int_mask);
		} else if (flag == DISABLE_INTRS) {
			/*  Disable MC Intrs in the general intr mask register 
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->xgxs_int_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  Memory Controller(MC) interrupts */
	if (mask & MC_INTR) {
		val64 = MC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* All MC block error interrupts are disabled for now
			 * TODO */
			writeq(DISABLE_ALL_INTRS, &bar0->mc_int_mask);
		} else if (flag == DISABLE_INTRS) {
			/*  Disable MC Intrs in the general intr mask register
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->mc_int_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}


	/*  Tx traffic interrupts */
	if (mask & TX_TRAFFIC_INTR) {
		val64 = TXTRAFFIC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			/* Enable all the Tx side interrupts */
			writeq(0x0, &bar0->tx_traffic_mask);	/* '0' Enables 
								 * all 64 TX 
								 * interrupt 
								 * levels.
								 */
		} else if (flag == DISABLE_INTRS) {
			/*  Disable Tx Traffic Intrs in the general intr mask 
			 *  register.
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->tx_traffic_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}

	/*  Rx traffic interrupts */
	if (mask & RX_TRAFFIC_INTR) {
		val64 = RXTRAFFIC_INT_M;
		if (flag == ENABLE_INTRS) {
			temp64 = readq(&bar0->general_int_mask);
			temp64 &= ~((u64) val64);
			writeq(temp64, &bar0->general_int_mask);
			writeq(0x0, &bar0->rx_traffic_mask);	/* '0' Enables 
								 * all 8 RX 
								 * interrupt 
								 * levels.
								 */
		} else if (flag == DISABLE_INTRS) {
			/*  Disable Rx Traffic Intrs in the general intr mask 
			 *  register.
			 */
			writeq(DISABLE_ALL_INTRS, &bar0->rx_traffic_mask);
			temp64 = readq(&bar0->general_int_mask);
			val64 |= temp64;
			writeq(val64, &bar0->general_int_mask);
		}
	}
}

/**  
 *  verify_xena_quiescence - Checks whether the H/W is ready 
 *  @val64 :  Value read from adapter status register.
 *  @flag : indicates if the adapter enable bit was ever written once
 *  before.
 *  Description: Returns whether the H/W is ready to go or not. Depending
 *  on whether adapter enable bit was written or not the comparison 
 *  differs and the calling function passes the input argument flag to
 *  indicate this.
 *  Return: 1 If xena is quiescence 
 *          0 If Xena is not quiescence
 */

static int verify_xena_quiescence(u64 val64, int flag)
{
	int ret = 0;
	u64 tmp64 = ~((u64) val64);

	if (!
	    (tmp64 &
	     (ADAPTER_STATUS_TDMA_READY | ADAPTER_STATUS_RDMA_READY |
	      ADAPTER_STATUS_PFC_READY | ADAPTER_STATUS_TMAC_BUF_EMPTY |
	      ADAPTER_STATUS_PIC_QUIESCENT | ADAPTER_STATUS_MC_DRAM_READY |
	      ADAPTER_STATUS_MC_QUEUES_READY | ADAPTER_STATUS_M_PLL_LOCK |
	      ADAPTER_STATUS_P_PLL_LOCK))) {
		if (flag == FALSE) {
			if (!(val64 & ADAPTER_STATUS_RMAC_PCC_IDLE) &&
			    ((val64 & ADAPTER_STATUS_RC_PRC_QUIESCENT) ==
			     ADAPTER_STATUS_RC_PRC_QUIESCENT)) {

				ret = 1;

			}
		} else {
			if (((val64 & ADAPTER_STATUS_RMAC_PCC_IDLE) ==
			     ADAPTER_STATUS_RMAC_PCC_IDLE) &&
			    (!(val64 & ADAPTER_STATUS_RC_PRC_QUIESCENT) ||
			     ((val64 & ADAPTER_STATUS_RC_PRC_QUIESCENT) ==
			      ADAPTER_STATUS_RC_PRC_QUIESCENT))) {

				ret = 1;

			}
		}
	}

	return ret;
}

/**
 * fix_mac_address -  Fix for Mac addr problem on Alpha platforms
 * @sp: Pointer to device specifc structure
 * Description : 
 * New procedure to clear mac address reading  problems on Alpha platforms
 *
 */

void fix_mac_address(nic_t * sp)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64;
	int i = 0;

	while (fix_mac[i] != END_SIGN) {
		writeq(fix_mac[i++], &bar0->gpio_control);
		val64 = readq(&bar0->gpio_control);
	}
}

/**
 *  start_nic - Turns the device on   
 *  @nic : device private variable.
 *  Description: 
 *  This function actually turns the device on. Before this  function is 
 *  called,all Registers are configured from their reset states 
 *  and shared memory is allocated but the NIC is still quiescent. On 
 *  calling this function, the device interrupts are cleared and the NIC is
 *  literally switched on by writing into the adapter control register.
 *  Return Value: 
 *  SUCCESS on success and -1 on failure.
 */

static int start_nic(struct s2io_nic *nic)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	struct net_device *dev = nic->dev;
	register u64 val64 = 0;
	u16 interruptible, i;
	u16 subid;
	struct config_param *config;

	config = &nic->config;

	/*  PRC Initialization and configuration */
	for (i = 0; i < config->RxRingNum; i++) {
		writeq((u64) nic->rx_blocks[i][0].block_dma_addr,
		       &bar0->prc_rxd0_n[i]);

		val64 = readq(&bar0->prc_ctrl_n[i]);
#ifndef CONFIG_2BUFF_MODE
		val64 |= PRC_CTRL_RC_ENABLED;
#else
		val64 |= PRC_CTRL_RC_ENABLED | PRC_CTRL_RING_MODE_3;
#endif
		writeq(val64, &bar0->prc_ctrl_n[i]);
	}

#ifdef CONFIG_2BUFF_MODE
	/* Enabling 2 buffer mode by writing into Rx_pa_cfg reg. */
	val64 = readq(&bar0->rx_pa_cfg);
	val64 |= RX_PA_CFG_IGNORE_L2_ERR;
	writeq(val64, &bar0->rx_pa_cfg);
#endif

	/* Enabling MC-RLDRAM. After enabling the device, we timeout
	 * for around 100ms, which is approximately the time required
	 * for the device to be ready for operation.
	 */
	val64 = readq(&bar0->mc_rldram_mrs);
	val64 |= MC_RLDRAM_QUEUE_SIZE_ENABLE | MC_RLDRAM_MRS_ENABLE;
	writeq(val64, &bar0->mc_rldram_mrs);
	val64 = readq(&bar0->mc_rldram_mrs);

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 10);	/* Delay by around 100 ms. */

	/* Enabling ECC Protection. */
	val64 = readq(&bar0->adapter_control);
	val64 &= ~ADAPTER_ECC_EN;
	writeq(val64, &bar0->adapter_control);

	/* Clearing any possible Link state change interrupts that 
	 * could have popped up just before Enabling the card.
	 */
	val64 = readq(&bar0->mac_rmac_err_reg);
	if (val64)
		writeq(val64, &bar0->mac_rmac_err_reg);

	/* Verify if the device is ready to be enabled, if so enable 
	 * it.
	 */
	val64 = readq(&bar0->adapter_status);
	if (!verify_xena_quiescence(val64, nic->device_enabled_once)) {
		DBG_PRINT(ERR_DBG, "%s: device is not ready, ", dev->name);
		DBG_PRINT(ERR_DBG, "Adapter status reads: 0x%llx\n",
			  (unsigned long long) val64);
		return FAILURE;
	}

	/*  Enable select interrupts */
	interruptible = TX_TRAFFIC_INTR | RX_TRAFFIC_INTR | TX_MAC_INTR |
	    RX_MAC_INTR;
	en_dis_able_nic_intrs(nic, interruptible, ENABLE_INTRS);

	/* With some switches, link might be already up at this point.
	 * Because of this weird behavior, when we enable laser, 
	 * we may not get link. We need to handle this. We cannot 
	 * figure out which switch is misbehaving. So we are forced to 
	 * make a global change. 
	 */

	/* Enabling Laser. */
	val64 = readq(&bar0->adapter_control);
	val64 |= ADAPTER_EOI_TX_ON;
	writeq(val64, &bar0->adapter_control);

	/* SXE-002: Initialize link and activity LED */
	subid = nic->pdev->subsystem_device;
	if ((subid & 0xFF) >= 0x07) {
		val64 = readq(&bar0->gpio_control);
		val64 |= 0x0000800000000000ULL;
		writeq(val64, &bar0->gpio_control);
		val64 = 0x0411040400000000ULL;
		writeq(val64, (void *) ((u8 *) bar0 + 0x2700));
	}

	/* 
	 * Here we are performing soft reset on XGXS to 
	 * force link down. Since link is already up, we will get
	 * link state change interrupt after this reset
	 */
	writeq(0x8007051500000000ULL, &bar0->dtx_control);
	val64 = readq(&bar0->dtx_control);
	writeq(0x80070515000000E0ULL, &bar0->dtx_control);
	val64 = readq(&bar0->dtx_control);
	writeq(0x80070515001F00E4ULL, &bar0->dtx_control);
	val64 = readq(&bar0->dtx_control);

	return SUCCESS;
}

/** 
 *  Free_tx_buffers - Free all queued Tx buffers 
 *  @nic : device private variable.
 *  Description: 
 *  Free all queued Tx buffers.
 *  Return Value: void 
*/

void free_tx_buffers(struct s2io_nic *nic)
{
	struct net_device *dev = nic->dev;
	struct sk_buff *skb;
	TxD_t *txdp;
	int i, j;
#if DEBUG_ON
	int cnt = 0;
#endif
#ifdef MAC
    mac_info_tx_t *mac_control_tx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
#ifdef TXDBD
    u16 off=0;
    u16 block_no=0;
#endif

	config = &nic->config;
#ifdef MAC
	mac_control_tx = &nic->mac_control_tx;
#else
	mac_control = &nic->mac_control;
#endif

	for (i = 0; i < config->TxFIFONum; i++) {
#ifdef TXDBD
    block_no = 0;
#endif
		for (j = 0; j < config->TxCfg[i].FifoLen - 1; j++) {
#ifdef TXDBD
#ifdef MAC
            off = j % mac_control_tx->max_txds_per_block;
            txdp = nic->tx_blocks[i][block_no].block_virt_addr + off;
            if(off == mac_control_tx->max_txds_per_block){
                j++;
                block_no++;
            }
#else
            off = j % mac_control->max_txds_per_block;
            txdp = nic->tx_blocks[i][block_no].block_virt_addr + off;
            if(off == mac_control->max_txds_per_block){
                j++;
                block_no++;
            }
#endif
#else
#ifdef MAC
			txdp = mac_control_tx->txdl_start[i] +
			    (config->MaxTxDs * j);
#else
			txdp = mac_control->txdl_start[i] +
			    (config->MaxTxDs * j);
#endif
#endif
			if (!(txdp->Control_1 & TXD_LIST_OWN_XENA)) {
				/* If owned by host, ignore */
				continue;
			}
			skb =
			    (struct sk_buff *) ((unsigned long) txdp->
						Host_Control);
			if (skb == NULL) {
				DBG_PRINT(ERR_DBG, "%s: NULL skb ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "in Tx Int\n");
				return;
			}
#if DEBUG_ON
			cnt++;
#endif
			dev_kfree_skb(skb);
			memset(txdp, 0, sizeof(TxD_t));
		}
#if DEBUG_ON
		DBG_PRINT(INTR_DBG,
			  "%s:forcibly freeing %d skbs on FIFO%d\n",
			  dev->name, cnt, i);
#endif
#if LATEST_CHANGES
#ifdef MAC
#ifdef TXDBD
                mac_control_tx->tx_curr_put_info[i].block_index = 0;
                mac_control_tx->tx_curr_get_info[i].block_index = 0;
#endif
                mac_control_tx->tx_curr_put_info[i].offset = 0;
                mac_control_tx->tx_curr_get_info[i].offset = 0;
#else
#ifdef TXDBD
                mac_control->tx_curr_put_info[i].block_index = 0;
                mac_control->tx_curr_get_info[i].block_index = 0;
#endif
                mac_control->tx_curr_put_info[i].offset = 0;
                mac_control->tx_curr_get_info[i].offset = 0;
#endif
#endif
	}
}

/**  
 *   stop_nic -  To stop the nic  
 *   @nic ; device private variable.
 *   Description: 
 *   This function does exactly the opposite of what the start_nic() 
 *   function does. This function is called to stop the device.
 *   Return Value:
 *   void.
 */

static void stop_nic(struct s2io_nic *nic)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	register u64 val64 = 0;
	u16 interruptible, i;
	struct config_param *config;

	config = &nic->config;

/*  Disable all interrupts */
	interruptible = TX_TRAFFIC_INTR | RX_TRAFFIC_INTR | TX_MAC_INTR |
	    RX_MAC_INTR;
	en_dis_able_nic_intrs(nic, interruptible, DISABLE_INTRS);

/*  Disable PRCs */
	for (i = 0; i < config->RxRingNum; i++) {
		val64 = readq(&bar0->prc_ctrl_n[i]);
		val64 &= ~((u64) PRC_CTRL_RC_ENABLED);
		writeq(val64, &bar0->prc_ctrl_n[i]);
	}
}

/**  
 *  fill_rx_buffers - Allocates the Rx side skbs 
 *  @nic:  device private variable
 *  @ring_no: ring number 
 *  Description: 
 *  The function allocates Rx side skbs and puts the physical
 *  address of these buffers into the RxD buffer pointers, so that the NIC
 *  can DMA the received frame into these locations.
 *  The NIC supports 3 receive modes, viz
 *  1. single buffer,
 *  2. three buffer and
 *  3. Five buffer modes.
 *  Each mode defines how many fragments the received frame will be split 
 *  up into by the NIC. The frame is split into L3 header, L4 Header, 
 *  L4 payload in three buffer mode and in 5 buffer mode, L4 payload itself
 *  is split into 3 fragments. As of now only single buffer mode is
 *  supported.
 *   Return Value:
 *  SUCCESS on success or an appropriate -ve value on failure.
 */

int fill_rx_buffers(struct s2io_nic *nic, int ring_no)
{
	struct net_device *dev = nic->dev;
	struct sk_buff *skb;
	RxD_t *rxdp;
	int off, off1, size, block_no, block_no1;
	int offset, offset1;
	u32 alloc_tab = 0;
	u32 alloc_cnt = nic->pkt_cnt[ring_no] -
	    atomic_read(&nic->rx_bufs_left[ring_no]);
#ifdef MAC
    mac_info_rx_t *mac_control_rx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
#ifdef CONFIG_2BUFF_MODE
	RxD_t *rxdpnext;
	int nextblk;
	u64 tmp;
	buffAdd_t *ba;
	dma_addr_t rxdpphys;
#endif
#ifdef  MAC
    mac_control_rx = &nic->mac_control_rx;
#else
	mac_control = &nic->mac_control;
#endif
	config = &nic->config;

	if (frame_len[ring_no]) {
		if (frame_len[ring_no] > dev->mtu)
			dev->mtu = frame_len[ring_no];
		size = frame_len[ring_no] + HEADER_ETHERNET_II_802_3_SIZE +
		    HEADER_802_2_SIZE + HEADER_SNAP_SIZE;
	} else {
		size = dev->mtu + HEADER_ETHERNET_II_802_3_SIZE +
		    HEADER_802_2_SIZE + HEADER_SNAP_SIZE;
	}

	while (alloc_tab < alloc_cnt) {
#ifdef MAC
        block_no = mac_control_rx->rx_curr_put_info[ring_no].
            block_index;
        block_no1 = mac_control_rx->rx_curr_get_info[ring_no].
            block_index;
        off = mac_control_rx->rx_curr_put_info[ring_no].offset;
        off1 = mac_control_rx->rx_curr_get_info[ring_no].offset;
#else
		block_no = mac_control->rx_curr_put_info[ring_no].
		    block_index;
		block_no1 = mac_control->rx_curr_get_info[ring_no].
		    block_index;
		off = mac_control->rx_curr_put_info[ring_no].offset;
		off1 = mac_control->rx_curr_get_info[ring_no].offset;
#endif
#ifndef CONFIG_2BUFF_MODE
		offset = block_no * (MAX_RXDS_PER_BLOCK + 1) + off;
		offset1 = block_no1 * (MAX_RXDS_PER_BLOCK + 1) + off1;
#else
		offset = block_no * (MAX_RXDS_PER_BLOCK) + off;
		offset1 = block_no1 * (MAX_RXDS_PER_BLOCK) + off1;
#endif

		rxdp = nic->rx_blocks[ring_no][block_no].
		    block_virt_addr + off;
		if ((offset == offset1) && (rxdp->Host_Control)) {
			DBG_PRINT(INTR_DBG, "%s: Get and Put", dev->name);
			DBG_PRINT(INTR_DBG, " info equated\n");
			goto end;
		}
#ifndef	CONFIG_2BUFF_MODE
		if (rxdp->Control_1 == END_OF_BLOCK) {
#ifdef MAC
            mac_control_rx->rx_curr_put_info[ring_no].
                block_index++;
            mac_control_rx->rx_curr_put_info[ring_no].
                block_index %= nic->block_count[ring_no];
            block_no = mac_control_rx->rx_curr_put_info
                [ring_no].block_index;
            off++;
            off %= (MAX_RXDS_PER_BLOCK + 1);
            mac_control_rx->rx_curr_put_info[ring_no].offset =
                off;
#else
			mac_control->rx_curr_put_info[ring_no].
			    block_index++;
			mac_control->rx_curr_put_info[ring_no].
			    block_index %= nic->block_count[ring_no];
			block_no = mac_control->rx_curr_put_info
			    [ring_no].block_index;
			off++;
			off %= (MAX_RXDS_PER_BLOCK + 1);
			mac_control->rx_curr_put_info[ring_no].offset =
			    off;
#endif
			/*rxdp = nic->rx_blocks[ring_no][block_no].
			   block_virt_addr + off; */
			rxdp = (RxD_t *) ((unsigned long) rxdp->Control_2);
			DBG_PRINT(INTR_DBG, "%s: Next block at: %p\n",
				  dev->name, rxdp);
		}
#else
		if (rxdp->Host_Control == END_OF_BLOCK) {
#ifdef MAC
            mac_control_rx->rx_curr_put_info[ring_no].
                block_index++;
            mac_control_rx->rx_curr_put_info[ring_no].
                block_index %= nic->block_count[ring_no];
            block_no = mac_control_rx->rx_curr_put_info
                [ring_no].block_index;
            off = 0;
            DBG_PRINT(INTR_DBG, "%s: block%d at: 0x%llx\n",
                  dev->name, block_no,
                  (unsigned long long) rxdp->Control_1);
            mac_control_rx->rx_curr_put_info[ring_no].offset =
                off;
#else
			mac_control->rx_curr_put_info[ring_no].
			    block_index++;
			mac_control->rx_curr_put_info[ring_no].
			    block_index %= nic->block_count[ring_no];
			block_no = mac_control->rx_curr_put_info
			    [ring_no].block_index;
			off = 0;
			DBG_PRINT(INTR_DBG, "%s: block%d at: 0x%llx\n",
				  dev->name, block_no,
				  (unsigned long long) rxdp->Control_1);
			mac_control->rx_curr_put_info[ring_no].offset =
			    off;
#endif
			rxdp = nic->rx_blocks[ring_no][block_no].
			    block_virt_addr;
		}
#endif

#ifndef	CONFIG_2BUFF_MODE
		if (rxdp->Control_1 & RXD_OWN_XENA) {
#else
		if (rxdp->Control_2 & BIT(0)) {
#endif
#ifdef MAC
            mac_control_rx->rx_curr_put_info[ring_no].
                offset = off;
#else
			mac_control->rx_curr_put_info[ring_no].
			    offset = off;
#endif
			goto end;
		}
#ifdef	CONFIG_2BUFF_MODE
		/* RxDs Spanning chachelines will be replenished only 
		 * if the succeeding RxD is also owned by Host. It 
		 * will always be the ((8*i)+3) and ((8*i)+6) 
		 * descriptors for the 48 byte descriptor. The offending 
		 * decsriptor is of-course the 3rd descriptor.
		 */
		rxdpphys = nic->rx_blocks[ring_no][block_no].
		    block_dma_addr + (off * sizeof(RxD_t));
		if (((u64) (rxdpphys)) % 128 > 80) {
			rxdpnext = nic->rx_blocks[ring_no][block_no].
			    block_virt_addr + (off + 1);
			if (rxdpnext->Host_Control == END_OF_BLOCK) {
				nextblk = (block_no + 1) %
				    (nic->block_count[ring_no]);
				rxdpnext = nic->rx_blocks[ring_no]
				    [nextblk].block_virt_addr;
			}
			if (rxdpnext->Control_2 & BIT(0))
				goto end;
		}
#endif

#ifndef	CONFIG_2BUFF_MODE
		skb = dev_alloc_skb(size + HEADER_ALIGN_LAYER_3);
#else
		skb =
		    dev_alloc_skb(dev->mtu + ALIGN_SIZE +
				  /*BUF0_LEN + */ 22);
#endif
		if (!skb) {
			DBG_PRINT(ERR_DBG, "%s: Out of ", dev->name);
			DBG_PRINT(ERR_DBG, "memory to allocate SKBs\n");
			return -ENOMEM;
		}
#ifndef	CONFIG_2BUFF_MODE
		skb_reserve(skb, HEADER_ALIGN_LAYER_3);
		memset(rxdp, 0, sizeof(RxD_t));
		rxdp->Buffer0_ptr = pci_map_single
		    (nic->pdev, skb->data, size, PCI_DMA_FROMDEVICE);
		rxdp->Control_2 &= (~MASK_BUFFER0_SIZE);
		rxdp->Control_2 |= SET_BUFFER0_SIZE(size);
		rxdp->Host_Control = (unsigned long) (skb);
		rxdp->Control_1 |= RXD_OWN_XENA;
		off++;
		off %= (MAX_RXDS_PER_BLOCK + 1);
#ifdef MAC
        mac_control_rx->rx_curr_put_info[ring_no].offset = off;
#else
		mac_control->rx_curr_put_info[ring_no].offset = off;
#endif
#else
		ba = &nic->ba[block_no][off];
		tmp = (u64) skb->data;
		tmp += ALIGN_SIZE;
		tmp &= ~ALIGN_SIZE;
		skb->data = (void *) tmp;

		ba->ba_0_org = (void *)
		    kmalloc(BUF0_LEN + ALIGN_SIZE, GFP_ATOMIC);
		if (!ba->ba_0_org)
			return -ENOMEM;
		tmp = (u64) ba->ba_0_org;
		tmp += ALIGN_SIZE;
		tmp &= ~((u64) ALIGN_SIZE);
		ba->ba_0 = (void *) tmp;

		ba->ba_1_org = (void *)
		    kmalloc(BUF1_LEN + ALIGN_SIZE, GFP_ATOMIC);
		if (!ba->ba_1_org)
			return -ENOMEM;
		tmp = (u64) ba->ba_1_org;
		tmp += ALIGN_SIZE;
		tmp &= ~((u64) ALIGN_SIZE);
		ba->ba_1 = (void *) tmp;

		memset(rxdp, 0, sizeof(RxD_t));

		rxdp->Buffer2_ptr = pci_map_single
		    (nic->pdev, skb->data, dev->mtu + 22,
		     PCI_DMA_FROMDEVICE);
		rxdp->Buffer0_ptr =
		    pci_map_single(nic->pdev, ba->ba_0, BUF0_LEN,
				   PCI_DMA_FROMDEVICE);
		rxdp->Buffer1_ptr =
		    pci_map_single(nic->pdev, ba->ba_1, BUF1_LEN,
				   PCI_DMA_FROMDEVICE);

		rxdp->Control_2 = SET_BUFFER2_SIZE(dev->mtu + 22);
		rxdp->Control_2 |= SET_BUFFER0_SIZE(BUF0_LEN);
		rxdp->Control_2 |= SET_BUFFER1_SIZE(1);	/* dummy. */
		rxdp->Control_2 |= BIT(0);	/* Set Buffer_Empty bit. */
		rxdp->Host_Control = (u64) ((unsigned long) (skb));
		rxdp->Control_1 |= RXD_OWN_XENA;
		off++;
#ifdef MAC
        mac_control_rx->rx_curr_put_info[ring_no].offset = off;
#else
		mac_control->rx_curr_put_info[ring_no].offset = off;
#endif
#endif
		atomic_inc(&nic->rx_bufs_left[ring_no]);
		alloc_tab++;
	}

      end:
	return SUCCESS;
}

/**
 *  free_rx_buffers - Frees all Rx buffers   
 *  @sp: device private variable.
 *  Description: 
 *  This function will free all Rx buffers allocated by host.
 *  Return Value:
 *  NONE.
 */

static void free_rx_buffers(struct s2io_nic *sp)
{
	struct net_device *dev = sp->dev;
	int i, j, blk = 0, off, buf_cnt = 0;
	RxD_t *rxdp;
	struct sk_buff *skb;
#ifdef MAC
    mac_info_rx_t *mac_control_rx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
#ifdef CONFIG_2BUFF_MODE
	buffAdd_t *ba;
#endif

	config = &sp->config;
#ifdef MAC
    mac_control_rx = &sp->mac_control_rx;
#else
	mac_control = &sp->mac_control;
#endif

	for (i = 0; i < config->RxRingNum; i++) {
		for (j = 0, blk = 0; j < config->RxCfg[i].NumRxd; j++) {
			off = j % (MAX_RXDS_PER_BLOCK + 1);
			rxdp = sp->rx_blocks[i][blk].block_virt_addr + off;

#ifndef CONFIG_2BUFF_MODE
			if (rxdp->Control_1 == END_OF_BLOCK) {
				rxdp =
				    (RxD_t *) ((unsigned long) rxdp->
					       Control_2);
				j++;
				blk++;
			}
#else
			if (rxdp->Host_Control == END_OF_BLOCK) {
				blk++;
				continue;
			}
#endif

			if (!(rxdp->Control_1 & RXD_OWN_XENA)) {
				memset(rxdp, 0, sizeof(RxD_t));
				continue;
			}

			skb =
			    (struct sk_buff *) ((unsigned long) rxdp->
						Host_Control);
			if (skb) {
#ifndef CONFIG_2BUFF_MODE
				pci_unmap_single(sp->pdev, (dma_addr_t)
						 rxdp->Buffer0_ptr,
						 dev->mtu +
						 HEADER_ETHERNET_II_802_3_SIZE
						 + HEADER_802_2_SIZE +
						 HEADER_SNAP_SIZE,
						 PCI_DMA_FROMDEVICE);
#else
				ba = &sp->ba[blk][off];
				pci_unmap_single(sp->pdev, (dma_addr_t)
						 rxdp->Buffer0_ptr,
						 BUF0_LEN,
						 PCI_DMA_FROMDEVICE);
				pci_unmap_single(sp->pdev, (dma_addr_t)
						 rxdp->Buffer1_ptr,
						 BUF1_LEN,
						 PCI_DMA_FROMDEVICE);
				pci_unmap_single(sp->pdev, (dma_addr_t)
						 rxdp->Buffer2_ptr,
						 dev->mtu + 22,
						 PCI_DMA_FROMDEVICE);
				kfree(ba->ba_0_org);
				kfree(ba->ba_1_org);
#endif
				dev_kfree_skb(skb);
				atomic_dec(&sp->rx_bufs_left[i]);
				buf_cnt++;
			}
			memset(rxdp, 0, sizeof(RxD_t));
		}
#ifdef MAC
        mac_control_rx->rx_curr_put_info[i].block_index = 0;
        mac_control_rx->rx_curr_get_info[i].block_index = 0;
        mac_control_rx->rx_curr_put_info[i].offset = 0;
        mac_control_rx->rx_curr_get_info[i].offset = 0;
#else
		mac_control->rx_curr_put_info[i].block_index = 0;
		mac_control->rx_curr_get_info[i].block_index = 0;
		mac_control->rx_curr_put_info[i].offset = 0;
		mac_control->rx_curr_get_info[i].offset = 0;
#endif
		atomic_set(&sp->rx_bufs_left[i], 0);
		DBG_PRINT(INIT_DBG, "%s:Freed 0x%x Rx Buffers on ring%d\n",
			  dev->name, buf_cnt, i);
	}
}

/**
 * s2io_poll - Rx interrupt handler for NAPI support
 * @dev : pointer to the device structure.
 * @budget : The number of packets that were budgeted to be processed 
 * during  one pass through the 'Poll" function.
 * Description:
 * Comes into picture only if NAPI support has been incorporated. It does
 * the same thing that rx_intr_handler does, but not in a interrupt context
 * also It will process only a given number of packets.
 * Return value:
 * 0 on success and 1 if there are No Rx packets to be processed.
 */

#ifdef CONFIG_S2IO_NAPI
static int s2io_poll(struct net_device *dev, int *budget)
{
	nic_t *nic = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	int pkts_to_process = *budget, pkt_cnt = 0;
	register u64 val64 = 0;
	rx_curr_get_info_t offset_info;
	int i, block_no;
#ifndef CONFIG_2BUFF_MODE
	u16 val16, cksum;
#endif
	struct sk_buff *skb;
	RxD_t *rxdp;
#ifdef MAC
    mac_info_rx_t *mac_control_rx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
#ifdef CONFIG_2BUFF_MODE
	buffAdd_t *ba;
#endif

	config = &nic->config;
#ifdef MAC
    mac_control_rx = &nic->mac_control_rx;
#else
	mac_control = &nic->mac_control;
#endif

	if (pkts_to_process > dev->quota)
		pkts_to_process = dev->quota;

	val64 = readq(&bar0->rx_traffic_int);
	writeq(val64, &bar0->rx_traffic_int);

	for (i = 0; i < config->RxRingNum; i++) {
		if (--pkts_to_process < 0) {
			goto no_rx;
		}
#ifdef MAC
        offset_info = mac_control_rx->rx_curr_get_info[i];
#else
		offset_info = mac_control->rx_curr_get_info[i];
#endif
		block_no = offset_info.block_index;
		rxdp = nic->rx_blocks[i][block_no].block_virt_addr +
		    offset_info.offset;
#ifndef	CONFIG_2BUFF_MODE
		while (!(rxdp->Control_1 & RXD_OWN_XENA)) {
			if (rxdp->Control_1 == END_OF_BLOCK) {
				rxdp =
				    (RxD_t *) ((unsigned long) rxdp->
					       Control_2);
				offset_info.offset++;
				offset_info.offset %=
				    (MAX_RXDS_PER_BLOCK + 1);
				block_no++;
				block_no %= nic->block_count[i];
#ifdef MAC
                mac_control_rx->rx_curr_get_info[i].
                    offset = offset_info.offset;
                mac_control_rx->rx_curr_get_info[i].
                    block_index = block_no;
#else
				mac_control->rx_curr_get_info[i].
				    offset = offset_info.offset;
				mac_control->rx_curr_get_info[i].
				    block_index = block_no;
#endif
				continue;
			}
			skb =
			    (struct sk_buff *) ((unsigned long) rxdp->
						Host_Control);
			if (skb == NULL) {
				DBG_PRINT(ERR_DBG, "%s: The skb is ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Null in Rx Intr\n");
				return 0;
			}
			val64 = RXD_GET_BUFFER0_SIZE(rxdp->Control_2);
			val16 = (u16) (val64 >> 48);
			cksum = RXD_GET_L4_CKSUM(rxdp->Control_1);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer0_ptr,
					 dev->mtu +
					 HEADER_ETHERNET_II_802_3_SIZE +
					 HEADER_802_2_SIZE +
					 HEADER_SNAP_SIZE,
					 PCI_DMA_FROMDEVICE);
			rx_osm_handler(nic, val16, rxdp, i);
			pkt_cnt++;
			offset_info.offset++;
			offset_info.offset %= (MAX_RXDS_PER_BLOCK + 1);
			rxdp =
			    nic->rx_blocks[i][block_no].block_virt_addr +
			    offset_info.offset;
#ifdef MAC
            mac_control_rx->rx_curr_get_info[i].offset =
                offset_info.offset;
#else
			mac_control->rx_curr_get_info[i].offset =
			    offset_info.offset;
#endif
			if ((indicate_max_pkts)
			    && (pkt_cnt > indicate_max_pkts))
				break;
		}
#else
		while ((!(rxdp->Control_1 & RXD_OWN_XENA)) &&
		       !(rxdp->Control_2 & BIT(0))) {
			skb = (struct sk_buff *) ((unsigned long)
						  rxdp->Host_Control);
			if (skb == NULL) {
				DBG_PRINT(ERR_DBG, "%s: The skb is ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Null in Rx Intr\n");
				return;
			}

			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer0_ptr,
					 BUF0_LEN, PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer1_ptr,
					 BUF1_LEN, PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer2_ptr,
					 dev->mtu + 22,
					 PCI_DMA_FROMDEVICE);
			ba = &nic->ba[block_no][offset_info.offset];

			rx_osm_handler(nic, rxdp, i, ba);

			offset_info.offset++;
#ifdef MAC
            mac_control_rx->rx_curr_get_info[i].offset =
                offset_info.offset;
#else
			mac_control->rx_curr_get_info[i].offset =
			    offset_info.offset;
#endif
			rxdp =
			    nic->rx_blocks[i][block_no].block_virt_addr +
			    offset_info.offset;

			if (offset_info.offset &&
			    (!(offset_info.offset % MAX_RXDS_PER_BLOCK))) {
				offset_info.offset = 0;
#ifdef MAC
				mac_control_rx->rx_curr_get_info[i].
				    offset = offset_info.offset;
#else
				mac_control->rx_curr_get_info[i].
				    offset = offset_info.offset;
#endif
				block_no++;
				block_no %= nic->block_count[i];
#ifdef MAC
                mac_control_rx->rx_curr_get_info[i].
                    block_index = block_no;
#else
				mac_control->rx_curr_get_info[i].
				    block_index = block_no;
#endif
				rxdp =
				    nic->rx_blocks[i][block_no].
				    block_virt_addr;
			}
			pkt_cnt++;
			if ((indicate_max_pkts) &&
			    (pkt_cnt > indicate_max_pkts))
				break;
		}
#endif
		if ((indicate_max_pkts) && (pkt_cnt > indicate_max_pkts))
			break;
	}
	if (!pkt_cnt)
		pkt_cnt = 1;

	for (i = 0; i < config->RxRingNum; i++)
		fill_rx_buffers(nic, i);

	dev->quota -= pkt_cnt;
	*budget -= pkt_cnt;
	netif_rx_complete(dev);

/* Re enable the Rx interrupts. */
	en_dis_able_nic_intrs(nic, RX_TRAFFIC_INTR, ENABLE_INTRS);
	return 0;

      no_rx:
	for (i = 0; i < config->RxRingNum; i++)
		fill_rx_buffers(nic, i);
	dev->quota -= pkt_cnt;
	*budget -= pkt_cnt;
	return 1;
}
#else
/**  
 *  rx_intr_handler - Rx interrupt handler
 *  @nic: device private variable.
 *  Description: 
 *  If the interrupt is because of a received frame or if the 
 *  receive ring contains fresh as yet un-processed frames,this function is
 *  called. It picks out the RxD at which place the last Rx processing had 
 *  stopped and sends the skb to the OSM's Rx handler and then increments 
 *  the offset.
 *  Return Value:
 *  NONE.
 */

static void rx_intr_handler(struct s2io_nic *nic)
{
	struct net_device *dev = (struct net_device *) nic->dev;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	rx_curr_get_info_t offset_info;
	RxD_t *rxdp;
	struct sk_buff *skb;
#ifndef CONFIG_2BUFF_MODE
	u16 val16, cksum;
#endif
	register u64 val64 = 0;
	int i, block_no, pkt_cnt = 0;
#ifdef MAC
    mac_info_rx_t *mac_control_rx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
#ifdef CONFIG_2BUFF_MODE
	buffAdd_t *ba;
#endif

	config = &nic->config;
#ifdef MAC
    mac_control_rx = &nic->mac_control_rx;
#else
	mac_control = &nic->mac_control;
#endif

#if DEBUG_ON
	nic->rxint_cnt++;
#endif

/* rx_traffic_int reg is an R1 register, hence we read and write back 
 * the samevalue in the register to clear it.
 */
	val64 = readq(&bar0->rx_traffic_int);
	writeq(val64, &bar0->rx_traffic_int);

	for (i = 0; i < config->RxRingNum; i++) {
#ifdef MAC
        offset_info = mac_control_rx->rx_curr_get_info[i];
#else
		offset_info = mac_control->rx_curr_get_info[i];
#endif
		block_no = offset_info.block_index;
		rxdp = nic->rx_blocks[i][block_no].block_virt_addr +
		    offset_info.offset;
#ifndef	CONFIG_2BUFF_MODE
		while (!(rxdp->Control_1 & RXD_OWN_XENA)) {
			if (rxdp->Control_1 == END_OF_BLOCK) {
				rxdp = (RxD_t *) ((unsigned long)
						  rxdp->Control_2);
				offset_info.offset++;
				offset_info.offset %=
				    (MAX_RXDS_PER_BLOCK + 1);
				block_no++;
				block_no %= nic->block_count[i];
#ifdef MAC
                mac_control_rx->rx_curr_get_info[i].
                    offset = offset_info.offset;
                mac_control_rx->rx_curr_get_info[i].
                    block_index = block_no;
#else
				mac_control->rx_curr_get_info[i].
				    offset = offset_info.offset;
				mac_control->rx_curr_get_info[i].
				    block_index = block_no;
#endif
				continue;
			}
			skb = (struct sk_buff *) ((unsigned long)
						  rxdp->Host_Control);
			if (skb == NULL) {
				DBG_PRINT(ERR_DBG, "%s: The skb is ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Null in Rx Intr\n");
				return;
			}
			val64 = RXD_GET_BUFFER0_SIZE(rxdp->Control_2);
			val16 = (u16) (val64 >> 48);
			cksum = RXD_GET_L4_CKSUM(rxdp->Control_1);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer0_ptr,
					 dev->mtu +
					 HEADER_ETHERNET_II_802_3_SIZE +
					 HEADER_802_2_SIZE +
					 HEADER_SNAP_SIZE,
					 PCI_DMA_FROMDEVICE);
			rx_osm_handler(nic, val16, rxdp, i);
			offset_info.offset++;
			offset_info.offset %= (MAX_RXDS_PER_BLOCK + 1);
			rxdp =
			    nic->rx_blocks[i][block_no].block_virt_addr +
			    offset_info.offset;
#ifdef MAC
            mac_control_rx->rx_curr_get_info[i].offset =
                offset_info.offset;
#else
			mac_control->rx_curr_get_info[i].offset =
			    offset_info.offset;
#endif
			pkt_cnt++;
			if ((indicate_max_pkts)
			    && (pkt_cnt > indicate_max_pkts))
				break;
		}
#else
		while ((!(rxdp->Control_1 & RXD_OWN_XENA)) &&
		       !(rxdp->Control_2 & BIT(0))) {
			skb = (struct sk_buff *) ((unsigned long)
						  rxdp->Host_Control);
			if (skb == NULL) {
				DBG_PRINT(ERR_DBG, "%s: The skb is ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Null in Rx Intr\n");
				return;
			}

			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer0_ptr,
					 BUF0_LEN, PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer1_ptr,
					 BUF1_LEN, PCI_DMA_FROMDEVICE);
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 rxdp->Buffer2_ptr,
					 dev->mtu + 22,
					 PCI_DMA_FROMDEVICE);
			ba = &nic->ba[block_no][offset_info.offset];

			rx_osm_handler(nic, rxdp, i, ba);

			offset_info.offset++;
#ifdef MAC
            mac_control->rx_curr_get_info[i].offset =
                offset_info.offset;
#else
			mac_control->rx_curr_get_info[i].offset =
			    offset_info.offset;
#endif
			rxdp =
			    nic->rx_blocks[i][block_no].block_virt_addr +
			    offset_info.offset;

			if (offset_info.offset &&
			    (!(offset_info.offset % MAX_RXDS_PER_BLOCK))) {
				offset_info.offset = 0;
#ifdef MAC
                mac_control_rx->rx_curr_get_info[i].
                    offset = offset_info.offset;
#else
				mac_control->rx_curr_get_info[i].
				    offset = offset_info.offset;
#endif
				block_no++;
				block_no %= nic->block_count[i];
#ifdef MAC
                mac_control_rx->rx_curr_get_info[i].
                    block_index = block_no;
#else
				mac_control->rx_curr_get_info[i].
				    block_index = block_no;
#endif
				rxdp =
				    nic->rx_blocks[i][block_no].
				    block_virt_addr;
			}
			pkt_cnt++;
			if ((indicate_max_pkts)
			    && (pkt_cnt > indicate_max_pkts))
				break;
		}
#endif
		if ((indicate_max_pkts) && (pkt_cnt > indicate_max_pkts))
			break;
	}
}
#endif
/**  
 *  tx_intr_handler - Transmit interrupt handler
 *  @nic : device private variable
 *  Description: 
 *  If an interrupt was raised to indicate DMA complete of the 
 *  Tx packet, this function is called. It identifies the last TxD 
 *  whose buffer was freed and frees all skbs whose data have already 
 *  DMA'ed into the NICs internal memory.
 *  Return Value:
 *  NONE
 */

static void tx_intr_handler(struct s2io_nic *nic)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	struct net_device *dev = (struct net_device *) nic->dev;
	tx_curr_get_info_t offset_info, offset_info1;
	struct sk_buff *skb;
	TxD_t *txdlp;
	register u64 val64 = 0;
	int i;
	u16 j, frg_cnt;
#ifdef MAC
    mac_info_tx_t *mac_control_tx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
#if DEBUG_ON
	int cnt = 0;
	nic->txint_cnt++;
#endif
#ifdef TXDBD
    u16 off = 0, off1 = 0;
    u16 offset = 0, offset1 = 0;
    u16 block_no = 0, block_no1 = 0;
#endif

	config = &nic->config;
#ifdef MAC
    mac_control_tx = &nic->mac_control_tx;
#else
	mac_control = &nic->mac_control;
#endif

	/* tx_traffic_int reg is an R1 register, hence we read and write 
	 * back the samevalue in the register to clear it.
	 */
	val64 = readq(&bar0->tx_traffic_int);
	writeq(val64, &bar0->tx_traffic_int);

	for (i = 0; i < config->TxFIFONum; i++) {
#ifdef TXDBD
#ifdef MAC
        offset_info = mac_control_tx->tx_curr_get_info[i];
        offset_info1 = mac_control_tx->tx_curr_put_info[i];

        off = offset_info.offset;
        off1 = offset_info1.offset;

        block_no = mac_control_tx->tx_curr_get_info[i].block_index;
        block_no1 = mac_control_tx->tx_curr_put_info[i].block_index;

        offset = block_no * (mac_control_tx->max_txds_per_block) + off ;
        offset1 = block_no1 * (mac_control_tx->max_txds_per_block) + off1;

        txdlp = nic->tx_blocks[i][block_no].block_virt_addr + off;
        while ((!(txdlp->Control_1 & TXD_LIST_OWN_XENA)) &&
               (offset != offset1) &&
               (txdlp->Host_Control)) {
#else
        offset_info = mac_control->tx_curr_get_info[i];
        offset_info1 = mac_control->tx_curr_put_info[i];

        off = offset_info.offset;
        off1 = offset_info1.offset;

        block_no = mac_control->tx_curr_get_info[i].block_index;
        block_no1 = mac_control->tx_curr_put_info[i].block_index;

        offset = block_no * (mac_control->max_txds_per_block) + off ;
        offset1 = block_no1 * (mac_control->max_txds_per_block) + off1;

        txdlp = nic->tx_blocks[i][block_no].block_virt_addr + off;
        while ((!(txdlp->Control_1 & TXD_LIST_OWN_XENA)) &&
               (offset != offset1) &&
               (txdlp->Host_Control)) {
#endif
#else
#ifdef MAC
       offset_info = mac_control_tx->tx_curr_get_info[i];
        offset_info1 = mac_control_tx->tx_curr_put_info[i];
        txdlp = mac_control_tx->txdl_start[i] +
            (config->MaxTxDs * offset_info.offset);
        while ((!(txdlp->Control_1 & TXD_LIST_OWN_XENA)) &&
               (offset_info.offset != offset_info1.offset) &&
               (txdlp->Host_Control)) {

#else
		offset_info = mac_control->tx_curr_get_info[i];
		offset_info1 = mac_control->tx_curr_put_info[i];
		txdlp = mac_control->txdl_start[i] +
		    (config->MaxTxDs * offset_info.offset);
		while ((!(txdlp->Control_1 & TXD_LIST_OWN_XENA)) &&
		       (offset_info.offset != offset_info1.offset) &&
		       (txdlp->Host_Control)) {
#endif
#endif
			/* Check for TxD errors */
			if (txdlp->Control_1 & TXD_T_CODE) {
				unsigned long long err;
				err = txdlp->Control_1 & TXD_T_CODE;
				DBG_PRINT(ERR_DBG, "***TxD error %llx\n",
					  err);
			}

			skb = (struct sk_buff *) ((unsigned long)
						  txdlp->Host_Control);
			if (skb == NULL) {
				DBG_PRINT(ERR_DBG, "%s: Null skb ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "in Tx Free Intr\n");
				return;
			}
			nic->tx_pkt_count++;

			frg_cnt = skb_shinfo(skb)->nr_frags;

			/*  For unfragmented skb */
			pci_unmap_single(nic->pdev, (dma_addr_t)
					 txdlp->Buffer_Pointer,
					 skb->len - skb->data_len,
					 PCI_DMA_TODEVICE);
			if (frg_cnt) {
				TxD_t *temp = txdlp;
				txdlp++;
				for (j = 0; j < frg_cnt; j++, txdlp++) {
					skb_frag_t *frag =
					    &skb_shinfo(skb)->frags[j];
					pci_unmap_page(nic->pdev,
						       (dma_addr_t)
						       txdlp->
						       Buffer_Pointer,
						       frag->size,
						       PCI_DMA_TODEVICE);
				}
				txdlp = temp;
			}
			memset(txdlp, 0,
			       (sizeof(TxD_t) * config->MaxTxDs));

			/* Updating the statistics block */
			nic->stats.tx_packets++;
			nic->stats.tx_bytes += skb->len;
#if DEBUG_ON
			nic->txpkt_bytes += skb->len;
			cnt++;
#endif
			dev_kfree_skb_irq(skb);
#ifdef TXDBD
#ifdef MAC
            offset_info.offset += config->MaxTxDs;
            if( offset_info.offset == mac_control_tx->max_txds_per_block)
            //if( offset_info.offset == MAX_TXDS_PER_BLOCK)
             {
                     mac_control_tx->tx_curr_get_info[i].block_index++;
                     mac_control_tx->tx_curr_get_info[i].block_index %=
                                             nic->tx_block_count[i];
                     offset_info.offset= 0;
                     block_no = mac_control_tx->tx_curr_get_info[i].block_index;
             }
             mac_control_tx->tx_curr_get_info[i].offset =
                                   offset_info.offset;
             offset = block_no * (mac_control_tx->max_txds_per_block)
			 + offset_info.offset ;
	     txdlp = nic->tx_blocks[i][block_no].block_virt_addr
                                     +  offset_info.offset;
#else
            offset_info.offset += config->MaxTxDs;
            if( offset_info.offset == mac_control->max_txds_per_block)
            //if( offset_info.offset == MAX_TXDS_PER_BLOCK)
             {
                     mac_control->tx_curr_get_info[i].block_index++;
                     mac_control->tx_curr_get_info[i].block_index %=
                                             nic->tx_block_count[i];
                     offset_info.offset= 0;
                     block_no = mac_control->tx_curr_get_info[i].block_index;
             }
             mac_control->tx_curr_get_info[i].offset =
                                   offset_info.offset;

             offset = block_no * (mac_control->max_txds_per_block)
			 + offset_info.offset ;
             txdlp = nic->tx_blocks[i][block_no].block_virt_addr
                                     +  offset_info.offset;
#endif 
#else

			offset_info.offset++;
			offset_info.offset %= offset_info.fifo_len + 1;
#ifdef MAC
            txdlp = mac_control_tx->txdl_start[i] +
                (config->MaxTxDs * offset_info.offset);
            mac_control_tx->tx_curr_get_info[i].offset =
                offset_info.offset;
#else
			txdlp = mac_control->txdl_start[i] +
			    (config->MaxTxDs * offset_info.offset);
			mac_control->tx_curr_get_info[i].offset =
			    offset_info.offset;
#endif
#endif
		}
#if DEBUG_ON
		DBG_PRINT(INTR_DBG, "%s: freed %d Tx Pkts\n", dev->name,
			  cnt);
#endif
	}

	spin_lock(&nic->tx_lock);
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
	spin_unlock(&nic->tx_lock);
}

/**  
 *  alarm_intr_handler - Alarm Interrrupt handler
 *  @nic: device private variable
 *  Description: If the interrupt was neither because of Rx packet or Tx 
 *  complete, this function is called. If the interrupt was to indicate
 *  a loss of link, the OSM link status handler is invoked for any other 
 *  alarm interrupt the block that raised the interrupt is displayed 
 *  and a H/W reset is issued.
 *  Return Value:
 *  NONE
*/

static void alarm_intr_handler(struct s2io_nic *nic)
{
	struct net_device *dev = (struct net_device *) nic->dev;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	register u64 val64 = 0, err_reg = 0;

	/* Handling link status change error Intr */
	err_reg = readq(&bar0->mac_rmac_err_reg);
	writeq(err_reg, &bar0->mac_rmac_err_reg);
	if (err_reg & RMAC_LINK_STATE_CHANGE_INT) {
#ifdef INIT_TQUEUE
		schedule_task(&nic->set_link_task);
#else
		schedule_work(&nic->set_link_task);
#endif
	}

	/* In case of a serious error, the device will be Reset. */
	val64 = readq(&bar0->serr_source);
	if (val64 & SERR_SOURCE_ANY) {
		DBG_PRINT(ERR_DBG, "%s: Device indicates ", dev->name);
		DBG_PRINT(ERR_DBG, "serious error!!\n");
#ifdef INIT_TQUEUE
		schedule_task(&nic->rst_timer_task);
#else
		schedule_work(&nic->rst_timer_task);
#endif
	}

	/*
	 * Also as mentioned in the latest Errata sheets if the PCC_FB_ECC
	 * Error occurs, the adapter will be recycled by disabling the
	 * adapter enable bit and enabling it again after the device 
	 * becomes Quiescent.
	 */
	val64 = readq(&bar0->pcc_err_reg);
	writeq(val64, &bar0->pcc_err_reg);
	if (val64 & PCC_FB_ECC_DB_ERR) {
		u64 ac = readq(&bar0->adapter_control);
		ac &= ~(ADAPTER_CNTL_EN);
		writeq(ac, &bar0->adapter_control);
		ac = readq(&bar0->adapter_control);
#ifdef INIT_TQUEUE
		schedule_task(&nic->set_link_task);
#else
		schedule_work(&nic->set_link_task);
#endif
	}

/* Other type of interrupts are not being handled now,  TODO*/
}

/** 
 *  waitFor Cmd Complete - waits for a command to complete.
 *  @sp : private member of the device structure, which is a pointer to the 
 *  s2io_nic structure.
 *  Description: Function that waits for a command to Write into RMAC 
 *  ADDR DATA registers to be completed and returns either success or 
 *  error depending on whether the command was complete or not. 
 *  Return value:
 *   SUCCESS on success and FAILURE on failure.
 */

int wait_for_cmd_complete(nic_t * sp)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	int ret = FAILURE, cnt = 0;
	u64 val64;

	while (TRUE) {
		val64 =
		    RMAC_ADDR_CMD_MEM_RD | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD
		    | RMAC_ADDR_CMD_MEM_OFFSET(0);
		writeq(val64, &bar0->rmac_addr_cmd_mem);
		val64 = readq(&bar0->rmac_addr_cmd_mem);
		if (!val64) {
			ret = SUCCESS;
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
		if (cnt++ > 10)
			break;
	}

	return ret;
}

/** 
 *  s2io_reset - Resets the card. 
 *  @sp : private member of the device structure.
 *  Description: Function to Reset the card. This function then also
 *  restores the previously saved PCI configuration space registers as 
 *  the card reset also resets the Configration space.
 *  Return value:
 *  void.
 */

void s2io_reset(nic_t * sp)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64;
	u16 subid;

	val64 = SW_RESET_ALL;
	writeq(val64, &bar0->sw_reset);

	/* At this stage, if the PCI write is indeed completed, the 
	 * card is reset and so is the PCI Config space of the device. 
	 * So a read cannot be issued at this stage on any of the 
	 * registers to ensure the write into "sw_reset" register
	 * has gone through.
	 * Question: Is there any system call that will explicitly force
	 * all the write commands still pending on the bus to be pushed
	 * through?
	 * As of now I'am just giving a 250ms delay and hoping that the
	 * PCI write to sw_reset register is done by this time.
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 4);

	/* Restore the PCI state saved during initializarion. */
	pci_restore_state(sp->pdev, sp->config_space);
	s2io_init_pci(sp);

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 4);

	/* SXE-002: Configure link and activity LED to turn it off */
	subid = sp->pdev->subsystem_device;
	if ((subid & 0xFF) >= 0x07) {
		val64 = readq(&bar0->gpio_control);
		val64 |= 0x0000800000000000ULL;
		writeq(val64, &bar0->gpio_control);
		val64 = 0x0411040400000000ULL;
		writeq(val64, (void *) ((u8 *) bar0 + 0x2700));
	}

	sp->device_enabled_once = FALSE;
}

/**
 *  s2io_set_swapper - to set the swapper controle on the card 
 *  @sp : private member of the device structure, 
 *  pointer to the s2io_nic structure.
 *  Description: Function to set the swapper control on the card 
 *  correctly depending on the 'endianness' of the system.
 *  Return value:
 *  SUCCESS on success and FAILURE on failure.
 */

int s2io_set_swapper(nic_t * sp)
{
	struct net_device *dev = sp->dev;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64;

/*  Set proper endian settings and verify the same by reading the PIF 
 *  Feed-back register.
 */
#ifdef  __BIG_ENDIAN
/* The device by default set to a big endian format, so a big endian 
 * driver need not set anything.
 */
	writeq(0xffffffffffffffffULL, &bar0->swapper_ctrl);
	val64 = (SWAPPER_CTRL_PIF_R_FE |
		 SWAPPER_CTRL_PIF_R_SE |
		 SWAPPER_CTRL_PIF_W_FE |
		 SWAPPER_CTRL_PIF_W_SE |
		 SWAPPER_CTRL_TXP_FE |
		 SWAPPER_CTRL_TXP_SE |
		 SWAPPER_CTRL_TXD_R_FE |
		 SWAPPER_CTRL_TXD_W_FE |
		 SWAPPER_CTRL_TXF_R_FE |
		 SWAPPER_CTRL_RXD_R_FE |
		 SWAPPER_CTRL_RXD_W_FE |
		 SWAPPER_CTRL_RXF_W_FE |
		 SWAPPER_CTRL_XMSI_FE |
		 SWAPPER_CTRL_XMSI_SE |
		 SWAPPER_CTRL_STATS_FE | SWAPPER_CTRL_STATS_SE);
	writeq(val64, &bar0->swapper_ctrl);
#else
/* Initially we enable all bits to make it accessible by the driver,
 * then we selectively enable only those bits that we want to set.
 */
	writeq(0xffffffffffffffffULL, &bar0->swapper_ctrl);
	val64 = (SWAPPER_CTRL_PIF_R_FE |
		 SWAPPER_CTRL_PIF_R_SE |
		 SWAPPER_CTRL_PIF_W_FE |
		 SWAPPER_CTRL_PIF_W_SE |
		 SWAPPER_CTRL_TXP_FE |
		 SWAPPER_CTRL_TXP_SE |
		 SWAPPER_CTRL_TXD_R_FE |
		 SWAPPER_CTRL_TXD_R_SE |
		 SWAPPER_CTRL_TXD_W_FE |
		 SWAPPER_CTRL_TXD_W_SE |
		 SWAPPER_CTRL_TXF_R_FE |
		 SWAPPER_CTRL_RXD_R_FE |
		 SWAPPER_CTRL_RXD_R_SE |
		 SWAPPER_CTRL_RXD_W_FE |
		 SWAPPER_CTRL_RXD_W_SE |
		 SWAPPER_CTRL_RXF_W_FE |
		 SWAPPER_CTRL_XMSI_FE |
		 SWAPPER_CTRL_XMSI_SE |
		 SWAPPER_CTRL_STATS_FE | SWAPPER_CTRL_STATS_SE);
	writeq(val64, &bar0->swapper_ctrl);
#endif

/*  Verifying if endian settings are accurate by reading a feedback
 *  register.
 */
	val64 = readq(&bar0->pif_rd_swapper_fb);
	if (val64 != 0x0123456789ABCDEFULL) {
		/* Endian settings are incorrect, calls for another dekko. */
		DBG_PRINT(ERR_DBG, "%s: Endian settings are wrong, ",
			  dev->name);
		DBG_PRINT(ERR_DBG, "feedback read %llx\n",
			  (unsigned long long) val64);
		return FAILURE;
	}

	return SUCCESS;
}

/* ********************************************************* *
 * Functions defined below concern the OS part of the driver *
 * ********************************************************* */

/**  
 *  s2io-open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver. It mainly calls a
 *  function to allocate Rx buffers and inserts them into the buffer
 *  descriptors and then enables the Rx part of the NIC. 
 *  Return value:
 *  '0' on success and an appropriate (-)ve integer as defined in errno.h
 *   file on failure.
*/

int s2io_open(struct net_device *dev)
{
	nic_t *sp = dev->priv;
	int i, ret = 0, err = 0;
	struct config_param *config;
#ifdef TXDBD
#ifdef MAC
	mac_info_tx_t *mac_control_tx;
#else
	mac_info_t *mac_control;
#endif
#endif

/* Make sure you have link off by default every time Nic is initialized*/
	netif_carrier_off(dev);
	sp->last_link_state = LINK_DOWN;

/*  Initialize the H/W I/O registers */
	if (init_nic(sp) != 0) {
		DBG_PRINT(ERR_DBG, "%s: H/W initialization failed\n",
			  dev->name);
		return -ENODEV;
	}

/*  After proper initialization of H/W, register ISR */
	err =
	    request_irq((int) sp->irq, s2io_isr, SA_SHIRQ, sp->name, dev);
	if (err) {
		s2io_reset(sp);
		DBG_PRINT(ERR_DBG, "%s: ISR registration failed\n",
			  dev->name);
		return err;
	}
	if (s2io_set_mac_addr(dev, dev->dev_addr) == FAILURE) {
		DBG_PRINT(ERR_DBG, "Set Mac Address Failed\n");
		s2io_reset(sp);
		return -ENODEV;
	}


	config = &sp->config;

/* Initialise tx pointers */
#ifdef TXDBD
#ifdef MAC
	mac_control_tx = &sp->mac_control_tx;
	for (i = 0; i < config->TxFIFONum; i++) {
	    mac_control_tx->tx_curr_put_info[i].block_index = 0;
	    mac_control_tx->tx_curr_put_info[i].offset = 0;
	    mac_control_tx->tx_curr_get_info[i].block_index = 0;
	    mac_control_tx->tx_curr_get_info[i].offset = 0;
	}
#else
	mac_control = &sp->mac_control;
	for (i = 0; i < config->TxFIFONum; i++) {
	    mac_control->tx_curr_put_info[i].block_index = 0;
	    mac_control->tx_curr_put_info[i].offset = 0;
	    mac_control->tx_curr_get_info[i].block_index = 0;
	    mac_control->tx_curr_get_info[i].offset = 0;
	}
#endif
#endif
/*  Setting its receive mode */
	s2io_set_multicast(dev);

/*  Initializing the Rx buffers. For now we are considering only 1 Rx ring
 * and initializing buffers into 1016 RxDs or 8 Rx blocks
 */
	for (i = 0; i < config->RxRingNum; i++) {
		if ((ret = fill_rx_buffers(sp, i))) {
			DBG_PRINT(ERR_DBG, "%s: Out of memory in Open\n",
				  dev->name);
			s2io_reset(sp);
			free_irq(dev->irq, dev);
			free_rx_buffers(sp);
			return -ENOMEM;
		}
		DBG_PRINT(INFO_DBG, "Buf in ring:%d is %d:\n", i,
			  atomic_read(&sp->rx_bufs_left[i]));
	}

/*  Enable tasklet for the device */
	tasklet_init(&sp->task, s2io_tasklet, (unsigned long) dev);

/*  Enable Rx Traffic and interrupts on the NIC */
	if (start_nic(sp)) {
		DBG_PRINT(ERR_DBG, "%s: Starting NIC failed\n", dev->name);
		tasklet_kill(&sp->task);
		s2io_reset(sp);
		free_irq(dev->irq, dev);
		free_rx_buffers(sp);
		return -ENODEV;
	}

	sp->device_close_flag = FALSE;	/* Device is up and running. */
	netif_start_queue(dev);

	return 0;
}

/**
 *  s2io_close -close entry point of the driver
 *  @dev : device pointer.
 *  Description:
 *  This is the stop entry point of the driver. It needs to undo exactly
 *  whatever was done by the open entry point,thus it's usually referred to
 *  as the close function.Among other things this function mainly stops the
 *  Rx side of the NIC and frees all the Rx buffers in the Rx rings.
 *  Return value:
 *  '0' on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
*/

int s2io_close(struct net_device *dev)
{
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	register u64 val64 = 0;
	u16 cnt = 0;
	unsigned long flags;

#if (!LATEST_CHANGES)
	spin_lock_irqsave(&sp->isr_lock, flags);
#endif
#if LATEST_CHANGES
	spin_lock_irqsave(&sp->tx_lock, flags);
#endif
	netif_stop_queue(dev);

/* disable Tx and Rx traffic on the NIC */
	stop_nic(sp);
#if (!LATEST_CHANGES)
	spin_unlock_irqrestore(&sp->isr_lock, flags);
#endif

/* If the device tasklet is running, wait till its done before killing it */
	while (atomic_read(&(sp->tasklet_status))) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 10);
	}
	tasklet_kill(&sp->task);

	/*  Free the Registered IRQ */
	free_irq(dev->irq, dev);

	/* Flush all scheduled tasks */
#if LATEST_CHANGES
	if (sp->task_flag == 1) {
		DBG_PRINT(INFO_DBG,"%s: Calling close from task\n",
			dev->name);
	} else {
#endif
#ifdef INIT_TQUEUE
		flush_scheduled_tasks();
#else
		flush_scheduled_work();
#endif
#if LATEST_CHANGES
	}
#endif

/* Check if the device is Quiescent and then Reset the NIC */
	do {
		val64 = readq(&bar0->adapter_status);
		if (verify_xena_quiescence(val64, sp->device_enabled_once)) {
			break;
		}

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
		cnt++;
		if (cnt == 10) {
			DBG_PRINT(ERR_DBG,
				  "s2io_close:Device not Quiescent ");
			DBG_PRINT(ERR_DBG, "adaper status reads 0x%llx\n",
				  (unsigned long long) val64);
			break;
		}
	} while (1);
	s2io_reset(sp);

/* Free all Tx Buffers waiting for transmission */
	free_tx_buffers(sp);

/*  Free all Rx buffers allocated by host */
	free_rx_buffers(sp);

	sp->device_close_flag = TRUE;	/* Device is shut down. */
#if LATEST_CHANGES
	spin_unlock_irqrestore(&sp->tx_lock, flags);
#endif
	return 0;
}

/**
 *  s2io_xmit - Tx entry point of te driver
 *  @skb : the socket buffer containing the Tx data.
 *  @dev : device pointer.
 *  Description :
 *  This function is the Tx entry point of the driver. S2IO NIC supports
 *  certain protocol assist features on Tx side, namely  CSO, S/G, LSO.
 *  NOTE: when device cant queue the pkt,just the trans_start variable will
 *  not be upadted.
 *  Return value:
 *  '0' on success & 1 on failure.
*/

int s2io_xmit(struct sk_buff *skb, struct net_device *dev)
{
	nic_t *sp = dev->priv;
	u16 off, txd_len, frg_cnt, frg_len, i, queue, off1;
	u16  queue_len;
	register u64 val64;
	TxD_t *txdp;
	TxFIFO_element_t *tx_fifo;
	unsigned long flags;
#ifdef NETIF_F_TSO
	int mss;
#endif
#ifdef TXDBD
    u32 offset = 0;
    u32 offset1 = 0;
    u16 block_no = 0;
    u16 block_no1 = 0;

#endif
#ifdef MAC
    mac_info_tx_t *mac_control_tx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;

	config = &sp->config;
#ifdef MAC
    mac_control_tx = &sp->mac_control_tx;
#else
	mac_control = &sp->mac_control;
#endif

	DBG_PRINT(TX_DBG, "%s: In S2IO Tx routine\n", dev->name);

	spin_lock_irqsave(&sp->tx_lock, flags);

#if LATEST_CHANGES
	if (netif_queue_stopped(dev)) {
		DBG_PRINT(ERR_DBG, "%s:s2io_xmit: Xmit queue stopped\n",
				dev->name);
		dev_kfree_skb(skb);
		spin_unlock_irqrestore(&sp->tx_lock, flags);
		return 0;
	}
#endif
	queue = 0;
	/* Multi FIFO Tx is disabled for now. */
	if (!queue && tx_prio) {
		u8 x = (skb->data)[5];
		queue = x % config->TxFIFONum;
	}
#ifdef TXDBD
#ifdef MAC
        off = (u16) mac_control_tx->tx_curr_put_info[queue].offset;
        off1 = (u16) mac_control_tx->tx_curr_get_info[queue].offset;
        txd_len = mac_control_tx->txdl_len;

        block_no = mac_control_tx->tx_curr_put_info[queue].block_index;
        block_no1 = mac_control_tx->tx_curr_get_info[queue].block_index;

        offset = block_no * (mac_control_tx->max_txds_per_block) + off ;
        offset1 = block_no1 * (mac_control_tx->max_txds_per_block) + off1;
	queue_len = mac_control_tx->tx_curr_put_info[queue].fifo_len + 1;
#else
        off = (u16) mac_control->tx_curr_put_info[queue].offset;
        off1 = (u16) mac_control->tx_curr_get_info[queue].offset;
        txd_len = mac_control->txdl_len;

        block_no = mac_control->tx_curr_put_info[queue].block_index;
        block_no1 = mac_control->tx_curr_get_info[queue].block_index;

        offset = block_no * (mac_control->max_txds_per_block) + off ;
        offset1 = block_no1 * (mac_control->max_txds_per_block) + off1;
	queue_len = mac_control->tx_curr_put_info[queue].fifo_len + 1;
#endif
        txdp = sp->tx_blocks[queue][block_no].block_virt_addr + off;
        if((txdp->Host_Control) && 
		//(((offset + config->MaxTxDs) % queue_len) == offset1))
		(offset == offset1))
        {
            DBG_PRINT(ERR_DBG, "Error in xmit, No free TXDs.\n");
                netif_stop_queue(dev);
            dev_kfree_skb(skb);
            spin_unlock_irqrestore(&sp->tx_lock, flags);
        return 0;
        }
#else
#ifdef MAC
    off = (u16) mac_control_tx->tx_curr_put_info[queue].offset;
    off1 = (u16) mac_control_tx->tx_curr_get_info[queue].offset;
    txd_len = mac_control_tx->txdl_len;
    txdp = mac_control_tx->txdl_start[queue] + (config->MaxTxDs * off);

    queue_len = mac_control_tx->tx_curr_put_info[queue].fifo_len + 1;

#else
	off = (u16) mac_control->tx_curr_put_info[queue].offset;
	off1 = (u16) mac_control->tx_curr_get_info[queue].offset;
	txd_len = mac_control->txdl_len;
	txdp = mac_control->txdl_start[queue] + (config->MaxTxDs * off);

	queue_len = mac_control->tx_curr_put_info[queue].fifo_len + 1;
#endif
	/* Avoid "put" pointer going beyond "get" pointer */
	if (txdp->Host_Control || (((off + 1) % queue_len) == off1)) {
		DBG_PRINT(ERR_DBG, "Error in xmit, No free TXDs.\n");
		netif_stop_queue(dev);
		dev_kfree_skb(skb);
		spin_unlock_irqrestore(&sp->tx_lock, flags);
		return 0;
	}
#endif
#ifdef NETIF_F_TSO
	mss = skb_shinfo(skb)->tso_size;
	if (mss) {
		txdp->Control_1 |= TXD_TCP_LSO_EN;
		txdp->Control_1 |= TXD_TCP_LSO_MSS(mss);
	}
#endif

	frg_cnt = skb_shinfo(skb)->nr_frags;
	frg_len = skb->len - skb->data_len;

	txdp->Host_Control = (unsigned long) skb;
	txdp->Buffer_Pointer = pci_map_single
	    (sp->pdev, skb->data, frg_len, PCI_DMA_TODEVICE);
	if (skb->ip_summed == CHECKSUM_HW) {
		txdp->Control_2 |=
		    (TXD_TX_CKO_IPV4_EN | TXD_TX_CKO_TCP_EN |
		     TXD_TX_CKO_UDP_EN);
	}

	txdp->Control_2 |= config->TxIntrType;

	txdp->Control_1 |= (TXD_BUFFER0_SIZE(frg_len) |
			    TXD_GATHER_CODE_FIRST);
	txdp->Control_1 |= TXD_LIST_OWN_XENA;

	/* For fragmented SKB. */
	for (i = 0; i < frg_cnt; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		txdp++;
		txdp->Buffer_Pointer = (u64) pci_map_page
		    (sp->pdev, frag->page, frag->page_offset,
		     frag->size, PCI_DMA_TODEVICE);
		txdp->Control_1 |= TXD_BUFFER0_SIZE(frag->size);
	}
	txdp->Control_1 |= TXD_GATHER_CODE_LAST;
#ifdef TXDBD
#ifdef MAC
    tx_fifo = mac_control_tx->tx_FIFO_start[queue];
    val64 = sp->tx_blocks[queue][block_no].block_dma_addr +
                           (sizeof(TxD_t) * off);
#else
    tx_fifo = mac_control->tx_FIFO_start[queue];
    val64 = sp->tx_blocks[queue][block_no].block_dma_addr +
                           (sizeof(TxD_t) * off);
#endif
#else
#ifdef MAC
	tx_fifo = mac_control_tx->tx_FIFO_start[queue];
	val64 = (mac_control_tx->txdl_start_phy[queue] +
		 (sizeof(TxD_t) * txd_len * off));
#else
	tx_fifo = mac_control->tx_FIFO_start[queue];
	val64 = (mac_control->txdl_start_phy[queue] +
		 (sizeof(TxD_t) * txd_len * off));
#endif
#endif
	writeq(val64, &tx_fifo->TxDL_Pointer);

	val64 = (TX_FIFO_LAST_TXD_NUM(frg_cnt) | TX_FIFO_FIRST_LIST |
		 TX_FIFO_LAST_LIST);
#ifdef NETIF_F_TSO
	if (mss)
		val64 |= TX_FIFO_SPECIAL_FUNC;
#endif
	/*
	 * according to the XENA spec:
	 *
	 * It is important to note that pointers and list control words are
	 * always written in pairs: in the first write, the host must write a
	 * pointer, and in the second write, it must write the list control
	 * word. Any other access will result in an error. Also, all 16 bytes
	 * of the pointer/control structure must be written, including any
	 * reserved bytes.
	 */
	wmb();
	writeq(val64, &tx_fifo->List_Control);

	/* Perform a PCI read to flush previous writes */
	val64 = readq(&bar0->general_int_status);
#ifdef TXDBD
     off += config->MaxTxDs;
#ifdef MAC
     if(off == mac_control_tx->max_txds_per_block)
       {
               mac_control_tx->tx_curr_put_info[queue].block_index ++;
               mac_control_tx->tx_curr_put_info[queue].block_index %=
                   sp->tx_block_count[queue];
               off = 0;
       }
       mac_control_tx->tx_curr_put_info[queue].offset = off;
#else
     if(off == mac_control->max_txds_per_block)
       {
               mac_control->tx_curr_put_info[queue].block_index ++;
               mac_control->tx_curr_put_info[queue].block_index %=
                   sp->tx_block_count[queue];
               off = 0;
       }
       mac_control->tx_curr_put_info[queue].offset = off;
#endif
/*    if (offset + 1 == offset1) {
        DBG_PRINT(TX_DBG,
          "No free TxDs for xmit, Put: 0x%x Get:0x%x\n",
          offset, offset1);
        netif_stop_queue(dev);
    }
*/

#else
	off++;
#ifdef MAC
	off %= mac_control_tx->tx_curr_put_info[queue].fifo_len + 1;
	mac_control_tx->tx_curr_put_info[queue].offset = off;
#else
	off %= mac_control->tx_curr_put_info[queue].fifo_len + 1;
	mac_control->tx_curr_put_info[queue].offset = off;
#endif
	/* Avoid "put" pointer going beyond "get" pointer */
	if (((off + 1) % queue_len) == off1) {
		DBG_PRINT(TX_DBG,
			  "No free TxDs for xmit, Put: 0x%x Get:0x%x\n",
			  off, off1);
		netif_stop_queue(dev);
	}
#endif
	dev->trans_start = jiffies;
	spin_unlock_irqrestore(&sp->tx_lock, flags);

	return 0;
}

/**
 *  s2io_isr - ISR handler of the device .
 *  @irq: the irq of the device.
 *  @dev_id: a void pointer to the dev structure of the NIC.
 *  @ptregs: pointer to the registers pushed on the stack.
 *  Description:  This function is the ISR handler of the device. It 
 *  identifies the reason for the interrupt and calls the relevant 
 *  service routines. As a contongency measure, this ISR allocates the 
 *  recv buffers, if their numbers are below the panic value which is
 *  presently set to 25% of the original number of rcv buffers allocated.
 *  Return value:
 *  void.
*/

static irqreturn_t s2io_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	int i, ret;
	u64 reason = 0, general_mask = 0;
	struct config_param *config;

	config = &sp->config;

#if (!LATEST_CHANGES)
	spin_lock(&sp->isr_lock);
#endif
	/* Identify the cause for interrupt and call the appropriate
	 * interrupt handler. Causes for the interrupt could be;
	 * 1. Rx of packet.
	 * 2. Tx complete.
	 * 3. Link down.
	 * 4. Error in any functional blocks of the NIC. 
	 */
	reason = readq(&bar0->general_int_status);

	if (!reason) {
		/* The interrupt was not raised by Xena. */
#if (!LATEST_CHANGES)
		spin_unlock(&sp->isr_lock);
#endif
		return IRQ_NONE;
	}
	/* Mask the interrupts on the NIC */
	general_mask = readq(&bar0->general_int_mask);
	writeq(0xFFFFFFFFFFFFFFFFULL, &bar0->general_int_mask);

#if DEBUG_ON
	sp->int_cnt++;
#endif

	/* If Intr is because of Tx Traffic */
	if (reason & GEN_INTR_TXTRAFFIC) {
		tx_intr_handler(sp);
	}

	/* If Intr is because of an error */
	if (reason & (GEN_ERROR_INTR))
		alarm_intr_handler(sp);

#ifdef CONFIG_S2IO_NAPI
	if (reason & GEN_INTR_RXTRAFFIC) {
		if (netif_rx_schedule_prep(dev)) {
			en_dis_able_nic_intrs(sp, RX_TRAFFIC_INTR,
					      DISABLE_INTRS);
			/* We retake the snap shot of the general interrupt 
			 * register.
			 */
			general_mask |= BIT(40);
			__netif_rx_schedule(dev);
		}
	}
#else
	/* If Intr is because of Rx Traffic */
	if (reason & GEN_INTR_RXTRAFFIC) {
		rx_intr_handler(sp);
	}
#endif

/* If the Rx buffer count is below the panic threshold then reallocate the
 * buffers from the interrupt handler itself, else schedule a tasklet to 
 * reallocate the buffers.
 */
#if 1
	for (i = 0; i < config->RxRingNum; i++) {
		int rxb_size = atomic_read(&sp->rx_bufs_left[i]);
		int level = rx_buffer_level(sp, rxb_size, i);

		if ((level == PANIC) && (!TASKLET_IN_USE)) {
			DBG_PRINT(ERR_DBG, "%s: Rx BD hit ", dev->name);
			DBG_PRINT(ERR_DBG, "PANIC levels\n");
			if ((ret = fill_rx_buffers(sp, i)) == -ENOMEM) {
				DBG_PRINT(ERR_DBG, "%s:Out of memory",
					  dev->name);
				DBG_PRINT(ERR_DBG, " in ISR!!\n");
				writeq(general_mask,
				       &bar0->general_int_mask);
#if (!LATEST_CHANGES)
				spin_unlock(&sp->isr_lock);
#endif
				return IRQ_HANDLED;
			}
			clear_bit(0,
				  (unsigned long *) (&sp->tasklet_status));
		} else if ((level == LOW)
			   && (!atomic_read(&sp->tasklet_status))) {
			tasklet_schedule(&sp->task);
		}

	}
#else
	tasklet_schedule(&sp->task);
#endif

	/* Unmask all the previously enabled interrupts on the NIC */
	writeq(general_mask, &bar0->general_int_mask);

#if (!LATEST_CHANGES)
	spin_unlock(&sp->isr_lock);
#endif
	return IRQ_HANDLED;
}

/**
 *  s2io_get_stats - Updates the device statistics structure. 
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function updates the device statistics structure in the s2io_nic 
 *  structure and returns a pointer to the same.
 *  Return value:
 *  pointer to the updated net_device_stats structure.
 */

struct net_device_stats *s2io_get_stats(struct net_device *dev)
{
	nic_t *sp = dev->priv;
#ifdef MAC
    mac_info_st_t *mac_control_st;

    mac_control_st = &sp->mac_control_st;

    sp->stats.tx_errors = mac_control_st->StatsInfo->tmac_any_err_frms;
    sp->stats.rx_errors = mac_control_st->StatsInfo->rmac_drop_frms;
    sp->stats.multicast = mac_control_st->StatsInfo->rmac_vld_mcst_frms;
    sp->stats.rx_length_errors =
        mac_control_st->StatsInfo->rmac_long_frms;
#else

	mac_info_t *mac_control;
	mac_control = &sp->mac_control;

	sp->stats.tx_errors = mac_control->StatsInfo->tmac_any_err_frms;
	sp->stats.rx_errors = mac_control->StatsInfo->rmac_drop_frms;
	sp->stats.multicast = mac_control->StatsInfo->rmac_vld_mcst_frms;
	sp->stats.rx_length_errors =
	    mac_control->StatsInfo->rmac_long_frms;
#endif
	return (&sp->stats);
}

/**
 *  s2io_set_multicast - entry point for multicast address enable/disable.
 *  @dev : pointer to the device structure
 *  Description:
 *  This function is a driver entry point which gets called by the kernel 
 *  whenever multicast addresses must be enabled/disabled. This also gets 
 *  called to set/reset promiscuous mode. Depending on the deivce flag, we
 *  determine, if multicast address must be enabled or if promiscuous mode
 *  is to be disabled etc.
 *  Return value:
 *  void.
 */

static void s2io_set_multicast(struct net_device *dev)
{
	int i, j, prev_cnt;
	struct dev_mc_list *mclist;
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64 = 0, multi_mac = 0x010203040506ULL, mask =
	    0xfeffffffffffULL;
	u64 dis_addr = 0xffffffffffffULL, mac_addr = 0;
	void *add;

	if ((dev->flags & IFF_ALLMULTI) && (!sp->m_cast_flg)) {
		/*  Enable all Multicast addresses */
		writeq(RMAC_ADDR_DATA0_MEM_ADDR(multi_mac),
		       &bar0->rmac_addr_data0_mem);
		writeq(RMAC_ADDR_DATA1_MEM_MASK(mask),
		       &bar0->rmac_addr_data1_mem);
		val64 = RMAC_ADDR_CMD_MEM_WE |
		    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
		    RMAC_ADDR_CMD_MEM_OFFSET(MAC_MC_ALL_MC_ADDR_OFFSET);
		writeq(val64, &bar0->rmac_addr_cmd_mem);
		/* Wait till command completes */
		wait_for_cmd_complete(sp);

		sp->m_cast_flg = 1;
		sp->all_multi_pos = MAC_MC_ALL_MC_ADDR_OFFSET;
	} else if ((dev->flags & IFF_ALLMULTI) && (sp->m_cast_flg)) {
		/*  Disable all Multicast addresses */
		writeq(RMAC_ADDR_DATA0_MEM_ADDR(dis_addr),
		       &bar0->rmac_addr_data0_mem);
		val64 = RMAC_ADDR_CMD_MEM_WE |
		    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
		    RMAC_ADDR_CMD_MEM_OFFSET(sp->all_multi_pos);
		writeq(val64, &bar0->rmac_addr_cmd_mem);
		/* Wait till command completes */
		wait_for_cmd_complete(sp);

		sp->m_cast_flg = 0;
		sp->all_multi_pos = 0;
	}

	if ((dev->flags & IFF_PROMISC) && (!sp->promisc_flg)) {
		/*  Put the NIC into promiscuous mode */
		add = (void *) &bar0->mac_cfg;
		val64 = readq(&bar0->mac_cfg);
		val64 |= MAC_CFG_RMAC_PROM_ENABLE;

		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) val64, add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));

		val64 = readq(&bar0->mac_cfg);
		sp->promisc_flg = 1;
		DBG_PRINT(ERR_DBG, "%s: entered promiscuous mode\n",
			  dev->name);
	} else if (!(dev->flags & IFF_PROMISC) && (sp->promisc_flg)) {
		/*  Remove the NIC from promiscuous mode */
		add = (void *) &bar0->mac_cfg;
		val64 = readq(&bar0->mac_cfg);
		val64 &= ~MAC_CFG_RMAC_PROM_ENABLE;

		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) val64, add);
		writeq(RMAC_CFG_KEY(0x4C0D), &bar0->rmac_cfg_key);
		writel((u32) (val64 >> 32), (add + 4));

		val64 = readq(&bar0->mac_cfg);
		sp->promisc_flg = 0;
		DBG_PRINT(ERR_DBG, "%s: left promiscuous mode\n",
			  dev->name);
	}

	/*  Update individual M_CAST address list */
	if ((!sp->m_cast_flg) && dev->mc_count) {
		if (dev->mc_count >
		    (MAX_ADDRS_SUPPORTED - MAC_MC_ADDR_START_OFFSET - 1)) {
			DBG_PRINT(ERR_DBG, "%s: No more Rx filters ",
				  dev->name);
			DBG_PRINT(ERR_DBG, "can be added, please enable ");
			DBG_PRINT(ERR_DBG, "ALL_MULTI instead\n");
			return;
		}

		prev_cnt = sp->mc_addr_count;
		sp->mc_addr_count = dev->mc_count;

		/* Clear out the previous list of Mc in the H/W. */
		for (i = 0; i < prev_cnt; i++) {
			writeq(RMAC_ADDR_DATA0_MEM_ADDR(dis_addr),
			       &bar0->rmac_addr_data0_mem);
			val64 = RMAC_ADDR_CMD_MEM_WE |
			    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
			    RMAC_ADDR_CMD_MEM_OFFSET
			    (MAC_MC_ADDR_START_OFFSET + i);
			writeq(val64, &bar0->rmac_addr_cmd_mem);

			/* Wait for command completes */
			if (wait_for_cmd_complete(sp)) {
				DBG_PRINT(ERR_DBG, "%s: Adding ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Multicasts failed\n");
				return;
			}
		}

		/* Create the new Rx filter list and update the same in H/W. */
		for (i = 0, mclist = dev->mc_list; i < dev->mc_count;
		     i++, mclist = mclist->next) {
			memcpy(sp->usr_addrs[i].addr, mclist->dmi_addr,
			       ETH_ALEN);
			for (j = 0; j < ETH_ALEN; j++) {
				mac_addr |= mclist->dmi_addr[j];
				mac_addr <<= 8;
			}
			writeq(RMAC_ADDR_DATA0_MEM_ADDR(mac_addr),
			       &bar0->rmac_addr_data0_mem);

			val64 = RMAC_ADDR_CMD_MEM_WE |
			    RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
			    RMAC_ADDR_CMD_MEM_OFFSET
			    (i + MAC_MC_ADDR_START_OFFSET);
			writeq(val64, &bar0->rmac_addr_cmd_mem);

			/* Wait for command completes */
			if (wait_for_cmd_complete(sp)) {
				DBG_PRINT(ERR_DBG, "%s: Adding ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "Multicasts failed\n");
				return;
			}
		}
	}
}

/**
 *  s2io_set_mac_address - Programs the Xframe mac address 
 *  @dev : pointer to the device structure.
 *  @new_mac : a uchar pointer to the new mac address which is to be set.
 *  Description : This procedure will program the Xframe to receive 
 *  frames with new Mac Address
 *  Return value: SUCCESS on success and an appropriate (-)ve integer 
 *  as defined in errno.h file on failure.
 */

int s2io_set_mac_addr(struct net_device *dev, u8 * addr)
{
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	register u64 val64, mac_addr = 0;
	int i;

	/* 
	 * Set the new MAC address as the new unicast filter and reflect this
	 * change on the device address registered with the OS. It will be
	 * at offset 0. 
	 */
	for (i = 0; i < ETH_ALEN; i++) {
		mac_addr <<= 8;
		mac_addr |= addr[i];
	}

	writeq(RMAC_ADDR_DATA0_MEM_ADDR(mac_addr),
	       &bar0->rmac_addr_data0_mem);

	val64 =
	    RMAC_ADDR_CMD_MEM_WE | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
	    RMAC_ADDR_CMD_MEM_OFFSET(0);
	writeq(val64, &bar0->rmac_addr_cmd_mem);
	/* Wait till command completes */
	if (wait_for_cmd_complete(sp)) {
		DBG_PRINT(ERR_DBG, "%s: set_mac_addr failed\n", dev->name);
		return FAILURE;
	}

	return SUCCESS;
}

/**
 * s2io_ethtool_sset - Sets different link parameters. 
 * @sp : private member of the device structure, which is a pointer to the  * s2io_nic structure.
 * @info: pointer to the structure with parameters given by ethtool to set
 * link information.
 * Description:
 * The function sets different link parameters provided by the user onto 
 * the NIC.
 * Return value:
 * 0 on success.
*/

#ifndef SET_ETHTOOL_OPS
#define SPEED_10000 10000
#endif
static int s2io_ethtool_sset(struct net_device *dev,
			     struct ethtool_cmd *info)
{
	nic_t *sp = dev->priv;
	if ((info->autoneg == AUTONEG_ENABLE) ||
	    (info->speed != SPEED_10000) || (info->duplex != DUPLEX_FULL))
		return -EINVAL;
	else {
		s2io_close(sp->dev);
		s2io_open(sp->dev);
	}

	return 0;
}

/**
 * s2io_ethtol_gset - Return link specific information. 
 * @sp : private member of the device structure, pointer to the
 *      s2io_nic structure.
 * @info : pointer to the structure with parameters given by ethtool
 * to return link information.
 * Description:
 * Returns link specefic information like speed, duplex etc.. to ethtool.
 * Return value :
 * void.
 */

int s2io_ethtool_gset(struct net_device *dev, struct ethtool_cmd *info)
{
	nic_t *sp = dev->priv;
	info->supported = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	info->advertising = (SUPPORTED_10000baseT_Full | SUPPORTED_FIBRE);
	info->port = PORT_FIBRE;
	/* info->transceiver?? TODO */

	if (netif_carrier_ok(sp->dev)) {
		info->speed = 10000;
		info->duplex = DUPLEX_FULL;
	} else {
		info->speed = -1;
		info->duplex = -1;
	}

	info->autoneg = AUTONEG_DISABLE;
	return 0;
}

/**
 * s2io_ethtool_gdrvinfo - Returns driver specific information. 
 * @sp : private member of the device structure, which is a pointer to the 
 * s2io_nic structure.
 * @info : pointer to the structure with parameters given by ethtool to
 * return driver information.
 * Description:
 * Returns driver specefic information like name, version etc.. to ethtool.
 * Return value:
 *  void
 */

static void s2io_ethtool_gdrvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *info)
{
	nic_t *sp = dev->priv;

	strncpy(info->driver, s2io_driver_name, sizeof(s2io_driver_name));
	strncpy(info->version, s2io_driver_version,
		sizeof(s2io_driver_version));
	strncpy(info->fw_version, "", 32);
	strncpy(info->bus_info, sp->pdev->slot_name, 32);
	info->regdump_len = XENA_REG_SPACE;
	info->eedump_len = XENA_EEPROM_SPACE;
	info->testinfo_len = S2IO_TEST_LEN;
#ifdef ETHTOOL_GSTATS
	info->n_stats = S2IO_STAT_LEN;
#endif
}

/**
 *  s2io_ethtool_gregs - dumps the entire space of Xfame into te buffer.
 *  @sp: private member of the device structure, which is a pointer to the 
 *  s2io_nic structure.
 *  @regs : pointer to the structure with parameters given by ethtool for 
 *  dumping the registers.
 *  @reg_space: The input argumnet into which all the registers are dumped.
 *  Description:
 *  Dumps the entire register space of xFrame NIC into the user given
 *  buffer area.
 * Return value :
 * void .
*/

static void s2io_ethtool_gregs(struct net_device *dev,
			       struct ethtool_regs *regs, void *space)
{
	int i;
	u64 reg;
	u8 *reg_space = (u8 *) space;
	nic_t *sp = dev->priv;

	regs->len = XENA_REG_SPACE;
	regs->version = sp->pdev->subsystem_device;

	for (i = 0; i < regs->len; i += 8) {
		reg = readq((void *) (sp->bar0 + i));
		memcpy((reg_space + i), &reg, 8);
	}
}

/**
 *  s2io_phy_id  - timer function that alternates adapter LED.
 *  @data : address of the private member of the device structure, which 
 *  is a pointer to the s2io_nic structure, provided as an u32.
 * Description: This is actually the timer function that alternates the 
 * adapter LED bit of the adapter control bit to set/reset every time on 
 * invocation. The timer is set for 1/2 a second, hence tha NIC blinks 
 *  once every second.
*/
static void s2io_phy_id(unsigned long data)
{
	nic_t *sp = (nic_t *) data;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64 = 0;
	u16 subid;

	subid = sp->pdev->subsystem_device;
	if ((subid & 0xFF) >= 0x07) {
		val64 = readq(&bar0->gpio_control);
		val64 ^= GPIO_CTRL_GPIO_0;
		writeq(val64, &bar0->gpio_control);
	} else {
		val64 = readq(&bar0->adapter_control);
		val64 ^= ADAPTER_LED_ON;
		writeq(val64, &bar0->adapter_control);
	}

	mod_timer(&sp->id_timer, jiffies + HZ / 2);
}

/**
 * s2io_ethtool_idnic - To physically ientify the nic on the system.
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @id : pointer to the structure with identification parameters given by 
 * ethtool.
 * Description: Used to physically identify the NIC on the system.
 * The Link LED will blink for a time specified by the user for 
 * identification.
 * NOTE: The Link has to be Up to be able to blink the LED. Hence 
 * identification is possible only if it's link is up.
 * Return value:
 * int , returns '0' on success
 */

static int s2io_ethtool_idnic(struct net_device *dev, u32 data)
{
	u64 val64 = 0, last_gpio_ctrl_val;
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u16 subid;

	subid = sp->pdev->subsystem_device;
	last_gpio_ctrl_val = readq(&bar0->gpio_control);
	if ((subid & 0xFF) < 0x07) {
		val64 = readq(&bar0->adapter_control);
		if (!(val64 & ADAPTER_CNTL_EN)) {
			printk(KERN_ERR
			       "Adapter Link down, cannot blink LED\n");
			return -EFAULT;
		}
	}
	if (sp->id_timer.function == NULL) {
		init_timer(&sp->id_timer);
		sp->id_timer.function = s2io_phy_id;
		sp->id_timer.data = (unsigned long) sp;
	}
	mod_timer(&sp->id_timer, jiffies);
	set_current_state(TASK_INTERRUPTIBLE);
	if (data)
		schedule_timeout(data * HZ);
	else
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	del_timer_sync(&sp->id_timer);

	if(CARDS_WITH_FAULTY_LINK_INDICATORS(subid)) {
		writeq(last_gpio_ctrl_val, &bar0->gpio_control);
		last_gpio_ctrl_val = readq(&bar0->gpio_control);
	}

	return 0;
}

/**
 * s2io_ethtool_getpause_data -Pause frame frame generation and reception.
 * @sp : private member of the device structure, which is a pointer to the  * s2io_nic structure.
 * @ep : pointer to the structure with pause parameters given by ethtool.
 * Description:
 * Returns the Pause frame generation and reception capability of the NIC.
 * Return value:
 *  void
*/
static void s2io_ethtool_getpause_data(struct net_device *dev,
				       struct ethtool_pauseparam *ep)
{
	u64 val64;
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;

	val64 = readq(&bar0->rmac_pause_cfg);
	if (val64 & RMAC_PAUSE_GEN_ENABLE)
		ep->tx_pause = TRUE;
	if (val64 & RMAC_PAUSE_RX_ENABLE)
		ep->rx_pause = TRUE;
	ep->autoneg = FALSE;
}

/**
 * s2io_ethtool-setpause_data -  set/reset pause frame generation.
 * @sp : private member of the device structure, which is a pointer to the 
 *      s2io_nic structure.
 * @ep : pointer to the structure with pause parameters given by ethtool.
 * Description:
 * It can be used to set or reset Pause frame generation or reception
 * support of the NIC.
 * Return value:
 * int, returns '0' on Success
*/

int s2io_ethtool_setpause_data(struct net_device *dev,
			       struct ethtool_pauseparam *ep)
{
	u64 val64;
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;

	val64 = readq(&bar0->rmac_pause_cfg);
	if (ep->tx_pause)
		val64 |= RMAC_PAUSE_GEN_ENABLE;
	else
		val64 &= ~RMAC_PAUSE_GEN_ENABLE;
	if (ep->rx_pause)
		val64 |= RMAC_PAUSE_RX_ENABLE;
	else
		val64 &= ~RMAC_PAUSE_RX_ENABLE;
	writeq(val64, &bar0->rmac_pause_cfg);
	return 0;
}

/**
 * read_eeprom - reads 4 bytes of data from user given offset.
 * @sp : private member of the device structure, which is a pointer to the 
 *      s2io_nic structure.
 * @off : offset at which the data must be written
 * Description:
 * Will read 4 bytes of data from the user given offset and return the 
 * read data.
 * NOTE: Will allow to read only part of the EEPROM visible through the
 *   I2C bus.
 * Return value:
 *  -1 on failure and the value read from the Eeprom if successful.
*/

#define S2IO_DEV_ID		5
static u32 read_eeprom(nic_t * sp, int off)
{
	u32 data = -1, exit_cnt = 0;
	u64 val64;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;

	val64 = I2C_CONTROL_DEV_ID(S2IO_DEV_ID) | I2C_CONTROL_ADDR(off) |
	    I2C_CONTROL_BYTE_CNT(0x3) | I2C_CONTROL_READ |
	    I2C_CONTROL_CNTL_START;
	writeq(val64, &bar0->i2c_control);

	while (exit_cnt < 5) {
		val64 = readq(&bar0->i2c_control);
		if (I2C_CONTROL_CNTL_END(val64)) {
			data = I2C_CONTROL_GET_DATA(val64);
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
		exit_cnt++;
	}

	return data;
}

/**
 *  write_eeprom - actually writes the relevant part of the data value.
 *  @sp : private member of the device structure, which is a pointer to the
 *       s2io_nic structure.
 *  @off : offset at which the data must be written
 *  @data : The data that is to be written
 *  @cnt : Number of bytes of the data that are actually to be written into 
 *  the Eeprom. (max of 3)
 * Description:
 *  Actually writes the relevant part of the data value into the Eeprom
 *  through the I2C bus.
 * Return value:
 *  '0' on success, -1 on failure.
*/

static int write_eeprom(nic_t * sp, int off, u32 data, int cnt)
{
	int exit_cnt = 0, ret = -1;
	u64 val64;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;

	val64 = I2C_CONTROL_DEV_ID(S2IO_DEV_ID) | I2C_CONTROL_ADDR(off) |
	    I2C_CONTROL_BYTE_CNT(cnt) | I2C_CONTROL_SET_DATA(data) |
	    I2C_CONTROL_CNTL_START;
	writeq(val64, &bar0->i2c_control);

	while (exit_cnt < 5) {
		val64 = readq(&bar0->i2c_control);
		if (I2C_CONTROL_CNTL_END(val64)) {
			if (!(val64 & I2C_CONTROL_NACK))
				ret = 0;
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 20);
		exit_cnt++;
	}

	return ret;
}

/**
 *  s2io_ethtool_geeprom  - reads the value stored in the Eeprom.
 *  @sp : private member of the device structure, which is a pointer to the *       s2io_nic structure.
 *  @eeprom : pointer to the user level structure provided by ethtool, 
 *  containing all relevant information.
 *  @data_buf : user defined value to be written into Eeprom.
 *  Description: Reads the values stored in the Eeprom at given offset
 *  for a given length. Stores these values int the input argument data
 *  buffer 'data_buf' and returns these to the caller (ethtool.)
 *  Return value:
 *  int  '0' on success
*/

int s2io_ethtool_geeprom(struct net_device *dev,
			 struct ethtool_eeprom *eeprom, u8 * data_buf)
{
	u32 data, i, valid;
	nic_t *sp = dev->priv;

	eeprom->magic = sp->pdev->vendor | (sp->pdev->device << 16);

	if ((eeprom->offset + eeprom->len) > (XENA_EEPROM_SPACE))
		eeprom->len = XENA_EEPROM_SPACE - eeprom->offset;

	for (i = 0; i < eeprom->len; i += 4) {
		data = read_eeprom(sp, eeprom->offset + i);
		if (data < 0) {
			DBG_PRINT(ERR_DBG, "Read of EEPROM failed\n");
			return -EFAULT;
		}
		valid = INV(data);
		memcpy((data_buf + i), &valid, 4);
	}
	return 0;
}

/**
 *  s2io_ethtool_seeprom - tries to write the user provided value in Eeprom
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @eeprom : pointer to the user level structure provided by ethtool, 
 *  containing all relevant information.
 *  @data_buf ; user defined value to be written into Eeprom.
 *  Description:
 *  Tries to write the user provided value in the Eeprom, at the offset
 *  given by the user.
 *  Return value:
 *  '0' on success, -EFAULT on failure.
*/

static int s2io_ethtool_seeprom(struct net_device *dev,
				struct ethtool_eeprom *eeprom,
				u8 * data_buf)
{
	int len = eeprom->len, cnt = 0;
	u32 valid = 0, data;
	nic_t *sp = dev->priv;

	if (eeprom->magic != (sp->pdev->vendor | (sp->pdev->device << 16))) {
		DBG_PRINT(ERR_DBG,
			  "ETHTOOL_WRITE_EEPROM Err: Magic value ");
		DBG_PRINT(ERR_DBG, "is wrong, Its not 0x%x\n",
			  eeprom->magic);
		return -EFAULT;
	}

	while (len) {
		data = (u32) data_buf[cnt] & 0x000000FF;
		if (data) {
			valid = (u32) (data << 24);
		} else
			valid = data;

		if (write_eeprom(sp, (eeprom->offset + cnt), valid, 0)) {
			DBG_PRINT(ERR_DBG,
				  "ETHTOOL_WRITE_EEPROM Err: Cannot ");
			DBG_PRINT(ERR_DBG,
				  "write into the specified offset\n");
			return -EFAULT;
		}
		cnt++;
		len--;
	}

	return 0;
}

/**
 * s2io_register_test - reads and writes into all clock domains. 
 * @sp : private member of the device structure, which is a pointer to the 
 * s2io_nic structure.
 * @data : variable that returns the result of each of the test conducted b
 * by the driver.
 * Description:
 * Read and write into all clock domains. The NIC has 3 clock domains,
 * see that registers in all the three regions are accessible.
 * Return value:
 * '0' on success.
*/

static int s2io_register_test(nic_t * sp, uint64_t * data)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64 = 0;
	int fail = 0;

	val64 = readq(&bar0->pcc_enable);
	if (val64 != 0xff00000000000000ULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 1 fails\n");
	}

	val64 = readq(&bar0->rmac_pause_cfg);
	if (val64 != 0xc000ffff00000000ULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 2 fails\n");
	}

	val64 = readq(&bar0->rx_queue_cfg);
	if (val64 != 0x0808080808080808ULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 3 fails\n");
	}

	val64 = readq(&bar0->xgxs_efifo_cfg);
	if (val64 != 0x000000001923141EULL) {
		fail = 1;
		DBG_PRINT(INFO_DBG, "Read Test level 4 fails\n");
	}

	val64 = 0x5A5A5A5A5A5A5A5AULL;
	writeq(val64, &bar0->xmsi_data);
	val64 = readq(&bar0->xmsi_data);
	if (val64 != 0x5A5A5A5A5A5A5A5AULL) {
		fail = 1;
		DBG_PRINT(ERR_DBG, "Write Test level 1 fails\n");
	}

	val64 = 0xA5A5A5A5A5A5A5A5ULL;
	writeq(val64, &bar0->xmsi_data);
	val64 = readq(&bar0->xmsi_data);
	if (val64 != 0xA5A5A5A5A5A5A5A5ULL) {
		fail = 1;
		DBG_PRINT(ERR_DBG, "Write Test level 2 fails\n");
	}

	*data = fail;
	return 0;
}

/**
 * s2io_eeprom_test - to verify that EEprom in the xena can be programmed. 
 * @sp : private member of the device structure, which is a pointer to the
 * s2io_nic structure.
 * @data:variable that returns the result of each of the test conducted by
 * the driver.
 * Description:
 * Verify that EEPROM in the xena can be programmed using I2C_CONTROL 
 * register.
 * Return value:
 * '0' on success.
*/

static int s2io_eeprom_test(nic_t * sp, uint64_t * data)
{
	int fail = 0, ret_data;

	/* Test Write Error at offset 0 */
	if (!write_eeprom(sp, 0, 0, 3))
		fail = 1;

	/* Test Write at offset 4f0 */
	if (write_eeprom(sp, 0x4F0, 0x01234567, 3))
		fail = 1;
	if ((ret_data = read_eeprom(sp, 0x4f0)) < 0)
		fail = 1;

	if (ret_data != 0x01234567)
		fail = 1;

	/* Reset the EEPROM data go FFFF */
	write_eeprom(sp, 0x4F0, 0xFFFFFFFF, 3);

	/* Test Write Request Error at offset 0x7c */
	if (!write_eeprom(sp, 0x07C, 0, 3))
		fail = 1;

	/* Test Write Request at offset 0x7fc */
	if (write_eeprom(sp, 0x7FC, 0x01234567, 3))
		fail = 1;
	if ((ret_data = read_eeprom(sp, 0x7FC)) < 0)
		fail = 1;

	if (ret_data != 0x01234567)
		fail = 1;

	/* Reset the EEPROM data go FFFF */
	write_eeprom(sp, 0x7FC, 0xFFFFFFFF, 3);

	/* Test Write Error at offset 0x80 */
	if (!write_eeprom(sp, 0x080, 0, 3))
		fail = 1;

	/* Test Write Error at offset 0xfc */
	if (!write_eeprom(sp, 0x0FC, 0, 3))
		fail = 1;

	/* Test Write Error at offset 0x100 */
	if (!write_eeprom(sp, 0x100, 0, 3))
		fail = 1;

	/* Test Write Error at offset 4ec */
	if (!write_eeprom(sp, 0x4EC, 0, 3))
		fail = 1;

	*data = fail;
	return 0;
}

/**
 * s2io_bist_test - invokes the MemBist test of the card .
 * @sp : private member of the device structure, which is a pointer to the 
 * s2io_nic structure.
 * @data:variable that returns the result of each of the test conducted by 
 * the driver.
 * Description:
 * This invokes the MemBist test of the card. We give around
 * 2 secs time for the Test to complete. If it's still not complete
 * within this peiod, we consider that the test failed. 
 * Return value:
 * '0' on success and -1 on failure.
*/

static int s2io_bist_test(nic_t * sp, uint64_t * data)
{
	u8 bist = 0;
	int cnt = 0, ret = -1;

	pci_read_config_byte(sp->pdev, PCI_BIST, &bist);
	bist |= PCI_BIST_START;
	pci_write_config_word(sp->pdev, PCI_BIST, bist);

	while (cnt < 20) {
		pci_read_config_byte(sp->pdev, PCI_BIST, &bist);
		if (!(bist & PCI_BIST_START)) {
			*data = (bist & PCI_BIST_CODE_MASK);
			ret = 0;
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		cnt++;
	}

	return ret;
}

/**
 * s2io-link_test - verifies the link state of the nic  
 * @sp ; private member of the device structure, which is a pointer to the 
 * s2io_nic structure.
 * @data: variable that returns the result of each of the test conducted by
 * the driver.
 * Description:
 * The function verifies the link state of the NIC and updates the input 
 * argument 'data' appropriately.
 * Return value:
 * '0' on success.
*/

static int s2io_link_test(nic_t * sp, uint64_t * data)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64;

	val64 = readq(&bar0->adapter_status);
	if (val64 & ADAPTER_STATUS_RMAC_LOCAL_FAULT)
		*data = 1;

	return 0;
}

/**
 * s2io_rldram_test - offline test for access to the RldRam chip on the NIC 
 * @sp - private member of the device structure, which is a pointer to the  * s2io_nic structure.
 * @data - variable that returns the result of each of the test 
 * conducted by the driver.
 * Description:
 *  This is one of the offline test that tests the read and write 
 *  access to the RldRam chip on the NIC.
 * Return value:
 *  '0' on success.
*/

static int s2io_rldram_test(nic_t * sp, uint64_t * data)
{
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	u64 val64;
	int cnt, iteration = 0, test_pass = 0;

	val64 = readq(&bar0->adapter_control);
	val64 &= ~ADAPTER_ECC_EN;
	writeq(val64, &bar0->adapter_control);

	val64 = readq(&bar0->mc_rldram_test_ctrl);
	val64 |= MC_RLDRAM_TEST_MODE;
	writeq(val64, &bar0->mc_rldram_test_ctrl);

	val64 = readq(&bar0->mc_rldram_mrs);
	val64 |= MC_RLDRAM_QUEUE_SIZE_ENABLE;
	writeq(val64, &bar0->mc_rldram_mrs);

	val64 |= MC_RLDRAM_MRS_ENABLE;
	writeq(val64, &bar0->mc_rldram_mrs);

	while (iteration < 2) {
		val64 = 0x55555555aaaa0000ULL;
		if (iteration == 1) {
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		}
		writeq(val64, &bar0->mc_rldram_test_d0);

		val64 = 0xaaaa5a5555550000ULL;
		if (iteration == 1) {
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		}
		writeq(val64, &bar0->mc_rldram_test_d1);

		val64 = 0x55aaaaaaaa5a0000ULL;
		if (iteration == 1) {
			val64 ^= 0xFFFFFFFFFFFF0000ULL;
		}
		writeq(val64, &bar0->mc_rldram_test_d2);

		val64 = (u64) (0x0000003fffff0000ULL);
		writeq(val64, &bar0->mc_rldram_test_add);


		val64 = MC_RLDRAM_TEST_MODE;
		writeq(val64, &bar0->mc_rldram_test_ctrl);

		val64 |=
		    MC_RLDRAM_TEST_MODE | MC_RLDRAM_TEST_WRITE |
		    MC_RLDRAM_TEST_GO;
		writeq(val64, &bar0->mc_rldram_test_ctrl);

		for (cnt = 0; cnt < 5; cnt++) {
			val64 = readq(&bar0->mc_rldram_test_ctrl);
			if (val64 & MC_RLDRAM_TEST_DONE)
				break;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ / 5);
		}

		if (cnt == 5)
			break;

		val64 = MC_RLDRAM_TEST_MODE;
		writeq(val64, &bar0->mc_rldram_test_ctrl);

		val64 |= MC_RLDRAM_TEST_MODE | MC_RLDRAM_TEST_GO;
		writeq(val64, &bar0->mc_rldram_test_ctrl);

		for (cnt = 0; cnt < 5; cnt++) {
			val64 = readq(&bar0->mc_rldram_test_ctrl);
			if (val64 & MC_RLDRAM_TEST_DONE)
				break;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ / 2);
		}

		if (cnt == 5)
			break;

		val64 = readq(&bar0->mc_rldram_test_ctrl);
		if (val64 & MC_RLDRAM_TEST_PASS)
			test_pass = 1;

		iteration++;
	}

	if (!test_pass)
		*data = 1;
	else
		*data = 0;

	return 0;
}

/**
 *  s2io_ethtool_test - conducts 6 tsets to determine the health of card.
 *  @sp : private member of the device structure, which is a pointer to the
 *  s2io_nic structure.
 *  @ethtest : pointer to a ethtool command specific structure that will be
 *  returned to the user.
 *  @data : variable that returns the result of each of the test 
 * conducted by the driver.
 * Description:
 *  This function conducts 6 tests ( 4 offline and 2 online) to determine
 *  the health of the card.
  * Return value:
 *  SUCCESS on success and an appropriate -1 on failure.
*/

static void s2io_ethtool_test(struct net_device *dev,
			      struct ethtool_test *ethtest,
			      uint64_t * data)
{
	nic_t *sp = dev->priv;
	int orig_state = netif_running(sp->dev);

	if (ethtest->flags == ETH_TEST_FL_OFFLINE) {
		/* Offline Tests. */
		if (orig_state) {
			s2io_close(sp->dev);
			s2io_set_swapper(sp);
		} else
			s2io_set_swapper(sp);

		if (s2io_register_test(sp, &data[0]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		s2io_reset(sp);
		s2io_set_swapper(sp);

		if (s2io_rldram_test(sp, &data[3]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		s2io_reset(sp);
		s2io_set_swapper(sp);

		if (s2io_eeprom_test(sp, &data[1]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		if (s2io_bist_test(sp, &data[4]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		if (orig_state)
			s2io_open(sp->dev);

		data[2] = 0;
	} else {
		/* Online Tests. */
		if (!orig_state) {
			DBG_PRINT(ERR_DBG,
				  "%s: is not up, cannot run test\n",
				  dev->name);
			data[0] = -1;
			data[1] = -1;
			data[2] = -1;
			data[3] = -1;
			data[4] = -1;
		}

		if (s2io_link_test(sp, &data[2]))
			ethtest->flags |= ETH_TEST_FL_FAILED;

		data[0] = 0;
		data[1] = 0;
		data[3] = 0;
		data[4] = 0;
	}
}

#ifdef ETHTOOL_GSTATS
static void s2io_get_ethtool_stats(struct net_device *dev,
				   struct ethtool_stats *estats,
				   u64 * tmp_stats)
{
	int i = 0;
	nic_t *sp = dev->priv;
#ifdef MAC
    StatInfo_t *stat_info = sp->mac_control_st.StatsInfo;
#else
	StatInfo_t *stat_info = sp->mac_control.StatsInfo;
#endif

	tmp_stats[i++] = stat_info->tmac_frms;
	tmp_stats[i++] = stat_info->tmac_data_octets;
	tmp_stats[i++] = stat_info->tmac_drop_frms;
	tmp_stats[i++] = stat_info->tmac_mcst_frms;
	tmp_stats[i++] = stat_info->tmac_bcst_frms;
	tmp_stats[i++] = stat_info->tmac_pause_ctrl_frms;
	tmp_stats[i++] = stat_info->tmac_any_err_frms;
	tmp_stats[i++] = stat_info->tmac_vld_ip_octets;
	tmp_stats[i++] = stat_info->tmac_vld_ip;
	tmp_stats[i++] = stat_info->tmac_drop_ip;
	tmp_stats[i++] = stat_info->tmac_icmp;
	tmp_stats[i++] = stat_info->tmac_rst_tcp;
	tmp_stats[i++] = stat_info->tmac_tcp;
	tmp_stats[i++] = stat_info->tmac_udp;
	tmp_stats[i++] = stat_info->rmac_vld_frms;
	tmp_stats[i++] = stat_info->rmac_data_octets;
	tmp_stats[i++] = stat_info->rmac_fcs_err_frms;
	tmp_stats[i++] = stat_info->rmac_drop_frms;
	tmp_stats[i++] = stat_info->rmac_vld_mcst_frms;
	tmp_stats[i++] = stat_info->rmac_vld_bcst_frms;
	tmp_stats[i++] = stat_info->rmac_in_rng_len_err_frms;
	tmp_stats[i++] = stat_info->rmac_long_frms;
	tmp_stats[i++] = stat_info->rmac_pause_ctrl_frms;
	tmp_stats[i++] = stat_info->rmac_discarded_frms;
	tmp_stats[i++] = stat_info->rmac_usized_frms;
	tmp_stats[i++] = stat_info->rmac_osized_frms;
	tmp_stats[i++] = stat_info->rmac_frag_frms;
	tmp_stats[i++] = stat_info->rmac_jabber_frms;
	tmp_stats[i++] = stat_info->rmac_ip;
	tmp_stats[i++] = stat_info->rmac_ip_octets;
	tmp_stats[i++] = stat_info->rmac_hdr_err_ip;
	tmp_stats[i++] = stat_info->rmac_drop_ip;
	tmp_stats[i++] = stat_info->rmac_icmp;
	tmp_stats[i++] = stat_info->rmac_tcp;
	tmp_stats[i++] = stat_info->rmac_udp;
	tmp_stats[i++] = stat_info->rmac_err_drp_udp;
	tmp_stats[i++] = stat_info->rmac_pause_cnt;
	tmp_stats[i++] = stat_info->rmac_accepted_ip;
	tmp_stats[i++] = stat_info->rmac_err_tcp;
}
#endif

#ifndef SET_ETHTOOL_OPS
/**
 * s2io_ethtool -to support all ethtool features .
 * @dev : device pointer.
 * @ifr :   An IOCTL specefic structure, that can contain a pointer to
 * a proprietary structure used to pass information to the driver.
 * Description:
 * Function used to support all ethtool fatures except dumping Device stats
 * as it can be obtained from the util tool for now.
 * Return value:
 * '0' on success and an appropriate (-)ve integer as defined in errno.h
 * file on failure.
*/

static int s2io_ethtool(struct net_device *dev, struct ifreq *rq)
{
	nic_t *sp = dev->priv;
	void *data = rq->ifr_data;
	u32 ecmd;

	if (get_user(ecmd, (u32 *) data)) {
		return -EFAULT;
	}

	switch (ecmd) {
	case ETHTOOL_GSET:
		{
			struct ethtool_cmd info = { ETHTOOL_GSET };
			s2io_ethtool_gset(dev, &info);
			if (copy_to_user(data, &info, sizeof(info)))
				return -EFAULT;
			break;
		}
	case ETHTOOL_SSET:
		{
			struct ethtool_cmd info;

			if (copy_from_user(&info, data, sizeof(info)))
				return -EFAULT;
			if (s2io_ethtool_sset(dev, &info))
				return -EFAULT;
			break;
		}
	case ETHTOOL_GDRVINFO:
		{
			struct ethtool_drvinfo info = { ETHTOOL_GDRVINFO };

			s2io_ethtool_gdrvinfo(dev, &info);
			if (copy_to_user(data, &info, sizeof(info)))
				return -EFAULT;
			break;
		}
	case ETHTOOL_GREGS:
		{
			struct ethtool_regs regs = { ETHTOOL_GREGS };
			u8 *reg_space;
			int ret = 0;

			regs.version = sp->pdev->subsystem_device;

			reg_space = kmalloc(XENA_REG_SPACE, GFP_KERNEL);
			if (reg_space == NULL) {
				DBG_PRINT(ERR_DBG,
					  "Memory allocation to dump ");
				DBG_PRINT(ERR_DBG, "registers failed\n");
				ret = -EFAULT;
			}
			memset(reg_space, 0, XENA_REG_SPACE);
			s2io_ethtool_gregs(dev, &regs, reg_space);
			if (copy_to_user(data, &regs, sizeof(regs))) {
				ret = -EFAULT;
				goto last_gregs;
			}
			data += offsetof(struct ethtool_regs, data);
			if (copy_to_user(data, reg_space, regs.len)) {
				ret = -EFAULT;
				goto last_gregs;
			}
		      last_gregs:
			kfree(reg_space);
			if (ret)
				return ret;
			break;
		}
	case ETHTOOL_GLINK:
		{
			struct ethtool_value link = { ETHTOOL_GLINK };

			link.data = netif_carrier_ok(dev);
			if (copy_to_user(data, &link, sizeof(link)))
				return -EFAULT;
			break;
		}
	case ETHTOOL_PHYS_ID:
		{
			struct ethtool_value id;

			if (copy_from_user(&id, data, sizeof(id)))
				return -EFAULT;
			s2io_ethtool_idnic(dev, id.data);
			break;
		}
	case ETHTOOL_GPAUSEPARAM:
		{
			struct ethtool_pauseparam ep =
			    { ETHTOOL_GPAUSEPARAM };

			s2io_ethtool_getpause_data(dev, &ep);
			if (copy_to_user(data, &ep, sizeof(ep)))
				return -EFAULT;
			break;

		}
	case ETHTOOL_SPAUSEPARAM:
		{
			struct ethtool_pauseparam ep;

			if (copy_from_user(&ep, data, sizeof(ep)))
				return -EFAULT;
			s2io_ethtool_setpause_data(dev, &ep);
			break;
		}
	case ETHTOOL_GRXCSUM:
		{
			struct ethtool_value ev = { ETHTOOL_GRXCSUM };

			ev.data = sp->rx_csum;
			if (copy_to_user(data, &ev, sizeof(ev)))
				return -EFAULT;
			break;
		}
	case ETHTOOL_GTXCSUM:
		{
			struct ethtool_value ev = { ETHTOOL_GTXCSUM };
			ev.data = (dev->features & NETIF_F_IP_CSUM);

			if (copy_to_user(data, &ev, sizeof(ev)))
				return -EFAULT;
			break;
		}
	case ETHTOOL_GSG:
		{
			struct ethtool_value ev = { ETHTOOL_GSG };
			ev.data = (dev->features & NETIF_F_SG);

			if (copy_to_user(data, &ev, sizeof(ev)))
				return -EFAULT;
			break;
		}
#ifdef NETIF_F_TSO
	case ETHTOOL_GTSO:
		{
			struct ethtool_value ev = { ETHTOOL_GTSO };
			ev.data = (dev->features & NETIF_F_TSO);

			if (copy_to_user(data, &ev, sizeof(ev)))
				return -EFAULT;
			break;
		}
#endif
	case ETHTOOL_STXCSUM:
		{
			struct ethtool_value ev;

			if (copy_from_user(&ev, data, sizeof(ev)))
				return -EFAULT;

			if (ev.data)
				dev->features |= NETIF_F_IP_CSUM;
			else
				dev->features &= ~NETIF_F_IP_CSUM;
			break;
		}
	case ETHTOOL_SRXCSUM:
		{
			struct ethtool_value ev;

			if (copy_from_user(&ev, data, sizeof(ev)))
				return -EFAULT;

			if (ev.data)
				sp->rx_csum = 1;
			else
				sp->rx_csum = 0;

			break;
		}
	case ETHTOOL_SSG:
		{
			struct ethtool_value ev;

			if (copy_from_user(&ev, data, sizeof(ev)))
				return -EFAULT;

			if (ev.data)
				dev->features |= NETIF_F_SG;
			else
				dev->features &= ~NETIF_F_SG;
			break;
		}
#ifdef NETIF_F_TSO
	case ETHTOOL_STSO:
		{
			struct ethtool_value ev;

			if (copy_from_user(&ev, data, sizeof(ev)))
				return -EFAULT;

			if (ev.data)
				dev->features |= NETIF_F_TSO;
			else
				dev->features &= ~NETIF_F_TSO;
			break;
		}
#endif
	case ETHTOOL_GEEPROM:
		{
			struct ethtool_eeprom eeprom = { ETHTOOL_GEEPROM };
			char *data_buf;
			int ret = 0;

			if (copy_from_user(&eeprom, data, sizeof(eeprom)))
				return -EFAULT;

			if (eeprom.len <= 0)
				return -EINVAL;

			if (!
			    (data_buf =
			     kmalloc(XENA_EEPROM_SPACE, GFP_KERNEL)))
				return -ENOMEM;
			s2io_ethtool_geeprom(dev, &eeprom, data_buf);

			if (copy_to_user(data, &eeprom, sizeof(eeprom))) {
				ret = -EFAULT;
				goto last_geprom;
			}

			data += offsetof(struct ethtool_eeprom, data);

			if (copy_to_user
			    (data, (void *) data_buf, eeprom.len)) {
				ret = -EFAULT;
				goto last_geprom;
			}

		      last_geprom:
			kfree(data_buf);
			if (ret)
				return ret;
			break;
		}
	case ETHTOOL_SEEPROM:
		{
			struct ethtool_eeprom eeprom;
			unsigned char *data_buf;
			void *ptr;
			int ret = 0;

			if (copy_from_user(&eeprom, data, sizeof(eeprom)))
				return -EFAULT;

			if (!(data_buf = kmalloc(eeprom.len, GFP_KERNEL)))
				return -ENOMEM;
			ptr = (void *) data_buf;

			data += offsetof(struct ethtool_eeprom, data);
			if (copy_from_user(ptr, data, eeprom.len)) {
				ret = -EFAULT;
				goto last_seprom;
			}

			if ((eeprom.offset + eeprom.len) >
			    (XENA_EEPROM_SPACE)) {
				DBG_PRINT(ERR_DBG, "%s Write ", dev->name);
				DBG_PRINT(ERR_DBG, "request overshoots ");
				DBG_PRINT(ERR_DBG, "the EEPROM area\n");
				ret = -EFAULT;
				goto last_seprom;
			}
			if (s2io_ethtool_seeprom(dev, &eeprom, data_buf)) {
				ret = -EFAULT;
				goto last_seprom;
			}

		      last_seprom:
			kfree(data_buf);
			if (ret)
				return ret;
			break;
		}
	case ETHTOOL_GSTRINGS:
		{
			struct ethtool_gstrings gstrings =
			    { ETHTOOL_GSTRINGS };
			char *strings = NULL;
			int ret = 0, mem_sz;

			if (copy_from_user
			    (&gstrings, data, sizeof(gstrings)))
				return -EFAULT;

			switch (gstrings.string_set) {
			case ETH_SS_TEST:
				gstrings.len = S2IO_TEST_LEN;
				mem_sz = S2IO_STRINGS_LEN;
				strings = kmalloc(mem_sz, GFP_KERNEL);
				if (!strings)
					return -ENOMEM;
				memcpy(strings, s2io_gstrings,
				       S2IO_STRINGS_LEN);
				break;
#ifdef ETHTOOL_GSTATS
			case ETH_SS_STATS:
				gstrings.len = S2IO_STAT_LEN;
				mem_sz = S2IO_STAT_STRINGS_LEN;
				strings = kmalloc(mem_sz, GFP_KERNEL);
				if (!strings)
					return -ENOMEM;
				memcpy(strings,
				       &ethtool_stats_keys,
				       sizeof(ethtool_stats_keys));
				break;
#endif

			default:
				return -EOPNOTSUPP;
			}

			if (copy_to_user
			    (data, &gstrings, sizeof(gstrings)))
				ret = -EFAULT;
			if (!ret) {
				data +=
				    offsetof(struct ethtool_gstrings,
					     data);
				if (copy_to_user(data, strings, mem_sz))
					ret = -EFAULT;
			}
			kfree(strings);
			if (ret)
				return ret;
			break;
		}
	case ETHTOOL_TEST:
		{
			struct {
				struct ethtool_test ethtest;
				uint64_t data[S2IO_TEST_LEN];
			} test = { {
			ETHTOOL_TEST}};

			if (copy_from_user(&test.ethtest, data,
					   sizeof(test.ethtest)))
				return -EFAULT;

			s2io_ethtool_test(dev, &test.ethtest, test.data);
			if (copy_to_user(data, &test, sizeof(test)))
				return -EFAULT;

			break;
		}
#ifdef ETHTOOL_GSTATS
	case ETHTOOL_GSTATS:
		{
			struct ethtool_stats stats;
			int ret;
			u64 *stat_mem;

			if (copy_from_user(&stats, data, sizeof(stats)))
				return -EFAULT;
			stats.n_stats = S2IO_STAT_LEN;
			stat_mem =
			    kmalloc(stats.n_stats * sizeof(u64), GFP_USER);
			if (!stat_mem)
				return -ENOMEM;

			s2io_get_ethtool_stats(dev, &stats, stat_mem);
			ret = 0;
			if (copy_to_user(data, &stats, sizeof(stats)))
				ret = -EFAULT;
			data += sizeof(stats);
			if (copy_to_user(data, stat_mem,
					 stats.n_stats * sizeof(u64)))
				ret = -EFAULT;
			kfree(stat_mem);
			return ret;
		}
#endif

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}
#else

int s2io_ethtool_get_regs_len(struct net_device *dev)
{
	return (XENA_REG_SPACE);
}


u32 s2io_ethtool_get_rx_csum(struct net_device * dev)
{
	nic_t *sp = dev->priv;

	return (sp->rx_csum);
}
int s2io_ethtool_set_rx_csum(struct net_device *dev, u32 data)
{
	nic_t *sp = dev->priv;

	if (data)
		sp->rx_csum = 1;
	else
		sp->rx_csum = 0;

	return 0;
}
int s2io_get_eeprom_len(struct net_device *dev)
{
	return (XENA_EEPROM_SPACE);
}

int s2io_ethtool_self_test_count(struct net_device *dev)
{
	return (S2IO_TEST_LEN);
}
void s2io_ethtool_get_strings(struct net_device *dev,
			      u32 stringset, u8 * data)
{
	switch (stringset) {
	case ETH_SS_TEST:
		memcpy(data, s2io_gstrings, S2IO_STRINGS_LEN);
		break;
	case ETH_SS_STATS:
		memcpy(data, &ethtool_stats_keys,
		       sizeof(ethtool_stats_keys));
	}
}
static int s2io_ethtool_get_stats_count(struct net_device *dev)
{
	return (S2IO_STAT_LEN);
}

int s2io_ethtool_op_set_tx_csum(struct net_device *dev, u32 data)
{
	if (data)
		dev->features |= NETIF_F_IP_CSUM;
	else
		dev->features &= ~NETIF_F_IP_CSUM;

	return 0;
}


static struct ethtool_ops netdev_ethtool_ops = {
	.get_settings = s2io_ethtool_gset,
	.set_settings = s2io_ethtool_sset,
	.get_drvinfo = s2io_ethtool_gdrvinfo,
	.get_regs_len = s2io_ethtool_get_regs_len,
	.get_regs = s2io_ethtool_gregs,
	.get_link = ethtool_op_get_link,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,00)) || \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,23))
	.get_eeprom_len = s2io_get_eeprom_len,
#endif
	.get_eeprom = s2io_ethtool_geeprom,
	.set_eeprom = s2io_ethtool_seeprom,
	.get_pauseparam = s2io_ethtool_getpause_data,
	.set_pauseparam = s2io_ethtool_setpause_data,
	.get_rx_csum = s2io_ethtool_get_rx_csum,
	.set_rx_csum = s2io_ethtool_set_rx_csum,
	.get_tx_csum = ethtool_op_get_tx_csum,
	.set_tx_csum = s2io_ethtool_op_set_tx_csum,
	.get_sg = ethtool_op_get_sg,
	.set_sg = ethtool_op_set_sg,
#ifdef NETIF_F_TSO
	.get_tso = ethtool_op_get_tso,
	.set_tso = ethtool_op_set_tso,
#endif
	.self_test_count = s2io_ethtool_self_test_count,
	.self_test = s2io_ethtool_test,
	.get_strings = s2io_ethtool_get_strings,
	.phys_id = s2io_ethtool_idnic,
	.get_stats_count = s2io_ethtool_get_stats_count,
	.get_ethtool_stats = s2io_get_ethtool_stats
};
#endif
/**
 *  s2io_ioctl -Entry point for the Ioctl 
 *  @dev :  Device pointer.
 *  @ifr :  An IOCTL specefic structure, that can contain a pointer to
 *  a proprietary structure used to pass information to the driver.
 *  @cmd :  This is used to distinguish between the different commands that
 *  can be passed to the IOCTL functions.
 *  Description:
 *  This function has support for ethtool, adding multiple MAC addresses on 
 *  the NIC and some DBG commands for the util tool.
 *  Return value:
 *  '0' on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure. 
*/

int s2io_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	switch (cmd) {
#ifndef SET_ETHTOOL_OPS
	case SIOCETHTOOL:
		{
			return s2io_ethtool(dev, rq);
		}
#endif
	default:
		{
			return -EOPNOTSUPP;
		}
	}
}

/**
 *  s2io_change-mtu - entry point to change MTU size for the device.
 *   @dev : device pointer.
 *   @new_mtu : the new MTU size for the device.
 *   Description: A driver entry point to change MTU size for the device.
 *   Before changing the MTU the device must be stopped.
  *  Return value:
 *   '0' on success and an appropriate (-)ve integer as defined in errno.h
 *   file on failure.
*/

int s2io_change_mtu(struct net_device *dev, int new_mtu)
{
	nic_t *sp = dev->priv;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) sp->bar0;
	register u64 val64;

	if (netif_running(dev)) {
		DBG_PRINT(ERR_DBG, "%s: Must be stopped to ", dev->name);
		DBG_PRINT(ERR_DBG, "change its MTU \n");
		return -EBUSY;
	}

	if ((new_mtu < MIN_MTU) || (new_mtu > S2IO_JUMBO_SIZE)) {
		DBG_PRINT(ERR_DBG, "%s: MTU size is invalid.\n",
			  dev->name);
		return -EPERM;
	}

/* Set the new MTU into the PYLD register of the NIC */
	val64 = new_mtu;
	writeq(vBIT(val64, 2, 14), &bar0->rmac_max_pyld_len);

	dev->mtu = new_mtu;

	return 0;
}

/**
 *  s2io_tasklet - Bottom half of the ISR.
 *  @dev_adr : address of the device structure in dma_addr_t format.
 *  Description:
 *  This is the tasklet or the bottom half of the ISR. This is
 *  an extension of the ISR which is scheduled by the scheduler to be run 
 *  when the load on the CPU is low. All low priority tasks of the ISR can
 *  be pushed into the tasklet. For now the tasklet is used only to 
 *  replenish the Rx buffers in the Rx buffer descriptors.
  *  Return value:
 *  void.
*/

static void s2io_tasklet(unsigned long dev_addr)
{
	struct net_device *dev = (struct net_device *) dev_addr;
	nic_t *sp = dev->priv;
	int i, ret;
	struct config_param *config;

	config = &sp->config;

	if (!TASKLET_IN_USE) {
		for (i = 0; i < config->RxRingNum; i++) {
			ret = fill_rx_buffers(sp, i);
			if (ret == -ENOMEM) {
				DBG_PRINT(ERR_DBG, "%s: Out of ",
					  dev->name);
				DBG_PRINT(ERR_DBG, "memory in tasklet\n");
				break;
			} else if (ret == -EFILL) {
				DBG_PRINT(ERR_DBG,
					  "%s: Rx Ring %d is full\n",
					  dev->name, i);
				break;
			}
		}
		clear_bit(0, (unsigned long *) (&sp->tasklet_status));
	}
}

/**
 * s2io_set_link- Set the LInk status
 * @data: long pointer to device private structue
 * Description: Sets the link status for the adapter
 */

static void s2io_set_link(unsigned long data)
{
	nic_t *nic = (nic_t *) data;
	struct net_device *dev = nic->dev;
	XENA_dev_config_t *bar0 = (XENA_dev_config_t *) nic->bar0;
	register u64 val64;
	u16 subid;

	subid = nic->pdev->subsystem_device;
	/* Allow a small delay for the NICs self initiated 
	 * cleanup to complete.
	 */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ / 10);

	val64 = readq(&bar0->adapter_status);
	if (verify_xena_quiescence(val64, nic->device_enabled_once)) {
		if (LINK_IS_UP(val64)) {
			val64 = readq(&bar0->adapter_control);
			val64 |= ADAPTER_CNTL_EN;
			writeq(val64, &bar0->adapter_control);
			if(CARDS_WITH_FAULTY_LINK_INDICATORS(subid)) {
				val64 = readq(&bar0->gpio_control);
				val64 |= GPIO_CTRL_GPIO_0;
				writeq(val64, &bar0->gpio_control);
				val64 = readq(&bar0->gpio_control);
			} else {
				val64 |= ADAPTER_LED_ON;
				writeq(val64, &bar0->adapter_control);
			}
			val64 = readq(&bar0->adapter_status);
			if (!LINK_IS_UP(val64)) {
				DBG_PRINT(ERR_DBG, "%s:", dev->name);
				DBG_PRINT(ERR_DBG, " Link down");
				DBG_PRINT(ERR_DBG, "after ");
				DBG_PRINT(ERR_DBG, "enabling ");
				DBG_PRINT(ERR_DBG, "device \n");
			}
			if (nic->device_enabled_once == FALSE) {
				nic->device_enabled_once = TRUE;
			}
			s2io_link(nic, LINK_UP);
		} else {
			if(CARDS_WITH_FAULTY_LINK_INDICATORS(subid)) {
				val64 = readq(&bar0->gpio_control);
				val64 &= ~GPIO_CTRL_GPIO_0;
				writeq(val64, &bar0->gpio_control);
				val64 = readq(&bar0->gpio_control);
			}
			s2io_link(nic, LINK_DOWN);
		}
	} else {		/* NIC is not Quiescent. */
		DBG_PRINT(ERR_DBG, "%s: Error: ", dev->name);
		DBG_PRINT(ERR_DBG, "device is not Quiescent\n");
		netif_stop_queue(dev);
	}
}

/** 
 * s2io-restart_nic -Resets the NIC.
 * @data : long pointer to the device private structure
 * Description:
 * This function is scheduled to be run by the s2io_tx_watchdog
 * function after 0.5 secs to reset the NIC. The idea is to reduce 
 * the run time of the watch dog routine which is run holding a
 * spin lock.
 */

static void s2io_restart_nic(unsigned long data)
{
	struct net_device *dev = (struct net_device *) data;
	nic_t *sp = dev->priv;

#if LATEST_CHANGES
	sp->task_flag = 1;
#endif
	s2io_close(dev);
#if LATEST_CHANGES
	sp->task_flag = 0;
#endif
	sp->device_close_flag = TRUE;
	s2io_open(dev);
	DBG_PRINT(ERR_DBG,
		  "%s: was reset by Tx watchdog timer.\n", dev->name);
}

/** 
 *  s2io_tx_watchdog - Watchdog for transmit side. 
 *  @dev : Pointer to net device structure
 *  Description:
 *  This function is triggered if the Tx Queue is stopped
 *  for a pre-defined amount of time when the Interface is still up.
 *  If the Interface is jammed in such a situation, the hardware is
 *  reset (by s2io_close) and restarted again (by s2io_open) to
 *  overcome any problem that might have been caused in the hardware.
 *  Return value:
 *  void
*/

static void s2io_tx_watchdog(struct net_device *dev)
{
	nic_t *sp = dev->priv;

	if (netif_carrier_ok(dev)) {
#ifdef INIT_TQUEUE
		schedule_task(&sp->rst_timer_task);
#else
		schedule_work(&sp->rst_timer_task);
#endif
	}
}

/**
 *   rx_osm_handler - To perform some OS related operations on SKB.
 *   @sp: private member of the device structure,pointer to s2io_nic structure.
 *   @skb : the socket buffer pointer.
 *   @len : length of the packet
 *   @cksum : FCS checksum of the frame.
 *   @ring_no : the ring from which this RxD was extracted.
 *   Description: 
 *   This function is called by the Tx interrupt serivce routine to perform
 *   some OS related operations on the SKB before passing it to the upper
 *   layers. It mainly checks if the checksum is OK, if so adds it to the
 *   SKBs cksum variable, increments the Rx packet count and passes the SKB
 *   to the upper layer. If the checksum is wrong, it increments the Rx
 *   packet error count, frees the SKB and returns error.
 *   Return value:
 *   SUCCESS on success and -1 on failure.
*/
#ifndef CONFIG_2BUFF_MODE
static int rx_osm_handler(nic_t * sp, u16 len, RxD_t * rxdp, int ring_no)
#else
static int rx_osm_handler(nic_t * sp, RxD_t * rxdp, int ring_no,
			  buffAdd_t * ba)
#endif
{
	struct net_device *dev = (struct net_device *) sp->dev;
	struct sk_buff *skb =
	    (struct sk_buff *) ((unsigned long) rxdp->Host_Control);
	u16 l3_csum, l4_csum;
#ifdef CONFIG_2BUFF_MODE
	int buf0_len, buf2_len;
	struct ethhdr *eth = (struct ethhdr *) ba->ba_0;
#endif

	l3_csum = RXD_GET_L3_CKSUM(rxdp->Control_1);
	if ((rxdp->Control_1 & TCP_OR_UDP_FRAME) && (sp->rx_csum)) {
		l4_csum = RXD_GET_L4_CKSUM(rxdp->Control_1);
		if ((l3_csum == L3_CKSUM_OK) && (l4_csum == L4_CKSUM_OK)) {
			/* NIC verifies if the Checksum of the received
			 * frame is Ok or not and accordingly returns
			 * a flag in the RxD.
			 */
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		} else {
			/* 
			 * Packet with erroneous checksum, let the 
			 * upper layers deal with it.
			 */
			skb->ip_summed = CHECKSUM_NONE;
		}
	} else {
		skb->ip_summed = CHECKSUM_NONE;
	}

	if (rxdp->Control_1 & RXD_T_CODE) {
		unsigned long long err = rxdp->Control_1 & RXD_T_CODE;
		DBG_PRINT(ERR_DBG, "%s: Rx error Value: 0x%llx\n",
			  dev->name, err);
	}
#ifdef CONFIG_2BUFF_MODE
	buf0_len = RXD_GET_BUFFER0_SIZE(rxdp->Control_2);
	buf2_len = RXD_GET_BUFFER2_SIZE(rxdp->Control_2);
#endif

	skb->dev = dev;
#ifndef CONFIG_2BUFF_MODE
	skb_put(skb, len);
	skb->protocol = eth_type_trans(skb, dev);
#else
	skb_put(skb, buf2_len);
	/* Reproducing eth_type_trans functionality and running
	 * on the ethernet header 'eth' stripped and given to us
	 * by the hardware in 2Buff mode.
	 */
	if (*eth->h_dest & 1) {
		if (!memcmp(eth->h_dest, dev->broadcast, ETH_ALEN))
			skb->pkt_type = PACKET_BROADCAST;
		else
			skb->pkt_type = PACKET_MULTICAST;
	} else if (memcmp(eth->h_dest, dev->dev_addr, ETH_ALEN)) {
		skb->pkt_type = PACKET_OTHERHOST;
	}
	skb->protocol = eth->h_proto;
#endif

#ifdef CONFIG_S2IO_NAPI
	netif_receive_skb(skb);
#else
	netif_rx(skb);
#endif

#ifdef CONFIG_2BUFF_MODE
	kfree(ba->ba_0_org);
	kfree(ba->ba_1_org);
#endif

	dev->last_rx = jiffies;
#if DEBUG_ON
	sp->rxpkt_cnt++;
#endif
	sp->rx_pkt_count++;
	sp->stats.rx_packets++;
#ifndef CONFIG_2BUFF_MODE
	sp->stats.rx_bytes += len;
	sp->rxpkt_bytes += len;
#else
	sp->stats.rx_bytes += buf0_len + buf2_len;
	sp->rxpkt_bytes += buf0_len + buf2_len;
#endif

	atomic_dec(&sp->rx_bufs_left[ring_no]);
	rxdp->Host_Control = 0;
	return SUCCESS;
}

int check_for_tx_space(nic_t * sp)
{
	u32 put_off, get_off, queue_len;
	int ret = TRUE, i;

	for (i = 0; i < sp->config.TxFIFONum; i++) {
#ifdef MAC
        queue_len = sp->mac_control_tx.tx_curr_put_info[i].fifo_len
            + 1;
        put_off = sp->mac_control_tx.tx_curr_put_info[i].offset;
        get_off = sp->mac_control_tx.tx_curr_get_info[i].offset;
#else
		queue_len = sp->mac_control.tx_curr_put_info[i].fifo_len
		    + 1;
		put_off = sp->mac_control.tx_curr_put_info[i].offset;
		get_off = sp->mac_control.tx_curr_get_info[i].offset;
#endif
		if (((put_off + 1) % queue_len) == get_off) {
			ret = FALSE;
			break;
		}
	}

	return ret;
}

/**
*  s2io_link - stops/starts the Tx queue.
*  @sp : private member of the device structure, which is a pointer to the
*  s2io_nic structure.
*  @link : inidicates whether link is UP/DOWN.
*  Description:
*  This function stops/starts the Tx queue depending on whether the link
*  status of the NIC is is down or up. This is called by the Alarm 
*  interrupt handler whenever a link change interrupt comes up. 
*  Return value:
*  void.
*/

void s2io_link(nic_t * sp, int link)
{
	struct net_device *dev = (struct net_device *) sp->dev;

	if (link != sp->last_link_state) {
		if (link == LINK_DOWN) {
			DBG_PRINT(ERR_DBG, "%s: Link down\n", dev->name);
			netif_carrier_off(dev);
			netif_stop_queue(dev);
		} else {
			DBG_PRINT(ERR_DBG, "%s: Link Up\n", dev->name);
			netif_carrier_on(dev);
			if (check_for_tx_space(sp) == TRUE) {
				/* Don't wake the queue, if we know there
				 * are no free TxDs available.
				 */
				netif_wake_queue(dev);
			}
		}
	}
	sp->last_link_state = link;
}

/**
*  get_xena_rev_id - to identify revision ID of xena. 
*  @pdev : PCI Dev structure
*  Description:
*  Function to identify the Revision ID of xena.
*  Return value:
*  returns the revision ID of the device.
*/

int get_xena_rev_id(struct pci_dev *pdev)
{
	u8 id = 0;
	int ret;
	ret = pci_read_config_byte(pdev, PCI_REVISION_ID, (u8 *) & id);
	return id;
}

/**
*  s2io_init_pci -Initialization of PCI and PCI-X configuration registers . 
*  @sp : private member of the device structure, which is a pointer to the 
*  s2io_nic structure.
*  Description:
*  This function initializes a few of the PCI and PCI-X configuration registers
*  with recommended values.
*  Return value:
*  void
*/

static void s2io_init_pci(nic_t * sp)
{
	u16 pci_cmd = 0;

/* Enable Data Parity Error Recovery in PCI-X command register. */
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(sp->pcix_cmd));
	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			      (sp->pcix_cmd | 1));
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(sp->pcix_cmd));

/* Set the PErr Response bit in PCI command register. */
	pci_read_config_word(sp->pdev, PCI_COMMAND, &pci_cmd);
	pci_write_config_word(sp->pdev, PCI_COMMAND,
			      (pci_cmd | PCI_COMMAND_PARITY));
	pci_read_config_word(sp->pdev, PCI_COMMAND, &pci_cmd);

/* Set user specified value in Latency Timer */
	if (latency_timer) {
		pci_write_config_byte(sp->pdev, PCI_LATENCY_TIMER,
				      latency_timer);
		pci_read_config_byte(sp->pdev, PCI_LATENCY_TIMER,
				     &latency_timer);
	}

/* Set MMRB count to 4096 in PCI-X Command register. */
	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			      (sp->pcix_cmd | 0x0C));
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(sp->pcix_cmd));

/*  Setting Maximum outstanding splits based on system type. */
	sp->pcix_cmd &= 0xFF8F;

	sp->pcix_cmd |= XENA_MAX_OUTSTANDING_SPLITS(max_splits_trans);
	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			      sp->pcix_cmd);
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(sp->pcix_cmd));

#if LATEST_CHANGES
/* Forcibly disabling Relaxed ordering capability of the card. */
	sp->pcix_cmd &= ~(0x0002);
	pci_write_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			      sp->pcix_cmd);
	pci_read_config_word(sp->pdev, PCIX_COMMAND_REGISTER,
			     &(sp->pcix_cmd));
#endif
}

MODULE_AUTHOR("Raghavendra Koushik <raghavendra.koushik@s2io.com>");
MODULE_LICENSE("GPL");
MODULE_PARM(lso_enable, "i");
MODULE_PARM(indicate_max_pkts, "i");
MODULE_PARM(cksum_offload_enable, "i");
MODULE_PARM(TxFifoNum, "i");
MODULE_PARM(TxFIFOLen_0, "i");
MODULE_PARM(TxFIFOLen_1, "i");
MODULE_PARM(TxFIFOLen_2, "i");
MODULE_PARM(TxFIFOLen_3, "i");
MODULE_PARM(TxFIFOLen_4, "i");
MODULE_PARM(TxFIFOLen_5, "i");
MODULE_PARM(TxFIFOLen_6, "i");
MODULE_PARM(TxFIFOLen_7, "i");
MODULE_PARM(MaxTxDs, "i");
MODULE_PARM(RxRingNum, "i");
MODULE_PARM(RxRingSz_0, "i");
MODULE_PARM(RxRingSz_1, "i");
MODULE_PARM(RxRingSz_2, "i");
MODULE_PARM(RxRingSz_3, "i");
MODULE_PARM(RxRingSz_4, "i");
MODULE_PARM(RxRingSz_5, "i");
MODULE_PARM(RxRingSz_6, "i");
MODULE_PARM(RxRingSz_7, "i");
MODULE_PARM(Stats_refresh_time, "i");
MODULE_PARM(rmac_pause_time, "i");
MODULE_PARM(mc_pause_threshold_q0q3, "i");
MODULE_PARM(mc_pause_threshold_q4q7, "i");
MODULE_PARM(shared_splits, "i");
MODULE_PARM(max_splits_trans, "i");
MODULE_PARM(tmac_util_period, "i");
MODULE_PARM(rmac_util_period, "i");
MODULE_PARM(tx_timer_val, "i");
MODULE_PARM(tx_utilz_periodic, "i");
MODULE_PARM(rx_timer_val, "i");
MODULE_PARM(rx_utilz_periodic, "i");
MODULE_PARM(tx_urange_a, "i");
MODULE_PARM(tx_ufc_a, "i");
MODULE_PARM(tx_urange_b, "i");
MODULE_PARM(tx_ufc_b, "i");
MODULE_PARM(tx_urange_c, "i");
MODULE_PARM(tx_ufc_c, "i");
MODULE_PARM(tx_ufc_d, "i");
MODULE_PARM(rx_urange_a, "i");
MODULE_PARM(rx_ufc_a, "i");
MODULE_PARM(rx_urange_b, "i");
MODULE_PARM(rx_ufc_b, "i");
MODULE_PARM(rx_urange_c, "i");
MODULE_PARM(rx_ufc_c, "i");
MODULE_PARM(rx_ufc_d, "i");
MODULE_PARM(latency_timer, "i");
/**
*  s2io_init_nic - Initialization of the adapter . 
*  @pdev : structure containing the PCI related information of the device.
*  @pre: List of PCI devices supported by the driver listed in s2io_tbl.
*  Description:
*  The function initializes an adapter identified by the pci_dec structure.
*  All OS related initialization including memory and device structure and 
*  initlaization of the device private variable is done. Also the swapper 
*  control register is initialized to enable read and write into the I/O 
*  registers of the device.
*  Return value:
*  returns '0' on success and negative on failure.
*/

static int __devinit
s2io_init_nic(struct pci_dev *pdev, const struct pci_device_id *pre)
{
	nic_t *sp;
	struct net_device *dev;
	char *dev_name = "S2IO 10GE NIC";
	int i, j, ret;
	int dma_flag = FALSE;
	u32 mac_up, mac_down;
	u64 val64 = 0, tmp64 = 0;
	XENA_dev_config_t *bar0 = NULL;
	u16 subid;
#ifdef MAC
    mac_info_tx_t *mac_control_tx;
    mac_info_rx_t *mac_control_rx;
#else
	mac_info_t *mac_control;
#endif
	struct config_param *config;


	if ((ret = pci_enable_device(pdev))) {
		DBG_PRINT(ERR_DBG,
			  "s2io_init_nic: pci_enable_device failed\n");
		return ret;
	}

	if (!pci_set_dma_mask(pdev, 0xffffffffffffffffULL)) {
		DBG_PRINT(INIT_DBG, "s2io_init_nic: Using 64bit DMA\n");
		dma_flag = TRUE;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,00))
		if (pci_set_consistent_dma_mask
		    (pdev, 0xffffffffffffffffULL)) {
			DBG_PRINT(ERR_DBG,
				  "Unable to obtain 64bit DMA for \
					consistent allocations\n");
			pci_disable_device(pdev);
			return -ENOMEM;
		}
#endif
	} else if (!pci_set_dma_mask(pdev, 0xffffffffUL)) {
		DBG_PRINT(INIT_DBG, "s2io_init_nic: Using 32bit DMA\n");
	} else {
		pci_disable_device(pdev);
		return -ENOMEM;
	}

	if (pci_request_regions(pdev, s2io_driver_name)) {
		DBG_PRINT(ERR_DBG, "Request Regions failed\n"),
		    pci_disable_device(pdev);
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(nic_t));
	if (dev == NULL) {
		DBG_PRINT(ERR_DBG, "Device allocation failed\n");
		pci_disable_device(pdev);
		pci_release_regions(pdev);
		return -ENODEV;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, dev);
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	/*  Private member variable initialized to s2io NIC structure */
	sp = dev->priv;
	memset(sp, 0, sizeof(nic_t));
	sp->dev = dev;
	sp->pdev = pdev;
	sp->vendor_id = pdev->vendor;
	sp->device_id = pdev->device;
	sp->high_dma_flag = dma_flag;
	sp->irq = pdev->irq;
	sp->device_enabled_once = FALSE;
	strcpy(sp->name, dev_name);

	/* Initialize some PCI/PCI-X fields of the NIC. */
	s2io_init_pci(sp);

	/* Setting the device configuration parameters.
	 * Most of these parameters can be specified by the user during 
	 * module insertion as they are module loadable parameters. If 
	 * these parameters are not not specified during load time, they 
	 * are initialized with default values.
	 */
#ifdef MAC
    mac_control_tx = &sp->mac_control_tx;
    mac_control_rx = &sp->mac_control_rx;
#else
	mac_control = &sp->mac_control;
#endif
	config = &sp->config;

	/* Tx side parameters. */
	config->TxFIFONum = TxFifoNum;
	config->TxCfg[0].FifoLen = TxFIFOLen_0;
	config->TxCfg[0].FifoPriority = 0;
	config->TxCfg[1].FifoLen = TxFIFOLen_1;
	config->TxCfg[1].FifoPriority = 1;
	config->TxCfg[2].FifoLen = TxFIFOLen_2;
	config->TxCfg[2].FifoPriority = 2;
	config->TxCfg[3].FifoLen = TxFIFOLen_3;
	config->TxCfg[3].FifoPriority = 3;
	config->TxCfg[4].FifoLen = TxFIFOLen_4;
	config->TxCfg[4].FifoPriority = 4;
	config->TxCfg[5].FifoLen = TxFIFOLen_5;
	config->TxCfg[5].FifoPriority = 5;
	config->TxCfg[6].FifoLen = TxFIFOLen_6;
	config->TxCfg[6].FifoPriority = 6;
	config->TxCfg[7].FifoLen = TxFIFOLen_7;
	config->TxCfg[7].FifoPriority = 7;

	config->TxIntrType = TXD_INT_TYPE_UTILZ;
	for (i = 0; i < config->TxFIFONum; i++) {
		config->TxCfg[i].fNoSnoop =
		    (NO_SNOOP_TXD | NO_SNOOP_TXD_BUFFER);
		if (config->TxCfg[i].FifoLen < 65) {
			config->TxIntrType = TXD_INT_TYPE_PER_LIST;
			break;
		}
	}
	config->MaxTxDs = MAX_SKB_FRAGS;
	config->TxFlow = TRUE;

	/* Rx side parameters. */
	config->RxRingNum = RxRingNum;
	config->RxCfg[0].NumRxd = RxRingSz_0 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[0].RingPriority = 0;
	config->RxCfg[1].NumRxd = RxRingSz_1 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[1].RingPriority = 1;
	config->RxCfg[2].NumRxd = RxRingSz_2 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[2].RingPriority = 2;
	config->RxCfg[3].NumRxd = RxRingSz_3 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[3].RingPriority = 3;
	config->RxCfg[4].NumRxd = RxRingSz_4 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[4].RingPriority = 4;
	config->RxCfg[5].NumRxd = RxRingSz_5 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[5].RingPriority = 5;
	config->RxCfg[6].NumRxd = RxRingSz_6 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[6].RingPriority = 6;
	config->RxCfg[7].NumRxd = RxRingSz_7 * (MAX_RXDS_PER_BLOCK + 1);
	config->RxCfg[7].RingPriority = 7;

	for (i = 0; i < RxRingNum; i++) {
		config->RxCfg[i].RingOrg = RING_ORG_BUFF1;
		config->RxCfg[i].RxdThresh = DEFAULT_RXD_THRESHOLD;
		config->RxCfg[i].fNoSnoop =
		    (NO_SNOOP_RXD | NO_SNOOP_RXD_BUFFER);
		config->RxCfg[i].RxD_BackOff_Interval = TBD;
	}

	config->RxFlow = TRUE;

	/* Miscellaneous parameters. */
	config->RxVLANEnable = TRUE;
	config->MTU = MAX_MTU_VLAN;
	config->JumboEnable = FALSE;

	/*  Setting Mac Control parameters */
#ifdef TXDBD
#ifdef MAC
   mac_control_tx->txdl_len = MAX_SKB_FRAGS;
    mac_control_rx->rmac_pause_time = rmac_pause_time;
    mac_control_rx->mc_pause_threshold_q0q3 = mc_pause_threshold_q0q3;
    mac_control_rx->mc_pause_threshold_q4q7 = mc_pause_threshold_q4q7;
    {
        int nPow = 1;
        while(nPow < MAX_SKB_FRAGS){
            nPow *= 2;
        }
        mac_control_tx->txdl_len = nPow;
        config->MaxTxDs = nPow;
        mac_control_tx->max_txds_per_block = ( 4 * BLOCK_SIZE) / sizeof(TxD_t);

    }
#else
    mac_control->txdl_len = MAX_SKB_FRAGS;
    mac_control->rmac_pause_time = rmac_pause_time;
    mac_control->mc_pause_threshold_q0q3 = mc_pause_threshold_q0q3;
    mac_control->mc_pause_threshold_q4q7 = mc_pause_threshold_q4q7;
    {
        int nPow = 1;
        while(nPow < MAX_SKB_FRAGS){
            nPow *= 2;
        }
        mac_control->txdl_len = nPow;
        config->MaxTxDs = nPow;
        mac_control->max_txds_per_block = ( 4 * BLOCK_SIZE) / sizeof(TxD_t);

    }
#endif

#else
#ifdef MAC
	mac_control_tx->txdl_len = MAX_SKB_FRAGS;
	mac_control_rx->rmac_pause_time = rmac_pause_time;
	mac_control_rx->mc_pause_threshold_q0q3 = mc_pause_threshold_q0q3;
	mac_control_rx->mc_pause_threshold_q4q7 = mc_pause_threshold_q4q7;
#else
	mac_control->txdl_len = MAX_SKB_FRAGS;
	mac_control->rmac_pause_time = rmac_pause_time;
	mac_control->mc_pause_threshold_q0q3 = mc_pause_threshold_q0q3;
	mac_control->mc_pause_threshold_q4q7 = mc_pause_threshold_q4q7;
#endif
#endif
	/* Initialize Ring buffer parameters. */
	for (i = 0; i < config->RxRingNum; i++)
		atomic_set(&sp->rx_bufs_left[i], 0);

	/*  initialize the shared memory used by the NIC and the host */
	if (init_shared_mem(sp)) {
		DBG_PRINT(ERR_DBG, "%s: Memory allocation failed\n",
			  dev->name);
		goto mem_alloc_failed;
	}

	sp->bar0 = (caddr_t) ioremap(pci_resource_start(pdev, 0),
				     pci_resource_len(pdev, 0));
	if (!sp->bar0) {
		DBG_PRINT(ERR_DBG, "%s: S2IO: cannot remap io mem1\n",
			  dev->name);
		goto bar0_remap_failed;
	}

	sp->bar1 = (caddr_t) ioremap(pci_resource_start(pdev, 2),
				     pci_resource_len(pdev, 2));
	if (!sp->bar1) {
		DBG_PRINT(ERR_DBG, "%s: S2IO: cannot remap io mem2\n",
			  dev->name);
		goto bar1_remap_failed;
	}

	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) sp->bar0;

	/* Initializing the BAR1 address as the start of the FIFO pointer. */
	for (j = 0; j < MAX_TX_FIFOS; j++) {
#ifdef MAC
        mac_control_tx->tx_FIFO_start[j] = (TxFIFO_element_t *)
            (sp->bar1 + (j * 0x00020000));
#else
		mac_control->tx_FIFO_start[j] = (TxFIFO_element_t *)
		    (sp->bar1 + (j * 0x00020000));
#endif
	}

	/*  Driver entry points */
	dev->open = &s2io_open;
	dev->stop = &s2io_close;
	dev->hard_start_xmit = &s2io_xmit;
	dev->get_stats = &s2io_get_stats;
	dev->set_multicast_list = &s2io_set_multicast;
	dev->do_ioctl = &s2io_ioctl;
	dev->change_mtu = &s2io_change_mtu;
#ifdef SET_ETHTOOL_OPS
	SET_ETHTOOL_OPS(dev, &netdev_ethtool_ops);
#endif
	/*
	 * will use eth_mac_addr() for  dev->set_mac_address
	 * mac address will be set every time dev->open() is called
	 */
#ifdef CONFIG_S2IO_NAPI
	dev->poll = s2io_poll;
	dev->weight = 64;	/* For now. */
#endif

	dev->features |= NETIF_F_SG;
	if (cksum_offload_enable)
		dev->features |= NETIF_F_IP_CSUM;
	if (sp->high_dma_flag == TRUE)
		dev->features |= NETIF_F_HIGHDMA;
#ifdef NETIF_F_TSO
	if (lso_enable)
		dev->features |= NETIF_F_TSO;
#endif

	dev->tx_timeout = &s2io_tx_watchdog;
	dev->watchdog_timeo = WATCH_DOG_TIMEOUT;
#ifdef INIT_TQUEUE
	INIT_TQUEUE(&sp->rst_timer_task,
		    (void (*)(void *)) s2io_restart_nic, dev);
	INIT_TQUEUE(&sp->set_link_task,
		    (void (*)(void *)) s2io_set_link, sp);
#else
	INIT_WORK(&sp->rst_timer_task,
		  (void (*)(void *)) s2io_restart_nic, dev);
	INIT_WORK(&sp->set_link_task,
		  (void (*)(void *)) s2io_set_link, sp);
#endif


	pci_save_state(sp->pdev, sp->config_space);

	/* Setting swapper control on the NIC, for proper reset operation */
	if (s2io_set_swapper(sp)) {
		DBG_PRINT(ERR_DBG, "%s:swapper settings are wrong\n",
			  dev->name);
		goto set_swap_failed;
	}

	/* Fix for all "FFs" MAC address problems observed on Alpha platforms */
	fix_mac_address(sp);
	s2io_reset(sp);

	/* Setting swapper control on the NIC, so the MAC address can be read.
	 */
	if (s2io_set_swapper(sp)) {
		DBG_PRINT(ERR_DBG,
			  "%s: S2IO: swapper settings are wrong\n",
			  dev->name);
		goto set_swap_failed;
	}

	/*  MAC address initialization.
	 *  For now only one mac address will be read and used.
	 */
	bar0 = (XENA_dev_config_t *) sp->bar0;
	val64 = RMAC_ADDR_CMD_MEM_RD | RMAC_ADDR_CMD_MEM_STROBE_NEW_CMD |
	    RMAC_ADDR_CMD_MEM_OFFSET(0 + MAC_MAC_ADDR_START_OFFSET);
	writeq(val64, &bar0->rmac_addr_cmd_mem);
	wait_for_cmd_complete(sp);

	tmp64 = readq(&bar0->rmac_addr_data0_mem);
	mac_down = (u32) tmp64;
	mac_up = (u32) (tmp64 >> 32);

	memset(sp->defMacAddr[0].mac_addr, 0, sizeof(ETH_ALEN));

	sp->defMacAddr[0].mac_addr[3] = (u8) (mac_up);
	sp->defMacAddr[0].mac_addr[2] = (u8) (mac_up >> 8);
	sp->defMacAddr[0].mac_addr[1] = (u8) (mac_up >> 16);
	sp->defMacAddr[0].mac_addr[0] = (u8) (mac_up >> 24);
	sp->defMacAddr[0].mac_addr[5] = (u8) (mac_down >> 16);
	sp->defMacAddr[0].mac_addr[4] = (u8) (mac_down >> 24);

	DBG_PRINT(INIT_DBG,
		  "DEFAULT MAC ADDR:0x%02x-%02x-%02x-%02x-%02x-%02x\n",
		  sp->defMacAddr[0].mac_addr[0],
		  sp->defMacAddr[0].mac_addr[1],
		  sp->defMacAddr[0].mac_addr[2],
		  sp->defMacAddr[0].mac_addr[3],
		  sp->defMacAddr[0].mac_addr[4],
		  sp->defMacAddr[0].mac_addr[5]);

	/*  Set the factory defined MAC address initially   */
	dev->addr_len = ETH_ALEN;
	memcpy(dev->dev_addr, sp->defMacAddr, ETH_ALEN);

	/*  Initialize the tasklet status flag */
	atomic_set(&(sp->tasklet_status), 0);


	/* Initialize spinlocks */
#if (!LATEST_CHANGES)
	spin_lock_init(&sp->isr_lock);
#endif
	spin_lock_init(&sp->tx_lock);

	/* SXE-002: Configure link and activity LED to init state 
	 * on driver load. 
	 */
	subid = sp->pdev->subsystem_device;
	if ((subid & 0xFF) >= 0x07) {
		val64 = readq(&bar0->gpio_control);
		val64 |= 0x0000800000000000ULL;
		writeq(val64, &bar0->gpio_control);
		val64 = 0x0411040400000000ULL;
		writeq(val64, (u64 *) ((u8 *) bar0 + 0x2700));
		val64 = readq(&bar0->gpio_control);
	}


	sp->rx_csum = 1;	/* Rx chksum verify enabled by default */
       #ifdef SNMP_SUPPORT
        if(!s2io_bdsnmp_init(dev))
                DBG_PRINT(INIT_DBG,"Error Creating Proc directory for SNMP\n");

        sp->nLinkStatus = 1;
        #ifdef NETIF_F_TSO
        sp->nFeature = 1;
        #endif
        memcpy(sp->cVersion,s2io_driver_version+8,20);
        memcpy(sp->cName, s2io_driver_name,20);
        struct timeval tm;
        do_gettimeofday(&tm);
        sp->lDate = tm.tv_sec;
        #endif

	if (register_netdev(dev)) {
		DBG_PRINT(ERR_DBG, "Device registration failed\n");
		goto register_failed;
	}

	/* Make Link state as off at this point, when the Link change 
	 * interrupt comes the state will be automatically changed to 
	 * the right state.
	 */
	netif_carrier_off(dev);
	sp->last_link_state = LINK_DOWN;

	return 0;

      register_failed:
      set_swap_failed:
	iounmap(sp->bar1);
      bar1_remap_failed:
	iounmap(sp->bar0);
      bar0_remap_failed:
      mem_alloc_failed:
	free_shared_mem(sp);
	pci_disable_device(pdev);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	free_netdev(dev);

	return -ENODEV;
}

/**
* s2io_rem_nic - Free the PCI device 
* @pdev: structure containing the PCI related information of the device.
* Description: This function is called by the Pci subsystem to release a 
* PCI device and free up all resource held up by the device. This could
* be in response to a Hot plug event or when the driver is to be removed 
* from memory.
*/

static void __devexit s2io_rem_nic(struct pci_dev *pdev)
{
	struct net_device *dev =
	    (struct net_device *) pci_get_drvdata(pdev);
	nic_t *sp;

	if (dev == NULL) {
		DBG_PRINT(ERR_DBG, "Driver Data is NULL!!\n");
		return;
	}
	sp = dev->priv;
	free_shared_mem(sp);
	iounmap(sp->bar0);
	iounmap(sp->bar1);
	pci_disable_device(pdev);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
    #ifdef SNMP_SUPPORT
    s2io_bdsnmp_rem(dev);
    #endif

	unregister_netdev(dev);

	free_netdev(dev);
}

/**
 * s2io_starter - Entry point for the driver
 * Description: This function is the entry point for the driver. It verifies
 * the module loadable parameters and initializes PCI configuration space.
 */

int __init s2io_starter(void)
{
	if (verify_load_parm())
		return -ENODEV;
	return pci_module_init(&s2io_driver);
}

/**
 * s2io_closer - Cleanup routine for the driver 
 * Description: This function is the cleanup routine for the driver. It unregist * ers the driver.
 */

void s2io_closer(void)
{
	pci_unregister_driver(&s2io_driver);
	DBG_PRINT(INIT_DBG, "cleanup done\n");
}

module_init(s2io_starter);
module_exit(s2io_closer);
/**
 * verify_load_parm -  verifies the module loadable parameters
 * Descriptions: Verifies the module loadable paramters and initializes the
 * Tx Fifo, Rx Ring and other paramters.
 */

int verify_load_parm()
{
	int fail = 0;
	if (!((lso_enable == 0) || (lso_enable == 1))) {
		printk("lso_enable can be either '1' or '0'\n");
		fail = 1;
	}
	if ((indicate_max_pkts > (0xFFFFFFFF))) {
		printk
		    ("indicate_max_pkts can take value greater than zero but less than 2power(32)\n");
		fail = 1;
	}
	if (!((cksum_offload_enable == 0) || (cksum_offload_enable == 1))) {
		printk("cksum_offload_enable can be only '0' or '1' \n");
		fail = 1;
	}
	if ((TxFifoNum == 0) || (TxFifoNum > 8)) {
		printk("TxFifoNum can take value from 1 to 8\n");
		fail = 1;
	}
	switch (TxFifoNum) {
		case 8:
		if ((TxFIFOLen_7 == 0) || TxFIFOLen_7 > 8192) {
			printk("TxFIFOLen_7 can take value from 1 to 8192\n");
			fail = 1;
		}
		case 7:
		if ((TxFIFOLen_6 == 0) || TxFIFOLen_6 > 8192) {
			printk("TxFIFOLen_6 can take value from 1 to 8192\n");
			fail = 1;
		}
		case 6:	
		if ((TxFIFOLen_5 == 0) || TxFIFOLen_5 > 8192) {
			printk("TxFIFOLen_5 can take value from 1 to 8192\n");
			fail = 1;
		}	
		case 5:
		if ((TxFIFOLen_4 == 0) || TxFIFOLen_4 > 8192) {
			printk("TxFIFOLen_4 can take value from 1 to 8192\n");
			fail = 1;
		}
		case 4:
		if ((TxFIFOLen_3 == 0) || TxFIFOLen_3 > 8192) {
			printk("TxFIFOLen_3 can take value from 1 to 8192\n");
			fail = 1;
		}
		case 3:	
		if ((TxFIFOLen_2 == 0) || TxFIFOLen_2 > 8192) {
			printk("TxFIFOLen_2 can take value from 1 to 8192\n");
			fail = 1;
		}
		case 2:
		if ((TxFIFOLen_1 == 0) || TxFIFOLen_1 > 8192) {
			printk("TxFIFOLen_1 can take value from 1 to 8192\n");
			fail = 1;
		}
		case 1:
		if ((TxFIFOLen_0 == 0) || TxFIFOLen_0 > 8192) {
			printk("TxFIFOLen_0 can take value from 1 to 8192\n");
			fail = 1;
		}
	}
	if ((MaxTxDs > 32) || (MaxTxDs < 1)) {
		printk("MaxTxDs can take falue from 1 to 32\n");
		fail = 1;
	}
	if ((RxRingNum > 8) || (RxRingNum < 1)) {
		printk("RxRingNum can take falue from 1 to 8\n");
		fail = 1;
	}
	switch(RxRingNum) {
		case 8:
		if (RxRingSz_7 < 1) {
			printk("RxRingSz_7 can take value greater than 0\n");
			fail = 1;
		}
		case 7:
		if (RxRingSz_6 < 1) {
			printk("RxRingSz_6 can take value greater than 0\n");
			fail = 1;
		}
		case 6:
		if (RxRingSz_5 < 1) {
			printk("RxRingSz_5 can take value greater than 0\n");
			fail = 1;
		}
		case 5:
		if (RxRingSz_4 < 1) {
			printk("RxRingSz_4 can take value greater than 0\n");
			fail = 1;
		}
		case 4:
		if (RxRingSz_3 < 1) {
			printk("RxRingSz_3 can take value greater than 0\n");
			fail = 1;
		}
		case 3:
		if (RxRingSz_2 < 1) {
			printk("RxRingSz_2 can take value greater than 0\n");
			fail = 1;
		}
		case 2:
		if (RxRingSz_1 < 1) {
			printk("RxRingSz_1 can take value greater than 0\n");
			fail = 1;
		}
		case 1:
		if (RxRingSz_0 < 1) {
			printk("RxRingSz_0 can take value greater than 0\n");
			fail = 1;
		}
	}
	if ((Stats_refresh_time < 1)) {
		printk
		    ("Stats_refresh_time cannot be less than 1 second \n");
		fail = 1;
	}
	if (((rmac_pause_time < 0x10) && (rmac_pause_time != 0)) ||
	    (rmac_pause_time > 0xFFFF)) {
		printk
		    ("rmac_pause_time can take value from 16 to 65535\n");
		fail = 1;
	}
	if ((max_splits_trans < 0) || (max_splits_trans > 7)) {
		printk("max_splits_trans can take value from 0 to 7\n");
		fail = 1;
	}
	if ((mc_pause_threshold_q0q3 > 0xFE)) {
		printk("mc_pause_threshold_q0q3 cannot exceed 254\n");
		fail = 1;
	}
	if ((mc_pause_threshold_q4q7 > 0xFE)) {
		printk("mc_pause_threshold_q4q7 cannot exceed 254\n");
		fail = 1;
	}
	if ((latency_timer)
	    && ((latency_timer < 8) /* || (latency_timer > 255) */ )) {
		printk("latency_timer can take value from 8 to 255\n");
		fail = 1;
	}
	if ((shared_splits > 31)) {
		printk("shared_splits can exceed 31\n");
		fail = 1;
	}
	if (rmac_util_period > 0xF) {
		printk("rmac_util_period can exceed 15\n");
		fail = 1;
	}
	if (tmac_util_period > 0xF) {
		printk("tmac_util_period can exceed 15\n");
		fail = 1;
	}
	if ((tx_utilz_periodic > 1) || (rx_utilz_periodic > 1)) {
		printk
		    ("tx_utilz_periodic & rx_utilz_periodic can be either "
		     "'0' or '1'\n");
		fail = 1;
	}
	if ((tx_urange_a > 127) || (tx_urange_b > 127)
	    || (tx_urange_c > 127)) {
		printk
		    ("tx_urange_a, tx_urange_b & tx_urange_c can take value "
		     "from 0 to 127\n");
		fail = 1;
	}
	if ((rx_urange_a > 127) || (rx_urange_b > 127)
	    || (rx_urange_c > 127)) {
		printk
		    ("rx_urange_a, rx_urange_b & rx_urange_c can take value "
		     "from 0 to 127\n");
		fail = 1;
	}
	if ((tx_ufc_a > 0xffff) || (tx_ufc_b > 0xffff) ||
	    (tx_ufc_c > 0xffff) || (tx_ufc_d > 0xffff)) {
		printk
		    (" tx_ufc_a, tx_ufc_b, tx_ufc_c, tx_ufc_d can take value"
		     "from 0 to 65535(0xFFFF)\n");
		fail = 1;
	}
	if ((rx_ufc_a > 0xffff) || (rx_ufc_b > 0xffff) ||
	    (rx_ufc_c > 0xffff) || (rx_ufc_d > 0xffff)) {
		printk
		    (" rx_ufc_a, rx_ufc_b, rx_ufc_c, rx_ufc_d can take value"
		     "from 0 to 65535(0xFFFF)\n");
		fail = 1;
	}
	return fail;
}
#ifdef SNMP_SUPPORT

/**
* fnBaseDrv - Get the driver information
* @pBaseDrv -Pointer to Base driver structure which contains the offset 
* and length of each of the field.
* Description
* This function copies the driver specific information from the dev structure
* to the pBaseDrv stucture. It calculates the number of physical adapters by
* parsing the dev_base global variable maintained by the kernel. This 
* variable has to read locked before accesing.This function is called by
* fnBaseReadProc function.
*   
*/
              
static void fnBaseDrv(struct stBaseDrv *pBaseDrv,struct net_device *dev)
{
                struct pci_dev *pdev = NULL;
        struct net_device *ndev;
        int nCount =0;
        nic_t *sp = (nic_t *)dev->priv;

        strncpy(pBaseDrv->m_cName,sp->cName,20);
        strncpy(pBaseDrv->m_cVersion,sp->cVersion,20);
        pBaseDrv->m_nStatus = sp->nLinkStatus;
        pBaseDrv->m_nFeature = sp->nFeature;
        pBaseDrv->m_nMemorySize = sp->nMemorySize;
        sprintf(pBaseDrv->m_cDate,"%ld",sp->lDate);
        /* Find all the ethernet devices on the system using pci_find_class.Get
        the private data which will be the net_device structure assigned by the
        driver.
        */
        while((pdev = pci_find_class((PCI_CLASS_NETWORK_ETHERNET <<8), pdev)))
        {
                ndev = (struct net_device *)pci_get_drvdata(pdev);
		if(ndev == NULL)
			break;	
                memcpy(pBaseDrv->m_stPhyAdap[nCount].m_cName, ndev->name,20);
                pBaseDrv->m_stPhyAdap[nCount].m_nIndex = ndev->ifindex;
                nCount ++;
        }
        pBaseDrv->m_nPhyCnt = nCount;
}
/* 
*  fnBaseReadProc - Read entry point for the proc file  
*  @page - Buffer pointer where the data is written
*  @start- Pointer to buffer ptr . It is used if the data is more than a page
*  @off- the offset to the page where data is written
*  @count - number of bytes to write
*  @eof - to indicate end of file
*  @data - pointer to device structure.
*
* Description - 
* This function gets   Base driver specific information from the fnBaseDrv func * tion and writes into the BDInfo file. This function is called whenever the use* r reads the file. The length of data written cannot exceed 4kb. If it exceeds * then use the start pointer to write multiple pages                                                                                 
* Return - the length of the string written to proc file
*/

static int fnBaseReadProc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
        struct stBaseDrv *pBaseDrv;
        int nLength = 0;
        int nCount = 0;
        struct net_device *dev= (struct net_device *)data;
        int nIndex =0;

        pBaseDrv= kmalloc(sizeof(struct stBaseDrv),GFP_KERNEL);
        if(pBaseDrv == NULL)
        {
                printk("Error allocating memory\n");
                return -ENOMEM;
        }
        fnBaseDrv(pBaseDrv,dev);
        sprintf(page + nLength,"%-30s%-20s\n","Base Driver Name",
            pBaseDrv->m_cName);
        nLength += 51;
        if(pBaseDrv->m_nStatus == 1)
                sprintf(page + nLength,"%-30s%-20s\n","Load Status","Loaded");

        else
                sprintf(page + nLength,"%-30s%-20s\n","Load Status","UnLoaded");  
        nLength += 51;
        sprintf(page + nLength,"%-30s%-20s\n","Base Driver Version",
            pBaseDrv->m_cVersion);
        nLength += 51;

        sprintf(page + nLength,"%-30s%-20d\n","Feature Supported",
            pBaseDrv->m_nFeature);
        nLength += 51;

        sprintf(page + nLength,"%-30s%-20d\n","Base Driver Memrory in Bytes",
            pBaseDrv->m_nMemorySize);
        nLength += 51;

        sprintf(page + nLength,"%-30s%-20s\n","Base Driver Date",
            pBaseDrv->m_cDate);
        nLength += 51;

        sprintf(page + nLength,"%-30s%-20d\n","No of Phy Adapter",
            pBaseDrv->m_nPhyCnt);
        nLength += 51;
        sprintf(page + nLength,"%-20s%-20s\n\n","Phy Adapter Index",
            "Phy Adapter Name");
        nLength +=42;

        for(nIndex=0,nCount=pBaseDrv->m_nPhyCnt; nCount != 0; nCount--,nIndex++)
        {
                sprintf(page + nLength,"%-20d%-20s\n",
                    pBaseDrv->m_stPhyAdap[nIndex].m_nIndex,
                    pBaseDrv->m_stPhyAdap[nIndex].m_cName);
                nLength += 41;
        }

        *eof =1;
        kfree(pBaseDrv);
        return nLength;
}

/* 
*  fnPhyAdapReadProc - Read entry point for the proc file  
*  @page - Buffer pointer where the data is written
*  @start- Pointer to buffer ptr . It is used if the data is more than a page
*  @off- the offset to the page where data is written
*  @count - number of bytes to write
*  @eof - to indicate end of file
*  @data - pointer to device structure.
*
* Description - 
* This function gets  physical adapter information. This function is called
*  whenever the use* r reads the file. The length of data written cannot
*  exceed 4kb. If it exceeds * then use the start pointer to write multiple page* Return - the length of the string written to proc file
*/
static int fnPhyAdapReadProc(char *page, char **start, off_t off, int count, int *eof, void *data)
{

        struct stPhyData *pPhyData;
        pPhyData= kmalloc(sizeof(struct stPhyData),GFP_KERNEL);
        if(pPhyData == NULL)
        {
                printk("Error allocating memory\n");
                return -ENOMEM;
        }

        struct net_device *pNetDev;
        struct net_device_stats *pNetStat;
        int nLength = 0;
        unsigned char cMAC[20];

        /* Print the header in the PhyAdap proc file*/
        sprintf(page + nLength,"%-10s%-22s%-10s%-10s%-22s%-22s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s%-10s\n",
                        "Index","Description","Mode","Type","Speed",
                        "MAC","Status","Slot","Bus","IRQ","Colis",
                        "Multi","RxBytes","RxDrop","RxError","RxPacket",
                        "TRxBytes","TRxDrop","TxError","TxPacket");

        /* 237 is the lenght of the above string copied in the page  */
        nLength +=237;

        struct pci_dev *pdev = NULL;
        /*pci_find_class will return to the pointer to the pdev structure
         * for all the network devices using PCI_CLASS_NETWORK_ETHERNET 
         * .The third argument is pointer to previous pdev structure.Initailly
         * it has to be null*/
        while((pdev = pci_find_class((PCI_CLASS_NETWORK_ETHERNET <<8), pdev)))
         {
                /* Private data will point to the netdevice structure*/
                pNetDev =(struct net_device *) pci_get_drvdata(pdev);
                if(pNetDev == NULL)
                        continue;
                if(pNetDev->addr_len != 0)
                {
                        pNetStat = pNetDev->get_stats(pNetDev);
                        pPhyData->m_nIndex = pNetDev->ifindex;
                        memcpy(pPhyData->m_cDesc,pNetDev->name,20);
                        pPhyData->m_nMode = 0;
                        pPhyData->m_nType = 0;
                        switch( pPhyData->m_nType)
                        {
                /*              case IFT_ETHER:
                                    memcpy(pPhyData->m_cSpeed,"10000000",20);
                                    break;

                                case 9:
                                    memcpy(pPhyData->m_cSpeed,"4000000",20);
                                    break;*/
                                default :
                                    memcpy(pPhyData->m_cSpeed,"10000000",20);
                                    break;
                        }
                        memcpy(pPhyData->m_cPMAC,pNetDev->dev_addr,ETH_ALEN);
                        memcpy(pPhyData->m_cCMAC,pNetDev->dev_addr,ETH_ALEN);
                        pPhyData->m_nLinkStatus=test_bit(__LINK_STATE_START,
                                        &pNetDev->state);
                        pPhyData->m_nPCISlot = PCI_SLOT(pdev->devfn);
                        pPhyData->m_nPCIBus = pdev->bus->number;

                        pPhyData->m_nIRQ =pNetDev->irq;
                        pPhyData->m_nCollision = pNetStat->collisions;
                        pPhyData->m_nMulticast = pNetStat->multicast;

                        pPhyData->m_nRxBytes =pNetStat->rx_bytes;
                        pPhyData->m_nRxDropped =pNetStat->rx_dropped;
                        pPhyData->m_nRxErrors = pNetStat->rx_errors;
                        pPhyData->m_nRxPackets = pNetStat->rx_packets;

                        pPhyData->m_nTxBytes =pNetStat->tx_bytes;
                        pPhyData->m_nTxDropped =pNetStat->tx_dropped;
                        pPhyData->m_nTxErrors = pNetStat->tx_errors;
                        pPhyData->m_nTxPackets = pNetStat->tx_packets;

                        sprintf(cMAC,"%02x:%02x:%02x:%02x:%02x:%02x",
                        pPhyData->m_cPMAC[0],pPhyData->m_cPMAC[1],
                        pPhyData->m_cPMAC[2],pPhyData->m_cPMAC[3],
                        pPhyData->m_cPMAC[4],pPhyData->m_cPMAC[5]);
                        sprintf(page + nLength,"%-10d%-22s%-10d%-10d%-22s%-22s%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d%-10d\n",
                                        pPhyData->m_nIndex,
                                        pPhyData->m_cDesc,pPhyData->m_nMode,
                                        pPhyData->m_nType, pPhyData->m_cSpeed,
                                        cMAC, pPhyData->m_nLinkStatus ,
                                        pPhyData->m_nPCISlot,
                                        pPhyData->m_nPCIBus, pPhyData->m_nIRQ,
                                        pPhyData->m_nCollision,
                                        pPhyData->m_nMulticast,
                                        pPhyData->m_nRxBytes,
                                        pPhyData->m_nRxDropped,
                                        pPhyData->m_nRxErrors,
                                        pPhyData->m_nRxPackets,
                                        pPhyData->m_nTxBytes,
                                        pPhyData->m_nTxDropped,
                                        pPhyData->m_nTxErrors,
                                        pPhyData->m_nTxPackets);
                        nLength +=237;
          }
    }

        *eof=1;

        kfree(pPhyData);
        return nLength;
}

/* 
* s2io_bdsnmp_init - Entry point to create proc file
* @dev-  Pointer to net device structure passed by the driver.
* Return 
* Success If creates all the files
* ERROR_PROC_ENTRY /ERROR_PROC_DIR Error If could not create all the files
* Description
* This functon is called when the driver is loaded. It creates the S2IO proc file sys* tem in the /proc/net/ directory. This directory is used to store the info* about the base driver afm driver, lacp, vlan  and nplus.
* It checks if S2IO directory already exists else creates it and creates the fil* es BDInfo  files and assiciates read funct* ion to each of the files.
*/

int s2io_bdsnmp_init(struct net_device *dev)
{
        struct proc_dir_entry *S2ioDir;
        struct proc_dir_entry *BaseDrv;
        struct proc_dir_entry *PhyAdap;
        int nLength = 0;

        nLength = strlen(S2IODIRNAME);
        /* IF the directory already exists then just return*/
        for(S2ioDir=proc_net->subdir; S2ioDir != NULL; S2ioDir = S2ioDir->next)
        {
                if((S2ioDir->namelen == nLength) &&
                 (!memcmp(S2ioDir->name,S2IODIRNAME,nLength)))
                break;
        }
        if(S2ioDir == NULL)
        /* Create the s2io directory*/
        if(!(S2ioDir = create_proc_entry(S2IODIRNAME,S_IFDIR,proc_net)))
        {
                DBG_PRINT(INIT_DBG,"Error Creating Proc directory for SNMP\n");
                return ERROR_PROC_DIR;
        }
        /* Create the BDInfo file to store driver info and associate read funtion*/
       if(!(BaseDrv = create_proc_read_entry(BDFILENAME,S_IFREG | S_IRUSR,
                        S2ioDir,fnBaseReadProc,(void *)dev))){
                DBG_PRINT(INIT_DBG,"Error Creating Proc File for Base Drvr\n");
                return ERROR_PROC_ENTRY;
        }
       if(!(PhyAdap = create_proc_read_entry(PADAPFILENAME,S_IFREG | S_IRUSR,
                S2ioDir,fnPhyAdapReadProc, (void *)dev))){
              DBG_PRINT(INIT_DBG,"Error Creating Proc File for Phys Adap\n");
               return ERROR_PROC_ENTRY;
        }


        return SUCCESS;
}
/* s2io_bdsnmp_rem : Removes the proc file entry
* @dev - pointer to netdevice structre
* Return - void
* Description
* This functon is called when the driver is Unloaded. It checks if the S2IO dire* ctoy exists and deletes the files in the reverse order of creation.
*/

void s2io_bdsnmp_rem(struct net_device *dev)
{
        int nLength = 0;
        struct proc_dir_entry *S2ioDir;
        nLength = strlen(S2IODIRNAME);
        /* Check if the S2IO directory exists or not and then delete all the files in the S2IO Directory*/
       for(S2ioDir = proc_net->subdir; S2ioDir != NULL; S2ioDir = S2ioDir->next)
        {
                if((S2ioDir->namelen == nLength) &&
                                 (!memcmp(S2ioDir->name,S2IODIRNAME,nLength)))

                break;
        }
        if(S2ioDir == NULL)
                return;
        remove_proc_entry(BDFILENAME,S2ioDir);
        remove_proc_entry(PADAPFILENAME,S2ioDir);
        if(S2ioDir->subdir == NULL){
            remove_proc_entry(S2IODIRNAME,proc_net);
        }
}
#endif

                                                              
/*  To build the driver, 
gcc -D__KERNEL__ -DMODULE -I/usr/src/linux-2.4/include -Wall -Wstrict-prototypes -O2 -c s2io.c
*/
/*
 *$Log: s2io.c,v $
 *Revision 1.143.2.11  2004/06/29 07:01:49  arao
 *Bug:694
 *Fixed the flood ping problem reported in bug: 1017
 *
 *Revision 1.143.2.10  2004/06/24 02:21:28  dyusupov
 *Bug: 903
 *piors removed
 *new makefile to support cross-compiling
 *
 *Revision 1.143.2.9  2004/06/23 01:18:56  rvatsava
 *bug: 941
 *Proper Tx fifo pointer initialisation is required.
 *
 *Revision 1.143.2.8  2004/06/22 22:01:41  rvatsava
 *bug: 1017
 *Fixed the IBM platform specific issue. However, due to recent
 *changes in TX path flood ping is not working properly. So need to revisit
 *the TX related changes.
 *
 *Revision 1.143.2.7  2004/06/22 09:12:37  arao
 *Bug:902
 *The mac control structure is split into tx,rx and st in the MAC macro
 *
 *Revision 1.143.2.6  2004/06/22 05:54:57  arao
 *Bug:694
 *The txd logic has been kept in TXDBD macro.
 *
 *Revision 1.143.2.5  2004/06/18 12:01:18  rkoushik
 *Bug:1018
 *Fixed NAPI compiler error bug reported by HP. Also changed
 *"dev->weight" to 64 from 128. In s2io_set_link removed
 *the conditional check for queue_stopped as this was creating
 *a problem every time the cable is plugged out and plugged in
 *again.
 *Iam looking at any other changes that can be done towards
 *enhancing NAPI implementation. Will check in again if there
 *are any new favourable developments.
 *
 *-Koushik
 *
 *Revision 1.143.2.4  2004/06/14 09:21:08  arao
 *Bug:921
 *Added the __devexit_p macro for remove entry point and replaced __exit with __devexit macro for s2io_rem_nic function
 *
 *Revision 1.143.2.3  2004/06/11 12:34:14  arao
 *Bug:576
 *pci->driver_data not defined in the kernel 2.6 so replacing with pci_get_drvdata function
 *
 *Revision 1.143.2.2  2004/06/10 14:07:14  arao
 *Bug: 576
 *SNMP Support
 *
 *Revision 1.143.2.1  2004/06/08 10:19:02  rkoushik
 *Bug: 940
 * Made the changes to sync up the s2io_close function with
 *s2io_xmit and also the tasks scheduled by the driver which
 * could be running independently on a different CPU.
 *Also fixed the issue raised by bug # 867
 *(disabling relaxed ordering feature.).
 *
 *-Koushik
 *
 *Revision 1.143  2004/05/31 12:21:16  rkoushik
 *Bug: 986
 *
 *In this check in Iam making the fixes listed below,
 *1. Handling the PCC_FB_ECC_ERR interrupt as specified in the new UG.
 *2. Also queuing a task to reset the NIC when a serious Error is detected.
 *3. The rmac_err_reg is cleared immediately in the Intr handler itself instead of the queued task 's2io_set_link'.
 *
 *Koushik
 *
 *Revision 1.142  2004/05/24 12:29:39  rkoushik
 *Bug: 984
 * free_rx_buffer for 2buffer mode was clearing the END_OF_BLOCK
 *marker too, which resulted in this error. Also there was an
 *extra increment of a counter. Both these are fixed in this check-in.
 *
 *Koushik
 *
 *Revision 1.141  2004/05/20 12:38:37  rkoushik
 *Bug: 970
 *Macro was not declared properly in s2io.c and RxD_t structure for
 *2BUFF mode was declared incorrectly in s2io.h leading to problems.
 *Both rectified in this checkin.
 *
 *-Koushik
 *
 *Revision 1.140  2004/05/18 09:52:05  rkoushik
 *Bug: 935
 * Updating the 2Buff mode changes and fix for the new Link LED problem
 *into the CVS head.
 *
 *- Koushik
 *
 *Revision 1.139  2004/05/14 14:14:23  arao
 *Bug: 885
 *KNF standard for function names and comments are updated to generate Html document
 *
 *Revision 1.138  2004/05/14 00:59:56  dyusupov
 *Bug: 922
 *new fields been added to the ethtool_ops in 2.6.0 and 2.4.23.
 *Doing appropriate compile time check for this.
 *
 *Revision 1.137  2004/05/13 19:30:06  dyusupov
 *Bug: 943
 *
 *REL_1-7-2-3_LX becomes HEAD now.
 *
 *Revision 1.119  2004/04/22 02:12:30  rvatsava
 *bug:878
 *
 *Avoiding general Protection Fault when attempt to disable Xena while
 *running Nttcp.
 *
 *Revision 1.118  2004/04/22 01:45:48  rvatsava
 *bug:883
 *
 *Avoiding possible deadlock in s2io_close().
 *
 *Revision 1.117  2004/04/19 05:58:16  araju
 *Bug: 551
 *Fixed the performance Issue. there was a mistake in RTI programming
 *
 *Revision 1.116  2004/04/07 09:58:30  araju
 *Bug: 551
 *Loadable Parameters added.
 *
 *Revision 1.115  2004/04/07 00:04:07  aravi
 *Bug: 872
 *Set outstanding split transactions to 3 for Itanium and 2 for other systems.
 *
 *Revision 1.114  2004/04/06 23:52:16  aravi
 *Bug: 871
 *Added PCI read at the end of s2io_isr and s2io_xmit to flush previous writes.
 *Without this, on certain systems(SGI), it would cause "connection loss" on
 *transmit test.
 *
 *Revision 1.113  2004/04/06 18:27:19  aravi
 *Bug: 813
 *Fix for ftp failure(out of memory condition).
 *1. In s2io_tasklet(), clear bit before returning in case of mem alloc failure.
 *2. In rx_buffer_level(), change threshold for LOW from 128 to 16.
 *
 *Revision 1.112  2004/03/30 10:14:01  rkoushik
 *Bug: 840
 *Adding #endif in s2io_init_nic
 *Koushik
 *
 *Revision 1.111  2004/03/26 12:38:59  rkoushik
 *Bug: 832
 *  In this checkin, I have made a few cosmetic changes to the files
 *s2io.c and s2io.h with a view to minimize the diffs between the
 *files in the repository and those given to open source.
 *
 *-Koushik
 *
 *Revision 1.110  2004/03/19 06:41:10  rkoushik
 *Bug: 765
 *
 *	This checkin fixes the Multiple Link state displays
 *during link state change, whichwas  happening due to a very small
 *delay in the alarm Intr handler.
 *This checkin also addresses the Comment 13 of Jeffs latest set of comments
 *which provided a new way to identify the No TxD condition in s2io_xmit
 *routine. This was part of the Bug # 760, which will also be moved to fixed
 *state.
 *
 *Koushik
 *
 *Revision 1.109  2004/03/15 07:22:09  rkoushik
 *Bug: 765
 *To solve the multiple Link Down displays when the Nic's Link state
 *changes. further info in the bug.
 *
 *Koushik
 *
 *Revision 1.108  2004/03/15 05:34:23  rkoushik
 *Bug: 613
 *Removed the device capability to drop packets received with L/T field mismatch.
 *rmac_err_cfg register is no longer being configured to enable this feature.
 *
 *Koushik
 *
 *Revision 1.107  2004/03/12 04:48:26  araju
 *Bug: 755
 *set rx/tx chksum offload independently
 *
 *Revision 1.106  2004/03/11 11:57:18  rkoushik
 *Bug: 760
 *Has addressed most of the issues with a few exceptions, namely
 *Issue # 13 - Modifying the no_txd logic in s2io_xmit.
 *	I will add this by monday after some local testing.
 *
 *Issue # 15 - Does get_stats require locking?
 *	I don't think so, because we just reflect what ever the
 *	statistics block is reflecting at the current moment.
 *
 *Issue # 16 - Reformant the function header comments.
 *	Does not look like a priority issue. Will address this
 *	in the next patch along with issue # 20.
 *
 *Issue # 20 - Provide a ethtool patch for proper dumping registers and EEPROM.
 *	Will address this in the next submission patch.
 *
 *Koushik
 *
 *Revision 1.105  2004/03/03 13:32:24  rkoushik
 *Bug: 760
 *	This checkin addresses Bug 5 - 12 (Bug # 0 - 4 is valid only
 *for distribution code.)
 *
 *Koushik
 *
 *Revision 1.103  2004/02/27 14:37:36  rkoushik
 *Bug: 748
 *Driver submission comments given by Jeff,
 *details given in the bug.
 *
 *Koushik
 *
 *Revision 1.102  2004/02/17 00:40:03  aravi
 *Bug: 724
 *Removed inclusion of util.h
 *
 *Revision 1.101  2004/02/11 17:48:02  aravi
 *Bug: 669
 *A comment embedded inside a comment was causing compilation failure.
 *Fixed this.
 *
 *Revision 1.100  2004/02/11 03:00:06  aravi
 *Bug: 669
 *The source is modified to reflect the suggested changes with the bug 669.
 *A few comments which are not incorporated:
 *        *)      // Enable DTX_Control registers.
 *                write64(&bar0->dtx_control, 0x8000051500000000);
 *                udelay(50);
 *
 *                -> this is a loop in disguise.
 *        *)
 *                if(skb == NULL) {
 *                                DBG_PRINT(ERR_DBG,"%s: NULL skb ",dev->name);
 *                                DBG_PRINT(ERR_DBG,"in Tx Int\n");
 *                                spin_unlock(&nic->tx_lock);
 *
 *                -> just goto to the normal spin_unlock and avoid an
 *extra return
 *     *) #ifdef AS_A_MODULE
 *                MODULE_AUTHOR("Raghavendra Koushik <raghavendra.koushik@s2io.com>");
 *                MODULE_LICENSE("GPL"); MODULE_PARM(ring_num, "1-"
 *__MODULE_STRING(1) "i");
 *
 *Revision 1.99  2004/02/10 11:58:35  rkoushik
 *Bug: 668
 *Eliminated usage of self declared type 'dmaaddr_t' and also
 *eliminated the usage of PPC64_ARCH macro which was prevalent in the older code.
 *Further details in the bug.
 *
 *Koushik
 *
 *Revision 1.98  2004/02/09 10:31:34  rkoushik
 *Bug: 656
 * Made the changes suggested in Bug # 656.
 *
 *Koushik
 *
 *Revision 1.97  2004/02/07 02:16:17  gkotlyar
 *Bug: 682
 *OST and MMRBC fields of the PCI-X command registerd were overwritten
 *whenever we called s2io_reset().  In addition, we did not initialiaze the OST bits before writing into it.
 *
 *Revision 1.96  2004/02/05 06:08:21  rkoushik
 *Bug: 693
 *Added stop_queue & wake_queue in s2io_link and the watchdog timer
 *resets Nic only if the Link state is up. Details mentioned in Bug # 693.
 *
 *Koushik
 *
 *Revision 1.95  2004/02/04 04:52:35  rkoushik
 *Bug: 667
 * Indented the code using indent utility. Details of the options
 *used are specified in bug # 667
 *
 *Koushik
 *
 *Revision 1.94  2004/02/02 12:03:32  rkoushik
 *Bug: 643
 *The tx_pkt_ptr variable has been removed. Tx watchdog function now does
 *a s2io_close followed by s2io_open calls to reset and re-initialise NIC.
 *The Tx Intr scheme is made dependednt on the size of the Progammed FIFOs.
 *
 *-Koushik
 *
 *Revision 1.93  2004/01/29 05:41:24  rkoushik
 *Bug: 657
 *Loop back test is being removed from the driver as one of ethtool's test
 *option.
 *
 *Koushik
 *
 *Revision 1.92  2004/01/29 04:01:48  aravi
 *Bug: 639
 *Added code for activity and Link LED
 *
 *Revision 1.91  2004/01/28 05:57:36  rkoushik
 *Bug: 603
 * The Fix is under a #if 1 macro in the tx_intr_handler function.
 *Please verify using nttcp stress tests for long duration and confirm if
 *the fix works on all platforms. If it does I will rid the #if macro
 *and make it part of the mainstream code.
 *
 *Koushik
 *
 *Revision 1.90  2004/01/28 05:39:07  rkoushik
 *Bug: 520
 * The s2io_set_multicast function was corrected.
 *The Mac_cfg register was not being written after writing into its key register
 *hence the NIC was not going into promiscous mode. Also to set All_Multi mode
 *the RMAC's data0 and data1 registers were being incorrectly written.
 *Both mistakes were rectified.
 *
 *Koushik
 *
 *Revision 1.89  2004/01/23 12:08:29  rkoushik
 *Bug: 549
 *Added the beacon feature for new celestica cards using GPIO.
 *test it out using the ethtool utility on both
 *the new and old cards in both Link Up and Down states.
 *
 *Koushik
 *
 *Revision 1.88  2004/01/19 21:12:44  aravi
 *Bug: 599
 *Got rid of compilation error due to variable declaration after assignment.
 *
 *Bug: 593
 *Fixed Tx Link loss problem by
 *1. checking for put pointer not going beyond get pointer
 *2. set default tx descriptors to 4096( done in s2io.h)
 *3. Set rts_frm_len register to MTU size.
 *4. Corrected the length used for address unmapping in
 *    tx intr handler.
 *
 *Revision 1.87  2004/01/19 09:50:59  rkoushik
 *Bug: 598
 * Added GPL notices on the driver source files, namely
 *s2io.c, s2io.h and regs.h
 *
 *Koushik
 *
 *Revision 1.86  2004/01/19 05:21:57  rkoushik
 *Bug: 614
 *The XAUI configuration was being done using old values mistakenly.
 *The init_nic func was modified with the new values for XAUI configuration.
 *
 *-Koushik
 *
 *Revision 1.85  2004/01/13 13:13:05  rkoushik
 *Bug: 449
 * The driver source has been modified to follow most of the suggestion given
 *by the codingStyle document in the linux Documentation folder.
 *Also some coding errors identified by Steve Modica mentioned in bug # 536
 *have also been set right.
 *
 *Koushik
 *
 *Revision 1.84  2004/01/02 09:43:28  rkoushik
 *Bug: 581
 *Resetting Nic after performing RldRam test so as to remove RldRam from
 *Test Mode.
 *
 *-Koushik
 *
 *Revision 1.83  2004/01/01 00:19:46  aravi
 *Bug: 570
 *Fixed race condition in Transmit path.
 *
 *Revision 1.82  2003/12/30 13:03:14  rkoushik
 *Bug: 177
 *The driver has been updated with support for funtionalities in ethtool
 *version 1.8. Interrupt moderation has been skipped as the methodology to
 *set it using ethtool is different to our methodology.
 *
 *-Koushik
 *
 *Revision 1.81  2003/12/16 20:43:38  ukiran
 *Bug:542
 *Workaround to address TX FIFO full condition
 *
 *Revision 1.80  2003/12/15 23:27:47  ukiran
 *Bug: 536
 *Changed buffer replenishing algorithm. Initializing receive memory.
 *
 *Revision 1.79  2003/12/15 05:08:06  rkoushik
 *Bug: 516
 * The Fix is against the problem seen by Lawerence Livermore people.
 *Further details on the problem and the fix is available in
 *bug # 516 of bugtrak.
 *
 *-Koushik
 *
 *Revision 1.78  2003/12/02 19:56:48  ukiran
 *Bug:524
 *Fix for all "FFs" MAC address problems on HP/Alpha platforms
 *
 *Revision 1.77  2003/12/02 19:53:12  ukiran
 *Bug:510
 *Cleanup of 
 chars
 *
 *Revision 1.76  2003/11/19 02:23:02  ukiran
 *Bug:473
 *Fix to address link down condition with misbehaving switches
 *
 *Revision 1.75  2003/11/14 01:53:36  ukiran
 *Bug:493
 *pci_set_consistent_dma_mask() is supported in kernels >2.6.0-test7.
 *Need to figure out whether it will be backported to 2.4.xx kernels.
 *
 *Revision 1.74  2003/11/12 05:32:06  rkoushik
 *Bug: 493
 *Added a kernel version check around the pci_set_consistent_dma_mask
 *function as specified in the latest comment of Bug # 493
 *
 *-Koushik
 *
 *Revision 1.73  2003/11/08 02:28:56  ukiran
 *Bug:493
 *Made the fix suggested by the customer. Added pci_set_consistent_dma_mask() after pci_set_dma_mask(). This might help in resolving pci_alloc_consistent failures at SGI.
 *we cannot verify this problem in our lab. We will verify at SGI.
 *Most of the drivers in public domain are not invoking this function.
 *So this problem exists in their adapters. However, tigon driver has
 *a fix for it.
 *
 *-Uday
 *
 *Revision 1.72  2003/11/07 10:22:40  rkoushik
 *Bug: 492
 *Changed as per the info provided in Bug # 492.
 *
 *Revision 1.71  2003/11/04 02:06:56  ukiran
 *Bug:484
 *Enabling Logs in source code
 *
 */
