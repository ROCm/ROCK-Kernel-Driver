/******************************************************************************
 *
 * Name:        skethtool.c
 * Project:     GEnesis, PCI Gigabit Ethernet Adapter
 * Version:     $Revision: 1.3.2.5 $
 * Date:        $Date: 2004/12/07 15:16:12 $
 * Purpose:     All functions regarding ethtool handling
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2004 Marvell.
 *
 *	Driver for Marvell Yukon/2 chipset and SysKonnect Gigabit Ethernet 
 *      Server Adapters.
 *
 *	Author: Ralph Roesler (rroesler@syskonnect.de)
 *	        Mirko Lindner (mlindner@syskonnect.de)
 *
 *	Address all question to: linux@syskonnect.de
 *
 *	The technical manual for the adapters is available from SysKonnect's
 *	web pages: www.syskonnect.com
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 *****************************************************************************/

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"
#include "h/skversion.h"
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/timer.h>

/******************************************************************************
 *
 * External Functions and Data
 *
 *****************************************************************************/

extern void SkDimDisableModeration(SK_AC *pAC, int CurrentModeration);
extern void SkDimEnableModerationIfNeeded(SK_AC *pAC);

/******************************************************************************
 *
 * Defines
 *
 *****************************************************************************/

#ifndef ETHT_STATSTRING_LEN
#define ETHT_STATSTRING_LEN 32
#endif

#define SK98LIN_STAT(m)	sizeof(((SK_AC *)0)->m),offsetof(SK_AC, m)

#define SUPP_COPPER_ALL (SUPPORTED_10baseT_Half  | SUPPORTED_10baseT_Full  | \
                         SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full | \
                         SUPPORTED_1000baseT_Half| SUPPORTED_1000baseT_Full| \
                         SUPPORTED_TP)

#define ADV_COPPER_ALL  (ADVERTISED_10baseT_Half  | ADVERTISED_10baseT_Full  | \
                         ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full | \
                         ADVERTISED_1000baseT_Half| ADVERTISED_1000baseT_Full| \
                         ADVERTISED_TP)

#define SUPP_FIBRE_ALL  (SUPPORTED_1000baseT_Full | \
                         SUPPORTED_FIBRE          | \
                         SUPPORTED_Autoneg)

#define ADV_FIBRE_ALL   (ADVERTISED_1000baseT_Full | \
                         ADVERTISED_FIBRE          | \
                         ADVERTISED_Autoneg)

/******************************************************************************
 *
 * Local Function Prototypes
 *
 *****************************************************************************/

#ifdef ETHTOOL_GSET
static void getSettings(SK_AC *pAC, int port, struct ethtool_cmd *ecmd);
#endif
#ifdef ETHTOOL_SSET
static int setSettings(SK_AC *pAC, int port, struct ethtool_cmd *ecmd);
#endif
#ifdef ETHTOOL_GPAUSEPARAM
static void getPauseParams(SK_AC *pAC, int port, struct ethtool_pauseparam *epause);
#endif
#ifdef ETHTOOL_SPAUSEPARAM
static int setPauseParams(SK_AC *pAC, int port, struct ethtool_pauseparam *epause);
#endif
#ifdef ETHTOOL_GDRVINFO
static void getDriverInfo(SK_AC *pAC, int port, struct ethtool_drvinfo *edrvinfo);
#endif
#ifdef ETHTOOL_PHYS_ID
static int startLocateNIC(SK_AC *pAC, int port, struct ethtool_value *blinkSecs);
static void toggleLeds(unsigned long ptr);
#endif
#ifdef ETHTOOL_GCOALESCE
static void getModerationParams(SK_AC *pAC, int port, struct ethtool_coalesce *ecoalesc);
#endif
#ifdef ETHTOOL_SCOALESCE
static int setModerationParams(SK_AC *pAC, int port, struct ethtool_coalesce *ecoalesc);
#endif
#ifdef ETHTOOL_GWOL
static void getWOLsettings(SK_AC *pAC, int port, struct ethtool_wolinfo *ewol);
#endif
#ifdef ETHTOOL_SWOL
static int setWOLsettings(SK_AC *pAC, int port, struct ethtool_wolinfo *ewol);
#endif

static int getPortNumber(struct net_device *netdev, struct ifreq *ifr);

/******************************************************************************
 *
 * Local Variables
 *
 *****************************************************************************/

struct sk98lin_stats {
	char stat_string[ETHT_STATSTRING_LEN];
	int  sizeof_stat;
	int  stat_offset;
};

static struct sk98lin_stats sk98lin_etht_stats_port0[] = {
	{ "rx_packets" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxOkCts) },
	{ "tx_packets" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxOkCts) },
	{ "rx_bytes" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxOctetsOkCts) },
	{ "tx_bytes" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxOctetsOkCts) },
	{ "rx_errors" , SK98LIN_STAT(PnmiStruct.InErrorsCts) },
	{ "tx_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxSingleCollisionCts) },
	{ "rx_dropped" , SK98LIN_STAT(PnmiStruct.RxNoBufCts) },
	{ "tx_dropped" , SK98LIN_STAT(PnmiStruct.TxNoBufCts) },
	{ "multicasts" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxMulticastOkCts) },
	{ "collisions" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxSingleCollisionCts) },
	{ "rx_length_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxRuntCts) },
	{ "rx_buffer_overflow_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxFifoOverflowCts) },
	{ "rx_crc_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxFcsCts) },
	{ "rx_frame_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxFramingCts) },
	{ "rx_too_short_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxShortsCts) },
	{ "rx_too_long_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxTooLongCts) },
	{ "rx_carrier_extension_errors", SK98LIN_STAT(PnmiStruct.Stat[0].StatRxCextCts) },
	{ "rx_symbol_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxSymbolCts) },
	{ "rx_llc_mac_size_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxIRLengthCts) },
	{ "rx_carrier_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxCarrierCts) },
	{ "rx_jabber_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxJabberCts) },
	{ "rx_missed_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatRxMissedCts) },
	{ "tx_abort_collision_errors" , SK98LIN_STAT(stats.tx_aborted_errors) },
	{ "tx_carrier_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxCarrierCts) },
	{ "tx_buffer_underrun_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxFifoUnderrunCts) },
	{ "tx_heartbeat_errors" , SK98LIN_STAT(PnmiStruct.Stat[0].StatTxCarrierCts) } ,
	{ "tx_window_errors" , SK98LIN_STAT(stats.tx_window_errors) }
};

static struct sk98lin_stats sk98lin_etht_stats_port1[] = {
	{ "rx_packets" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxOkCts) },
	{ "tx_packets" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxOkCts) },
	{ "rx_bytes" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxOctetsOkCts) },
	{ "tx_bytes" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxOctetsOkCts) },
	{ "rx_errors" , SK98LIN_STAT(PnmiStruct.InErrorsCts) },
	{ "tx_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxSingleCollisionCts) },
	{ "rx_dropped" , SK98LIN_STAT(PnmiStruct.RxNoBufCts) },
	{ "tx_dropped" , SK98LIN_STAT(PnmiStruct.TxNoBufCts) },
	{ "multicasts" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxMulticastOkCts) },
	{ "collisions" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxSingleCollisionCts) },
	{ "rx_length_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxRuntCts) },
	{ "rx_buffer_overflow_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxFifoOverflowCts) },
	{ "rx_crc_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxFcsCts) },
	{ "rx_frame_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxFramingCts) },
	{ "rx_too_short_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxShortsCts) },
	{ "rx_too_long_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxTooLongCts) },
	{ "rx_carrier_extension_errors", SK98LIN_STAT(PnmiStruct.Stat[1].StatRxCextCts) },
	{ "rx_symbol_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxSymbolCts) },
	{ "rx_llc_mac_size_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxIRLengthCts) },
	{ "rx_carrier_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxCarrierCts) },
	{ "rx_jabber_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxJabberCts) },
	{ "rx_missed_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatRxMissedCts) },
	{ "tx_abort_collision_errors" , SK98LIN_STAT(stats.tx_aborted_errors) },
	{ "tx_carrier_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxCarrierCts) },
	{ "tx_buffer_underrun_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxFifoUnderrunCts) },
	{ "tx_heartbeat_errors" , SK98LIN_STAT(PnmiStruct.Stat[1].StatTxCarrierCts) } ,
	{ "tx_window_errors" , SK98LIN_STAT(stats.tx_window_errors) }
};

#define SK98LIN_STATS_LEN sizeof(sk98lin_etht_stats_port0) / sizeof(struct sk98lin_stats)

static int nbrBlinkQuarterSeconds;
static int currentPortIndex;
static SK_BOOL isLocateNICrunning   = SK_FALSE;
static SK_BOOL isDualNetCard        = SK_FALSE;
static SK_BOOL doSwitchLEDsOn       = SK_FALSE;
static SK_BOOL boardWasDown[2]      = { SK_FALSE, SK_FALSE };
static struct timer_list locateNICtimer;

/******************************************************************************
 *
 * Global Functions
 *
 *****************************************************************************/

/*****************************************************************************
 *
 * 	SkEthIoctl - IOCTL entry point for all ethtool queries
 *
 * Description:
 *	Any IOCTL request that has to deal with the ethtool command tool is
 *	dispatched via this function.
 *
 * Returns:
 *	==0:	everything fine, no error
 *	!=0:	the return value is the error code of the failure 
 */
int SkEthIoctl(
struct net_device *netdev,  /* the pointer to netdev structure       */
struct ifreq      *ifr)     /* what interface the request refers to? */
{
	DEV_NET             *pNet        = (DEV_NET*) netdev->priv;
	SK_AC               *pAC         = pNet->pAC;
	void                *pAddr       = ifr->ifr_data;
	int                  port        = getPortNumber(netdev, ifr);
	SK_PNMI_STRUCT_DATA *pPnmiStruct = &pAC->PnmiStruct;
	SK_U32               Size        = sizeof(SK_PNMI_STRUCT_DATA);
	SK_U32               cmd;
	struct sk98lin_stats *sk98lin_etht_stats = 
		(port == 0) ? sk98lin_etht_stats_port0 : sk98lin_etht_stats_port1;

        if (get_user(cmd, (uint32_t *) pAddr)) {
                return -EFAULT;
	}

	switch(cmd) {
#ifdef ETHTOOL_GSET
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = { ETHTOOL_GSET };
		getSettings(pAC, port, &ecmd);
		if(copy_to_user(pAddr, &ecmd, sizeof(ecmd))) {
			return -EFAULT;
		}
		return 0;
	}
	break;
#endif
#ifdef ETHTOOL_SSET
	case ETHTOOL_SSET: {
		struct ethtool_cmd ecmd;
		if(copy_from_user(&ecmd, pAddr, sizeof(ecmd))) {
			return -EFAULT;
		}
		return setSettings(pAC, port, &ecmd);
	}
	break;
#endif
#ifdef ETHTOOL_GDRVINFO
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo drvinfo = { ETHTOOL_GDRVINFO };
		getDriverInfo(pAC, port, &drvinfo);
		if(copy_to_user(pAddr, &drvinfo, sizeof(drvinfo))) {
			return -EFAULT;
		}
		return 0;
	}
	break;
#endif
#ifdef ETHTOOL_GSTRINGS
	case ETHTOOL_GSTRINGS: {
		struct ethtool_gstrings gstrings = { ETHTOOL_GSTRINGS };
		char *strings = NULL;
		int err = 0;
		if(copy_from_user(&gstrings, pAddr, sizeof(gstrings))) {
			return -EFAULT;
		}
		switch(gstrings.string_set) {
#ifdef ETHTOOL_GSTATS
			case ETH_SS_STATS: {
				int i;
				gstrings.len = SK98LIN_STATS_LEN;
				if ((strings = kmalloc(SK98LIN_STATS_LEN*ETHT_STATSTRING_LEN,GFP_KERNEL)) == NULL) {
					return -ENOMEM;
				}
				for(i=0; i < SK98LIN_STATS_LEN; i++) {
					memcpy(&strings[i * ETHT_STATSTRING_LEN],
						&(sk98lin_etht_stats[i].stat_string),
						ETHT_STATSTRING_LEN);
				}
			}
			break;
#endif
			default:
				return -EOPNOTSUPP;
		}
		if(copy_to_user(pAddr, &gstrings, sizeof(gstrings))) {
			err = -EFAULT;
		}
		pAddr = (void *) ((unsigned long int) pAddr + offsetof(struct ethtool_gstrings, data));
		if(!err && copy_to_user(pAddr, strings, gstrings.len * ETH_GSTRING_LEN)) {
			err = -EFAULT;
		}
		kfree(strings);
		return err;
	}
#endif
#ifdef ETHTOOL_GSTATS
	case ETHTOOL_GSTATS: {
		struct {
			struct ethtool_stats eth_stats;
			uint64_t data[SK98LIN_STATS_LEN];
		} stats = { {ETHTOOL_GSTATS, SK98LIN_STATS_LEN} };
		int i;

		if (netif_running(pAC->dev[port])) {
			SkPnmiGetStruct(pAC, pAC->IoBase, pPnmiStruct, &Size, port);
		}
		for(i = 0; i < SK98LIN_STATS_LEN; i++) {
			if (netif_running(pAC->dev[port])) {
				stats.data[i] = (sk98lin_etht_stats[i].sizeof_stat ==
					sizeof(uint64_t)) ?
					*(uint64_t *)((char *)pAC +
						sk98lin_etht_stats[i].stat_offset) :
					*(uint32_t *)((char *)pAC +
						sk98lin_etht_stats[i].stat_offset);
			} else {
				stats.data[i] = (sk98lin_etht_stats[i].sizeof_stat ==
					sizeof(uint64_t)) ? (uint64_t) 0 : (uint32_t) 0;
			}
		}
		if(copy_to_user(pAddr, &stats, sizeof(stats))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_PHYS_ID
	case ETHTOOL_PHYS_ID: {
		struct ethtool_value blinkSecs;
		if(copy_from_user(&blinkSecs, pAddr, sizeof(blinkSecs))) {
			return -EFAULT;
		}
		return startLocateNIC(pAC, port, &blinkSecs);
	}
#endif
#ifdef ETHTOOL_GPAUSEPARAM
	case ETHTOOL_GPAUSEPARAM: {
		struct ethtool_pauseparam epause = { ETHTOOL_GPAUSEPARAM };
		getPauseParams(pAC, port, &epause);
		if(copy_to_user(pAddr, &epause, sizeof(epause))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_SPAUSEPARAM
	case ETHTOOL_SPAUSEPARAM: {
		struct ethtool_pauseparam epause;
		if(copy_from_user(&epause, pAddr, sizeof(epause))) {
			return -EFAULT;
		}
		return setPauseParams(pAC, port, &epause);
	}
#endif
#ifdef ETHTOOL_GSG
	case ETHTOOL_GSG: {
		struct ethtool_value edata = { ETHTOOL_GSG };
		edata.data = (netdev->features & NETIF_F_SG) != 0;
		if (copy_to_user(pAddr, &edata, sizeof(edata))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_SSG
	case ETHTOOL_SSG: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, pAddr, sizeof(edata))) {
                        return -EFAULT;
		}
		if (pAC->ChipsetType) { /* Don't handle if Genesis */
			if (edata.data) {
				netdev->features |= NETIF_F_SG;
			} else {
				netdev->features &= ~NETIF_F_SG;
			}
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_GRXCSUM
	case ETHTOOL_GRXCSUM: {
		struct ethtool_value edata = { ETHTOOL_GRXCSUM };
		edata.data = pAC->RxPort[port].UseRxCsum;
		if (copy_to_user(pAddr, &edata, sizeof(edata))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_SRXCSUM
	case ETHTOOL_SRXCSUM: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, pAddr, sizeof(edata))) {
			return -EFAULT;
		}
		pAC->RxPort[port].UseRxCsum = edata.data;
                return 0;
	}
#endif
#ifdef ETHTOOL_GTXCSUM
	case ETHTOOL_GTXCSUM: {
		struct ethtool_value edata = { ETHTOOL_GTXCSUM };
		edata.data = ((netdev->features & NETIF_F_IP_CSUM) != 0);
		if (copy_to_user(pAddr, &edata, sizeof(edata))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_STXCSUM
	case ETHTOOL_STXCSUM: {
		struct ethtool_value edata;
		if (copy_from_user(&edata, pAddr, sizeof(edata))) {
			return -EFAULT;
		}
		if (pAC->ChipsetType) { /* Don't handle if Genesis */
			if (edata.data) {
				netdev->features |= NETIF_F_IP_CSUM;
			} else {
				netdev->features &= ~NETIF_F_IP_CSUM;
			}
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_NWAY_RST
	case ETHTOOL_NWAY_RST: {
		if(netif_running(netdev)) {
			(*netdev->stop)(netdev);
			(*netdev->open)(netdev);
		}
		return 0;
	}
#endif
#ifdef NETIF_F_TSO
#ifdef ETHTOOL_GTSO
	case ETHTOOL_GTSO: {
		struct ethtool_value edata = { ETHTOOL_GTSO };
		edata.data = (netdev->features & NETIF_F_TSO) != 0;
		if (copy_to_user(pAddr, &edata, sizeof(edata))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_STSO
	case ETHTOOL_STSO: {
		struct ethtool_value edata;
		if (CHIP_ID_YUKON_2(pAC)) {
			if (copy_from_user(&edata, pAddr, sizeof(edata))) {
				return -EFAULT;
			}
			if (edata.data) {
				netdev->features |= NETIF_F_TSO;
			} else {
				netdev->features &= ~NETIF_F_TSO;
			}
			return 0;
		}
                return -EOPNOTSUPP;
	}
#endif
#endif
#ifdef ETHTOOL_GCOALESCE
	case ETHTOOL_GCOALESCE: {
		struct ethtool_coalesce ecoalesc = { ETHTOOL_GCOALESCE };
		getModerationParams(pAC, port, &ecoalesc);
		if(copy_to_user(pAddr, &ecoalesc, sizeof(ecoalesc))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_SCOALESCE
	case ETHTOOL_SCOALESCE: {
		struct ethtool_coalesce ecoalesc;
		if(copy_from_user(&ecoalesc, pAddr, sizeof(ecoalesc))) {
			return -EFAULT;
		}
		return setModerationParams(pAC, port, &ecoalesc);
	}
#endif
#ifdef ETHTOOL_GWOL
	case ETHTOOL_GWOL: {
		struct ethtool_wolinfo ewol = { ETHTOOL_GWOL };
		getWOLsettings(pAC, port, &ewol);
		if(copy_to_user(pAddr, &ewol, sizeof(ewol))) {
			return -EFAULT;
		}
		return 0;
	}
#endif
#ifdef ETHTOOL_SWOL
	case ETHTOOL_SWOL: {
		struct ethtool_wolinfo ewol;
		if(copy_from_user(&ewol, pAddr, sizeof(ewol))) {
			return -EFAULT;
		}
		return setWOLsettings(pAC, port, &ewol);
	}
#endif
        default:
                return -EOPNOTSUPP;
        }
} /* SkEthIoctl() */

/******************************************************************************
 *
 * Local Functions
 *
 *****************************************************************************/

#ifdef ETHTOOL_GSET
/*****************************************************************************
 *
 * 	getSettings - retrieves the current settings of the selected adapter
 *
 * Description:
 *	The current configuration of the selected adapter is returned.
 *	This configuration involves a)speed, b)duplex and c)autoneg plus
 *	a number of other variables.
 *
 * Returns:	N/A
 *
 */
static void getSettings(
SK_AC              *pAC,  /* pointer to adapter control context      */
int                 port, /* the port of the selected adapter        */
struct ethtool_cmd *ecmd) /* mandatory command structure for results */
{
	SK_GEPORT *pPort = &pAC->GIni.GP[port];

	static int DuplexAutoNegConfMap[9][3]= {
		{ -1                     , -1         , -1              },
		{ 0                      , -1         , -1              },
		{ SK_LMODE_HALF          , DUPLEX_HALF, AUTONEG_DISABLE },
		{ SK_LMODE_FULL          , DUPLEX_FULL, AUTONEG_DISABLE },
		{ SK_LMODE_AUTOHALF      , DUPLEX_HALF, AUTONEG_ENABLE  },
		{ SK_LMODE_AUTOFULL      , DUPLEX_FULL, AUTONEG_ENABLE  },
		{ SK_LMODE_AUTOBOTH      , DUPLEX_FULL, AUTONEG_ENABLE  },
		{ SK_LMODE_AUTOSENSE     , -1         , -1              },
		{ SK_LMODE_INDETERMINATED, -1         , -1              }
	};

	static int SpeedConfMap[6][2] = {
		{ 0                       , -1         },
		{ SK_LSPEED_AUTO          , -1         },
		{ SK_LSPEED_10MBPS        , SPEED_10   },
		{ SK_LSPEED_100MBPS       , SPEED_100  },
		{ SK_LSPEED_1000MBPS      , SPEED_1000 },
		{ SK_LSPEED_INDETERMINATED, -1         }
	};

	static int AdvSpeedMap[6][2] = {
		{ 0                       , -1         },
		{ SK_LSPEED_AUTO          , -1         },
		{ SK_LSPEED_10MBPS        , ADVERTISED_10baseT_Half   | ADVERTISED_10baseT_Full },
		{ SK_LSPEED_100MBPS       , ADVERTISED_100baseT_Half  | ADVERTISED_100baseT_Full },
		{ SK_LSPEED_1000MBPS      , ADVERTISED_1000baseT_Half | ADVERTISED_1000baseT_Full},
		{ SK_LSPEED_INDETERMINATED, -1         }
	};

	ecmd->phy_address = port;
	ecmd->speed       = SpeedConfMap[pPort->PLinkSpeedUsed][1];
	ecmd->duplex      = DuplexAutoNegConfMap[pPort->PLinkModeStatus][1];
	ecmd->autoneg     = DuplexAutoNegConfMap[pPort->PLinkModeStatus][2];
	ecmd->transceiver = XCVR_INTERNAL;

	if (pAC->GIni.GICopperType) {
		ecmd->port        = PORT_TP;
		ecmd->supported   = (SUPP_COPPER_ALL|SUPPORTED_Autoneg);
		if (pAC->GIni.GIGenesis) {
			ecmd->supported &= ~(SUPPORTED_10baseT_Half);
			ecmd->supported &= ~(SUPPORTED_10baseT_Full);
			ecmd->supported &= ~(SUPPORTED_100baseT_Half);
			ecmd->supported &= ~(SUPPORTED_100baseT_Full);
		} else {
			if (pAC->GIni.GIChipId == CHIP_ID_YUKON) {
				ecmd->supported &= ~(SUPPORTED_1000baseT_Half);
			} 
			if (pAC->GIni.GIChipId == CHIP_ID_YUKON_FE) {
				ecmd->supported &= ~(SUPPORTED_1000baseT_Half);
				ecmd->supported &= ~(SUPPORTED_1000baseT_Full);
			}
		}
		if (pAC->GIni.GP[0].PLinkSpeed != SK_LSPEED_AUTO) {
			ecmd->advertising = AdvSpeedMap[pPort->PLinkSpeed][1];
			if (pAC->GIni.GIChipId == CHIP_ID_YUKON) {
				ecmd->advertising &= ~(SUPPORTED_1000baseT_Half);
			} 
		} else {
			ecmd->advertising = ecmd->supported;
		}
		if (ecmd->autoneg == AUTONEG_ENABLE) {
			ecmd->advertising |= ADVERTISED_Autoneg;
		} 
	} else {
		ecmd->port        = PORT_FIBRE;
		ecmd->supported   = (SUPP_FIBRE_ALL);
		ecmd->advertising = (ADV_FIBRE_ALL);
	}
}
#endif

#ifdef ETHTOOL_SSET
/*****************************************************************************
 *
 *	setSettings - configures the settings of a selected adapter
 *
 * Description:
 *	Possible settings that may be altered are a)speed, b)duplex or 
 *	c)autonegotiation.
 *
 * Returns:
 *	==0:	everything fine, no error
 *	!=0:	the return value is the error code of the failure 
 */
static int setSettings(
SK_AC              *pAC,  /* pointer to adapter control context    */
int                 port, /* the port of the selected adapter      */
struct ethtool_cmd *ecmd) /* command structure containing settings */
{
	DEV_NET     *pNet  = (DEV_NET *) pAC->dev[port]->priv;
	SK_U32       Instance;
	char         Buf[4];
	unsigned int Len = 1;
	int Ret;

	if (port == 0) {
		Instance = (pAC->RlmtNets == 2) ? 1 : 2;
	} else {
		Instance = (pAC->RlmtNets == 2) ? 2 : 3;
	}

	if (((ecmd->autoneg == AUTONEG_DISABLE) || (ecmd->autoneg == AUTONEG_ENABLE)) &&
	    ((ecmd->duplex == DUPLEX_FULL) || (ecmd->duplex == DUPLEX_HALF))) {
		if (ecmd->autoneg == AUTONEG_DISABLE) {
			if (ecmd->duplex == DUPLEX_FULL) { 
				*Buf = (char) SK_LMODE_FULL;
			} else {
				*Buf = (char) SK_LMODE_HALF;
			}
		} else {
			if (ecmd->duplex == DUPLEX_FULL) { 
				*Buf = (char) SK_LMODE_AUTOFULL;
			} else {
				*Buf = (char) SK_LMODE_AUTOHALF;
			}
		}

		Ret = SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_LINK_MODE, 
					&Buf, &Len, Instance, pNet->NetNr);
	
		if (Ret != SK_PNMI_ERR_OK) {
			return -EINVAL;
		}
	}

	if ((ecmd->speed == SPEED_1000) ||
	    (ecmd->speed == SPEED_100)  || 
	    (ecmd->speed == SPEED_10)) {
		if (ecmd->speed == SPEED_1000) {
			*Buf = (char) SK_LSPEED_1000MBPS;
		} else if (ecmd->speed == SPEED_100) {
			*Buf = (char) SK_LSPEED_100MBPS;
		} else {
			*Buf = (char) SK_LSPEED_10MBPS;
		}

		Ret = SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_SPEED_MODE, 
					&Buf, &Len, Instance, pNet->NetNr);
	
		if (Ret != SK_PNMI_ERR_OK) {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}
	return 0;
}
#endif

#ifdef ETHTOOL_GPAUSEPARAM
/*****************************************************************************
 *
 * 	getPauseParams - retrieves the pause parameters
 *
 * Description:
 *	All current pause parameters of a selected adapter are placed 
 *	in the passed ethtool_pauseparam structure and are returned.
 *
 * Returns:	N/A
 *
 */
static void getPauseParams(
SK_AC                     *pAC,    /* pointer to adapter control context */
int                        port,   /* the port of the selected adapter   */
struct ethtool_pauseparam *epause) /* pause parameter struct for result  */
{
	SK_GEPORT *pPort            = &pAC->GIni.GP[port];

	epause->rx_pause = 0;
	epause->tx_pause = 0;

	if (pPort->PFlowCtrlMode == SK_FLOW_MODE_LOC_SEND) {
		epause->tx_pause = 1;
	} 
	if ((pPort->PFlowCtrlMode == SK_FLOW_MODE_SYMMETRIC) ||
	    (pPort->PFlowCtrlMode == SK_FLOW_MODE_SYM_OR_REM)) {
		epause->tx_pause = 1;
		epause->rx_pause = 1;
	}

	if ((epause->rx_pause == 0) && (epause->tx_pause == 0)) {
		epause->autoneg = SK_FALSE;
	} else {
		epause->autoneg = SK_TRUE;
	}
}
#endif

#ifdef ETHTOOL_SPAUSEPARAM
/*****************************************************************************
 *
 *	setPauseParams - configures the pause parameters of an adapter
 *
 * Description:
 *	This function sets the Rx or Tx pause parameters 
 *
 * Returns:
 *	==0:	everything fine, no error
 *	!=0:	the return value is the error code of the failure 
 */
static int setPauseParams(
SK_AC                     *pAC,    /* pointer to adapter control context */
int                        port,   /* the port of the selected adapter   */
struct ethtool_pauseparam *epause) /* pause parameter struct with params */
{
	SK_GEPORT *pPort            = &pAC->GIni.GP[port];
	DEV_NET   *pNet             = (DEV_NET *) pAC->dev[port]->priv;
	int        PrevSpeedVal     = pPort->PLinkSpeedUsed;

	SK_U32         Instance;
	char           Buf[4];
	int            Ret;
	SK_BOOL        prevAutonegValue = SK_TRUE;
	int            prevTxPause      = 0;
	int            prevRxPause      = 0;
	unsigned int   Len              = 1;

        if (port == 0) {
                Instance = (pAC->RlmtNets == 2) ? 1 : 2;
        } else {
                Instance = (pAC->RlmtNets == 2) ? 2 : 3;
        }

	/*
	** we have to determine the current settings to see if 
	** the operator requested any modification of the flow 
	** control parameters...
	*/
	if (pPort->PFlowCtrlMode == SK_FLOW_MODE_LOC_SEND) {
		prevTxPause = 1;
	} 
	if ((pPort->PFlowCtrlMode == SK_FLOW_MODE_SYMMETRIC) ||
	    (pPort->PFlowCtrlMode == SK_FLOW_MODE_SYM_OR_REM)) {
		prevTxPause = 1;
		prevRxPause = 1;
	}

	if ((prevRxPause == 0) && (prevTxPause == 0)) {
		prevAutonegValue = SK_FALSE;
	}


	/*
	** perform modifications regarding the changes 
	** requested by the operator
	*/
	if (epause->autoneg != prevAutonegValue) {
		if (epause->autoneg == AUTONEG_DISABLE) {
			*Buf = (char) SK_FLOW_MODE_NONE;
		} else {
			*Buf = (char) SK_FLOW_MODE_SYMMETRIC;
		}
	} else {
		if(epause->rx_pause && epause->tx_pause) {
			*Buf = (char) SK_FLOW_MODE_SYMMETRIC;
		} else if (epause->rx_pause && !epause->tx_pause) {
			*Buf = (char) SK_FLOW_MODE_SYM_OR_REM;
		} else if(!epause->rx_pause && epause->tx_pause) {
			*Buf = (char) SK_FLOW_MODE_LOC_SEND;
		} else {
			*Buf = (char) SK_FLOW_MODE_NONE;
		}
	}

	Ret = SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_FLOWCTRL_MODE,
			&Buf, &Len, Instance, pNet->NetNr);

	if (Ret != SK_PNMI_ERR_OK) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_CTRL,
		("ethtool (sk98lin): error changing rx/tx pause (%i)\n", Ret));
	}  else {
		Len = 1; /* set buffer length to correct value */
	}

	/*
	** It may be that autoneg has been disabled! Therefore
	** set the speed to the previously used value...
	*/
	*Buf = (char) PrevSpeedVal;

	Ret = SkPnmiSetVar(pAC, pAC->IoBase, OID_SKGE_SPEED_MODE, 
			&Buf, &Len, Instance, pNet->NetNr);

	if (Ret != SK_PNMI_ERR_OK) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_CTRL,
		("ethtool (sk98lin): error setting speed (%i)\n", Ret));
	}
        return 0;
}
#endif

#ifdef ETHTOOL_GCOALESCE
/*****************************************************************************
 *
 * 	getModerationParams - retrieves the IRQ moderation settings 
 *
 * Description:
 *	All current IRQ moderation settings of a selected adapter are placed 
 *	in the passed ethtool_coalesce structure and are returned.
 *
 * Returns:	N/A
 *
 */
static void getModerationParams(
SK_AC                   *pAC,      /* pointer to adapter control context */
int                      port,     /* the port of the selected adapter   */
struct ethtool_coalesce *ecoalesc) /* IRQ moderation struct for results  */
{
	DIM_INFO *Info = &pAC->DynIrqModInfo;
	SK_BOOL UseTxIrqModeration = SK_FALSE;
	SK_BOOL UseRxIrqModeration = SK_FALSE;

	if (Info->IntModTypeSelect != C_INT_MOD_NONE) {
		if (CHIP_ID_YUKON_2(pAC)) {
			UseRxIrqModeration = SK_TRUE;
			UseTxIrqModeration = SK_TRUE;
		} else {
			if ((Info->MaskIrqModeration == IRQ_MASK_RX_ONLY) ||
			    (Info->MaskIrqModeration == IRQ_MASK_SP_RX)   ||
			    (Info->MaskIrqModeration == IRQ_MASK_RX_TX_SP)) {
				UseRxIrqModeration = SK_TRUE;
			}
			if ((Info->MaskIrqModeration == IRQ_MASK_TX_ONLY) ||
			    (Info->MaskIrqModeration == IRQ_MASK_SP_TX)   ||
			    (Info->MaskIrqModeration == IRQ_MASK_RX_TX_SP)) {
				UseTxIrqModeration = SK_TRUE;
			}
		}

		if (UseRxIrqModeration) {
			ecoalesc->rx_coalesce_usecs = 1000000 / Info->MaxModIntsPerSec;
		}
		if (UseTxIrqModeration) {
			ecoalesc->tx_coalesce_usecs = 1000000 / Info->MaxModIntsPerSec;
		}
		if (Info->IntModTypeSelect == C_INT_MOD_DYNAMIC) {
			ecoalesc->rate_sample_interval = Info->DynIrqModSampleInterval; 
			if (UseRxIrqModeration) {
				ecoalesc->use_adaptive_rx_coalesce = 1;
				ecoalesc->rx_coalesce_usecs_low = 
					1000000 / Info->MaxModIntsPerSecLowerLimit;
				ecoalesc->rx_coalesce_usecs_high = 
					1000000 / Info->MaxModIntsPerSecUpperLimit;
			}
			if (UseTxIrqModeration) {
				ecoalesc->use_adaptive_tx_coalesce = 1;
				ecoalesc->tx_coalesce_usecs_low = 
					1000000 / Info->MaxModIntsPerSecLowerLimit;
				ecoalesc->tx_coalesce_usecs_high = 
					1000000 / Info->MaxModIntsPerSecUpperLimit;
			}
		}
	}
}
#endif

#ifdef ETHTOOL_SCOALESCE
/*****************************************************************************
 *
 *	setModerationParams - configures the IRQ moderation of an adapter
 *
 * Description:
 *	Depending on the desired IRQ moderation parameters, either a) static,
 *	b) dynamic or c) no moderation is configured. 
 *
 * Returns:
 *	==0:	everything fine, no error
 *	!=0:	the return value is the error code of the failure 
 *
 * Notes:
 *	The supported timeframe for the coalesced interrupts ranges from
 *	33.333us (30 IntsPerSec) down to 25us (40.000 IntsPerSec).
 *	Any requested value that is not in this range will abort the request!
 */
static int setModerationParams(
SK_AC                   *pAC,      /* pointer to adapter control context */
int                      port,     /* the port of the selected adapter   */
struct ethtool_coalesce *ecoalesc) /* IRQ moderation struct with params  */
{
	DIM_INFO  *Info             = &pAC->DynIrqModInfo;
	int        PrevModeration   = Info->IntModTypeSelect;

	Info->IntModTypeSelect = C_INT_MOD_NONE; /* initial default */

	if ((ecoalesc->rx_coalesce_usecs) || (ecoalesc->tx_coalesce_usecs)) {
		if (ecoalesc->rx_coalesce_usecs) {
			if ((ecoalesc->rx_coalesce_usecs < 25) ||
			    (ecoalesc->rx_coalesce_usecs > 33333)) {
				return -EINVAL; 
			}
		}
		if (ecoalesc->tx_coalesce_usecs) {
			if ((ecoalesc->tx_coalesce_usecs < 25) ||
			    (ecoalesc->tx_coalesce_usecs > 33333)) {
				return -EINVAL; 
			}
		}
		if (!CHIP_ID_YUKON_2(pAC)) {
			if ((Info->MaskIrqModeration == IRQ_MASK_SP_RX) ||
			    (Info->MaskIrqModeration == IRQ_MASK_SP_TX) ||
			    (Info->MaskIrqModeration == IRQ_MASK_RX_TX_SP)) {
				Info->MaskIrqModeration = IRQ_MASK_SP_ONLY;
			} 
		}
		Info->IntModTypeSelect = C_INT_MOD_STATIC;
		if (ecoalesc->rx_coalesce_usecs) {
			Info->MaxModIntsPerSec = 
				1000000 / ecoalesc->rx_coalesce_usecs;
			if (!CHIP_ID_YUKON_2(pAC)) {
				if (Info->MaskIrqModeration == IRQ_MASK_TX_ONLY) {
					Info->MaskIrqModeration = IRQ_MASK_TX_RX;
				} 
				if (Info->MaskIrqModeration == IRQ_MASK_SP_ONLY) {
					Info->MaskIrqModeration = IRQ_MASK_SP_RX;
				} 
				if (Info->MaskIrqModeration == IRQ_MASK_SP_TX) {
					Info->MaskIrqModeration = IRQ_MASK_RX_TX_SP;
				}
			} else {
				Info->MaskIrqModeration = Y2_IRQ_MASK;
			}
		}
		if (ecoalesc->tx_coalesce_usecs) {
			Info->MaxModIntsPerSec = 
				1000000 / ecoalesc->tx_coalesce_usecs;
			if (!CHIP_ID_YUKON_2(pAC)) {
				if (Info->MaskIrqModeration == IRQ_MASK_RX_ONLY) {
					Info->MaskIrqModeration = IRQ_MASK_TX_RX;
				} 
				if (Info->MaskIrqModeration == IRQ_MASK_SP_ONLY) {
					Info->MaskIrqModeration = IRQ_MASK_SP_TX;
				} 
				if (Info->MaskIrqModeration == IRQ_MASK_SP_RX) {
					Info->MaskIrqModeration = IRQ_MASK_RX_TX_SP;
				}
			} else {
				Info->MaskIrqModeration = Y2_IRQ_MASK;
			}
		}
	}
	if ((ecoalesc->rate_sample_interval)  ||
	    (ecoalesc->rx_coalesce_usecs_low) ||
	    (ecoalesc->tx_coalesce_usecs_low) ||
	    (ecoalesc->rx_coalesce_usecs_high)||
	    (ecoalesc->tx_coalesce_usecs_high)) {
		if (ecoalesc->rate_sample_interval) {
			if ((ecoalesc->rate_sample_interval < 1) ||
			    (ecoalesc->rate_sample_interval > 10)) {
				return -EINVAL; 
			}
		}
		if (ecoalesc->rx_coalesce_usecs_low) {
			if ((ecoalesc->rx_coalesce_usecs_low < 25) ||
			    (ecoalesc->rx_coalesce_usecs_low > 33333)) {
				return -EINVAL; 
			}
		}
		if (ecoalesc->rx_coalesce_usecs_high) {
			if ((ecoalesc->rx_coalesce_usecs_high < 25) ||
			    (ecoalesc->rx_coalesce_usecs_high > 33333)) {
				return -EINVAL; 
			}
		}
		if (ecoalesc->tx_coalesce_usecs_low) {
			if ((ecoalesc->tx_coalesce_usecs_low < 25) ||
			    (ecoalesc->tx_coalesce_usecs_low > 33333)) {
				return -EINVAL; 
			}
		}
		if (ecoalesc->tx_coalesce_usecs_high) {
			if ((ecoalesc->tx_coalesce_usecs_high < 25) ||
			    (ecoalesc->tx_coalesce_usecs_high > 33333)) {
				return -EINVAL; 
			}
		}

		Info->IntModTypeSelect = C_INT_MOD_DYNAMIC;
		if (ecoalesc->rate_sample_interval) {
			Info->DynIrqModSampleInterval = 
				ecoalesc->rate_sample_interval; 
		}
		if (ecoalesc->rx_coalesce_usecs_low) {
			Info->MaxModIntsPerSecLowerLimit = 
				1000000 / ecoalesc->rx_coalesce_usecs_low;
		}
		if (ecoalesc->tx_coalesce_usecs_low) {
			Info->MaxModIntsPerSecLowerLimit = 
				1000000 / ecoalesc->tx_coalesce_usecs_low;
		}
		if (ecoalesc->rx_coalesce_usecs_high) {
			Info->MaxModIntsPerSecUpperLimit = 
				1000000 / ecoalesc->rx_coalesce_usecs_high;
		}
		if (ecoalesc->tx_coalesce_usecs_high) {
			Info->MaxModIntsPerSecUpperLimit = 
				1000000 / ecoalesc->tx_coalesce_usecs_high;
		}
	}

	if ((PrevModeration         == C_INT_MOD_NONE) &&
	    (Info->IntModTypeSelect != C_INT_MOD_NONE)) {
		SkDimEnableModerationIfNeeded(pAC);
	}
	if (PrevModeration != C_INT_MOD_NONE) {
		SkDimDisableModeration(pAC, PrevModeration);
		if (Info->IntModTypeSelect != C_INT_MOD_NONE) {
			SkDimEnableModerationIfNeeded(pAC);
		}
	}

        return 0;
}
#endif

#ifdef ETHTOOL_GWOL
/*****************************************************************************
 *
 * 	getWOLsettings - retrieves the WOL settings of the selected adapter
 *
 * Description:
 *	All current WOL settings of a selected adapter are placed in the 
 *	passed ethtool_wolinfo structure and are returned to the caller.
 *
 * Returns:	N/A
 *
 */
static void getWOLsettings(
SK_AC                  *pAC,  /* pointer to adapter control context  */
int                     port, /* the port of the selected adapter    */
struct ethtool_wolinfo *ewol) /* mandatory WOL structure for results */
{
	ewol->supported = pAC->WolInfo.SupportedWolOptions;
	ewol->wolopts   = pAC->WolInfo.ConfiguredWolOptions;

	return;
}
#endif

#ifdef ETHTOOL_SWOL
/*****************************************************************************
 *
 *	setWOLsettings - configures the WOL settings of a selected adapter
 *
 * Description:
 *	The WOL settings of a selected adapter are configured regarding
 *	the parameters in the passed ethtool_wolinfo structure.
 *	Note that currently only wake on magic packet is supported!
 *
 * Returns:
 *	==0:	everything fine, no error
 *	!=0:	the return value is the error code of the failure 
 */
static int setWOLsettings(
SK_AC                  *pAC,  /* pointer to adapter control context */
int                     port, /* the port of the selected adapter   */
struct ethtool_wolinfo *ewol) /* WOL structure containing settings  */
{
	if (((ewol->wolopts & WAKE_MAGIC) == WAKE_MAGIC) || (ewol->wolopts == 0)) {
		pAC->WolInfo.ConfiguredWolOptions = ewol->wolopts;
		return 0;
	}
	return -EFAULT;
}
#endif

#ifdef ETHTOOL_GDRVINFO
/*****************************************************************************
 *
 * 	getDriverInfo - returns generic driver and adapter information
 *
 * Description:
 *	Generic driver information is returned via this function, such as
 *	the name of the driver, its version and and firmware version.
 *	In addition to this, the location of the selected adapter is 
 *	returned as a bus info string (e.g. '01:05.0').
 *	
 * Returns:	N/A
 *
 */
static void getDriverInfo(
SK_AC                  *pAC,      /* pointer to adapter control context   */
int                     port,     /* the port of the selected adapter     */
struct ethtool_drvinfo *edrvinfo) /* mandatory info structure for results */
{
	char versionString[32];

	snprintf(versionString, 32, "%s (%s)", VER_STRING, PATCHLEVEL);
	strncpy(edrvinfo->driver, DRIVER_FILE_NAME , 32);
	strncpy(edrvinfo->version, versionString , 32);
	strncpy(edrvinfo->fw_version, "N/A", 32);
	strncpy(edrvinfo->bus_info, pAC->PciDev->slot_name, 32);
#ifdef  ETHTOOL_GSTATS
	edrvinfo->n_stats = SK98LIN_STATS_LEN;
#endif
}
#endif

#ifdef ETHTOOL_PHYS_ID
/*****************************************************************************
 *
 * 	startLocateNIC - start the locate NIC feature of the elected adapter 
 *
 * Description:
 *	This function is used if the user want to locate a particular NIC.
 *	All LEDs are regularly switched on and off, so the NIC can easily
 *	be identified.
 *
 * Returns:	
 *	==0:	everything fine, no error, locateNIC test was started
 *	!=0:	one locateNIC test runs already
 *
 */
static int startLocateNIC(
SK_AC                *pAC,        /* pointer to adapter control context        */
int                   port,       /* the port of the selected adapter          */
struct ethtool_value *blinkSecs)  /* how long the LEDs should blink in seconds */
{
	struct SK_NET_DEVICE *pDev      = pAC->dev[port];
	int                   OtherPort = (port) ? 0 : 1;
	struct SK_NET_DEVICE *pOtherDev = pAC->dev[OtherPort];

	if (isLocateNICrunning) {
		return -EFAULT;
	}
	isLocateNICrunning = SK_TRUE;
	currentPortIndex = port;
	isDualNetCard = (pDev != pOtherDev) ? SK_TRUE : SK_FALSE;

	if (netif_running(pAC->dev[port])) {
		boardWasDown[0] = SK_FALSE;
	} else {
		(*pDev->open)(pDev);
		boardWasDown[0] = SK_TRUE;
	}

	if (isDualNetCard) {
		if (netif_running(pAC->dev[OtherPort])) {
			boardWasDown[1] = SK_FALSE;
		} else {
			(*pOtherDev->open)(pOtherDev);
			boardWasDown[1] = SK_TRUE;
		}
	}

	if ((blinkSecs->data < 1) || (blinkSecs->data > 30)) {
		blinkSecs->data = 3; /* three seconds default */
	}
	nbrBlinkQuarterSeconds = 4*blinkSecs->data;

	init_timer(&locateNICtimer);
	locateNICtimer.function = toggleLeds;
	locateNICtimer.data     = (unsigned long) pAC;
	locateNICtimer.expires  = jiffies + 1*HZ; /* initially 1sec */
	add_timer(&locateNICtimer);

	return 0;
}

/*****************************************************************************
 *
 * 	toggleLeds - Changes the LED state of an adapter
 *
 * Description:
 *	This function changes the current state of all LEDs of an adapter so
 *	that it can be located by a user. If the requested time interval for
 *	this test has elapsed, this function cleans up everything that was 
 *	temporarily setup during the locate NIC test. This involves of course
 *	also closing or opening any adapter so that the initial board state 
 *	is recovered.
 *
 * Returns:	N/A
 *
 */
static void toggleLeds(
unsigned long ptr)  /* holds the pointer to adapter control context */
{
	SK_AC                *pAC       = (SK_AC *) ptr;
	int                   port      = currentPortIndex;
	SK_IOC                IoC       = pAC->IoBase;
	struct SK_NET_DEVICE *pDev      = pAC->dev[port];
	int                   OtherPort = (port) ? 0 : 1;
	struct SK_NET_DEVICE *pOtherDev = pAC->dev[OtherPort];

	SK_U16  YukLedOn = (PHY_M_LED_MO_DUP(MO_LED_ON)  |
			    PHY_M_LED_MO_10(MO_LED_ON)   |
			    PHY_M_LED_MO_100(MO_LED_ON)  |
			    PHY_M_LED_MO_1000(MO_LED_ON) | 
			    PHY_M_LED_MO_RX(MO_LED_ON));
	SK_U16  YukLedOff = (PHY_M_LED_MO_DUP(MO_LED_OFF)  |
			     PHY_M_LED_MO_10(MO_LED_OFF)   |
			     PHY_M_LED_MO_100(MO_LED_OFF)  |
			     PHY_M_LED_MO_1000(MO_LED_OFF) | 
			     PHY_M_LED_MO_RX(MO_LED_OFF));

	nbrBlinkQuarterSeconds--;
	if (nbrBlinkQuarterSeconds <= 0) {
		(*pDev->stop)(pDev);
		if (isDualNetCard) {
			(*pOtherDev->stop)(pOtherDev);
		}

		if (!boardWasDown[0]) {
			(*pDev->open)(pDev);
		}
		if (isDualNetCard) {
			(*pOtherDev->open)(pOtherDev);
		}
		isDualNetCard      = SK_FALSE;
		isLocateNICrunning = SK_FALSE;
		return;
	}

	doSwitchLEDsOn = (doSwitchLEDsOn) ? SK_FALSE : SK_TRUE;
	if (doSwitchLEDsOn) {
		if (pAC->GIni.GIGenesis) {
			SK_OUT8(IoC,MR_ADDR(port,LNK_LED_REG),(SK_U8)SK_LNK_ON);
			SkGeYellowLED(pAC,IoC,LED_ON >> 1);
			SkGeXmitLED(pAC,IoC,MR_ADDR(port,RX_LED_INI),SK_LED_TST);
			if (pAC->GIni.GP[port].PhyType == SK_PHY_BCOM) {
				SkXmPhyWrite(pAC,IoC,port,PHY_BCOM_P_EXT_CTRL,PHY_B_PEC_LED_ON);
			} else if (pAC->GIni.GP[port].PhyType == SK_PHY_LONE) {
				SkXmPhyWrite(pAC,IoC,port,PHY_LONE_LED_CFG,0x0800);
			} else {
				SkGeXmitLED(pAC,IoC,MR_ADDR(port,TX_LED_INI),SK_LED_TST);
			}
		} else {
			SkGmPhyWrite(pAC,IoC,port,PHY_MARV_LED_CTRL,0);
			SkGmPhyWrite(pAC,IoC,port,PHY_MARV_LED_OVER,YukLedOn);
		}
	} else {
		if (pAC->GIni.GIGenesis) {
			SK_OUT8(IoC,MR_ADDR(port,LNK_LED_REG),(SK_U8)SK_LNK_OFF);
			SkGeYellowLED(pAC,IoC,LED_OFF >> 1);
			SkGeXmitLED(pAC,IoC,MR_ADDR(port,RX_LED_INI),SK_LED_DIS);
			if (pAC->GIni.GP[port].PhyType == SK_PHY_BCOM) {
				SkXmPhyWrite(pAC,IoC,port,PHY_BCOM_P_EXT_CTRL,PHY_B_PEC_LED_OFF);
			} else if (pAC->GIni.GP[port].PhyType == SK_PHY_LONE) {
				SkXmPhyWrite(pAC,IoC,port,PHY_LONE_LED_CFG,PHY_L_LC_LEDT);
			} else {
				SkGeXmitLED(pAC,IoC,MR_ADDR(port,TX_LED_INI),SK_LED_DIS);
			}
		} else {
			SkGmPhyWrite(pAC,IoC,port,PHY_MARV_LED_CTRL,0);
			SkGmPhyWrite(pAC,IoC,port,PHY_MARV_LED_OVER,YukLedOff);
		}
	}

	locateNICtimer.function = toggleLeds;
	locateNICtimer.data     = (unsigned long) pAC;
	locateNICtimer.expires  = jiffies + (1*HZ)/4; /* 250ms */
	add_timer(&locateNICtimer);
} 
#endif

/*****************************************************************************
 *
 * 	getPortNumber - evaluates the port number of an interface
 *
 * Description:
 *	It may be that the current interface refers to one which is located
 *	on a dual net adapter. Hence, this function will return the correct
 *	port for further use.
 *
 * Returns:
 *	the port number that corresponds to the selected adapter
 *
 */
static int getPortNumber(
struct net_device *netdev,  /* the pointer to netdev structure       */
struct ifreq      *ifr)     /* what interface the request refers to? */
{
	DEV_NET *pNet = (DEV_NET*) netdev->priv;
	SK_AC   *pAC  = pNet->pAC;

	if (pAC->dev[1] != pAC->dev[0]) {
		if (!strcmp(pAC->dev[1]->name, ifr->ifr_name)) {
			return 1; /* port index 1 */
		}
	}
	return 0;
}

/*******************************************************************************
 *
 * End of file
 *
 ******************************************************************************/
