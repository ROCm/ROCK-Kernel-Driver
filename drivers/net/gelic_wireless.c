/*
 * gelic_wireless.c: wireless extension for gelic_net
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/completion.h>
#include <linux/ctype.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

#include "gelic_net.h"

static struct iw_handler_def gelicw_handler_def;

/*
 * Data tables
 */
static const u32 freq_list[] = {
	2412, 2417, 2422, 2427, 2432,
	2437, 2442, 2447, 2452, 2457,
	2462, 2467, 2472, 2484 };

static const u32 bitrate_list[] = {
	 1000000,  2000000,  5500000, 11000000, /* 11b */
	 6000000,  9000000, 12000000, 18000000,
	24000000, 36000000, 48000000, 54000000
};
#define GELICW_NUM_11B_BITRATES 4 /* 802.11b: 1 ~ 11 Mb/s */

static inline struct device * ntodev(struct net_device *netdev)
{
	return &((struct gelic_net_card *)netdev_priv(netdev))->dev->core;
}

static inline struct device * wtodev(struct gelic_wireless *w)
{
	return &w->card->dev->core;
}
static inline struct gelic_wireless *gelicw_priv(struct net_device *netdev)
{
	struct gelic_net_card *card = netdev_priv(netdev);
	return &card->w;
}
static inline unsigned int bus_id(struct gelic_wireless *w)
{
	return w->card->dev->bus_id;
}
static inline unsigned int dev_id(struct gelic_wireless *w)
{
	return w->card->dev->dev_id;
}

/* control wired or wireless */
static void gelicw_vlan_mode(struct net_device *netdev, int mode)
{
	struct gelic_net_card *card = netdev_priv(netdev);

	if ((mode < GELIC_NET_VLAN_WIRED) ||
	    (mode > GELIC_NET_VLAN_WIRELESS))
		return;

	card->vlan_index = mode - 1;
}

/* wireless_send_event */
static void notify_assoc_event(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	union iwreq_data wrqu;

	cancel_delayed_work(&w->work_scan_all);
	cancel_delayed_work(&w->work_scan_essid);

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (w->state < GELICW_STATE_ASSOCIATED) {
		dev_dbg(ntodev(netdev), "notify disassociated\n");
		w->is_assoc = 0;
		memset(w->bssid, 0, ETH_ALEN);
	} else {
		dev_dbg(ntodev(netdev), "notify associated\n");
		w->channel = w->current_bss.channel;
		w->is_assoc = 1;
		memcpy(w->bssid, w->current_bss.bssid, ETH_ALEN);
	}
	memcpy(wrqu.ap_addr.sa_data, w->bssid, ETH_ALEN);
	wireless_send_event(netdev, SIOCGIWAP, &wrqu, NULL);
}

/* association/disassociation */
static void gelicw_assoc(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (w->state == GELICW_STATE_ASSOCIATED)
		return;
	schedule_delayed_work(&w->work_start, 0);
}

static int gelicw_disassoc(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	memset(w->bssid, 0, ETH_ALEN); /* clear current bssid */
	if (w->state < GELICW_STATE_ASSOCIATED)
		return 0;

	schedule_delayed_work(&w->work_stop, 0);
	w->state = GELICW_STATE_SCAN_DONE;
	/* notify disassociation */
	notify_assoc_event(netdev);

	return 0;
}

static void gelicw_reassoc(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (w->cmd_send_flg != GELICW_CMD_SEND_ALL)
		return;

	if (!gelicw_disassoc(netdev))
		gelicw_assoc(netdev);
}


/*
 * lv1_net_control command
 */
/* control Ether port */
static int gelicw_cmd_set_port(struct net_device *netdev, int mode)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 tag, val;
	int status;

	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_PORT, GELICW_ETHER_PORT, mode, 0,
			&tag, &val);
	if (status)
		dev_dbg(ntodev(netdev), "GELICW_SET_PORT failed:%d\n", status);
	return status;
}

/* check support channels */
static int gelicw_cmd_get_ch_info(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 ch_info, val;
	int status;

	if (w->state < GELICW_STATE_UP)
		return -EIO;

	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_INFO, 0, 0 , 0,
			&ch_info, &val);
	if (status) {
		dev_dbg(ntodev(netdev), "GELICW_GET_INFO failed:%d\n", status);
		w->ch_info = CH_INFO_FAIL;
	} else {
		dev_dbg(ntodev(netdev), "ch_info:%lx val:%lx\n", ch_info, val);
		w->ch_info = ch_info >> 48; /* MSB 16bit shows supported channnels */
	}
	return status;
}


/*
 * lv1_net_control GELICW_SET_CMD command
 * queued using schedule_work()
 */
/* association */
static void gelicw_cmd_start(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 val;
	int status;

	if (w->state < GELICW_STATE_SCAN_DONE)
		return;

	dev_dbg(ntodev(netdev), "GELICW_CMD_START\n");
	w->cmd_id = GELICW_CMD_START;
	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_CMD, w->cmd_id, 0, 0,
			&w->cmd_tag, &val);
	if (status) {
		w->cmd_tag = 0;
		dev_dbg(ntodev(netdev), "GELICW_CMD_START failed:%d\n", status);
	}
}

/* association done */
static void gelicw_cmd_start_done(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 res, val;
	int status;

	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_RES, w->cmd_tag, 0, 0,
			&res, &val);
	w->cmd_tag = 0;
	wake_up_interruptible(&w->waitq_cmd);

	if (status || res)
		dev_dbg(ntodev(netdev), "GELICW_CMD_START res:%d,%ld\n", status, res);
}

/* disassociation */
static void gelicw_cmd_stop(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 res, val;
	int status;

	if (w->state < GELICW_STATE_SCAN_DONE)
		return;

	dev_dbg(ntodev(netdev), "GELICW_CMD_STOP\n");
	init_completion(&w->cmd_done);
	w->cmd_id = GELICW_CMD_STOP;
	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_CMD, w->cmd_id, 0, 0,
			&w->cmd_tag, &val);
	if (status) {
		w->cmd_tag = 0;
		dev_dbg(ntodev(netdev), "GELICW_CMD_STOP failed:%d\n", status);
		return;
	}
	wait_for_completion_interruptible(&w->cmd_done);

	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_RES, w->cmd_tag, 0, 0,
			&res, &val);
	w->cmd_tag = 0;
	if (status || res) {
		dev_dbg(ntodev(netdev), "GELICW_CMD_STOP res:%d,%ld\n", status, res);
		return;
	}
}

/* get rssi */
static void gelicw_cmd_rssi(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 lpar, res, val;
	int status;
	struct rssi_desc *rssi;

	if (w->state < GELICW_STATE_ASSOCIATED) {
		w->rssi = 0;
		complete(&w->rssi_done);
		return;
	}

	lpar = ps3_mm_phys_to_lpar(__pa(w->data_buf));
	init_completion(&w->cmd_done);
	w->cmd_id = GELICW_CMD_GET_RSSI;
	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_CMD, w->cmd_id, 0, 0,
			&w->cmd_tag, &val);
	if (status) {
		w->cmd_tag = 0;
		w->rssi = 0;
		dev_dbg(ntodev(netdev), "GELICW_CMD_GET_RSSI failed:%d\n", status);
	} else {
		wait_for_completion_interruptible(&w->cmd_done);
		status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_RES, w->cmd_tag, lpar, sizeof(*rssi),
				&res, &val);
		w->cmd_tag = 0;
		if (status || res) {
			w->rssi = 0;
			dev_dbg(ntodev(netdev), "GELICW_CMD_GET_RSSI res:%d,%ld\n", status, res);
		}
		rssi = w->data_buf;
		w->rssi = rssi->rssi;
		dev_dbg(ntodev(netdev), "GELICW_CMD_GET_RSSI:%d\n", rssi->rssi);
	}

	complete(&w->rssi_done);
}

/* set common configuration */
static int gelicw_cmd_common(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 lpar, res, val;
	int status;
	struct common_config *config;

	if (w->state < GELICW_STATE_SCAN_DONE)
		return -EIO;

	lpar = ps3_mm_phys_to_lpar(__pa(w->data_buf));
	config = w->data_buf;
	config->scan_index = w->bss_index;
	config->bss_type = (w->iw_mode == IW_MODE_ADHOC) ?
				GELICW_BSS_ADHOC : GELICW_BSS_INFRA;
	config->auth_method = (w->auth_mode == IW_AUTH_ALG_SHARED_KEY) ?
				GELICW_AUTH_SHARED : GELICW_AUTH_OPEN;
	switch (w->wireless_mode) {
	case IEEE_B:
		config->op_mode = GELICW_OP_MODE_11B;
		break;
	case IEEE_G:
		config->op_mode = GELICW_OP_MODE_11G;
		break;
	case IEEE_B | IEEE_G:
	default:
		/* default 11bg mode */
		config->op_mode = GELICW_OP_MODE_11BG;
		break;
	}

	dev_dbg(ntodev(netdev), "GELICW_CMD_SET_CONFIG: index:%d type:%d auth:%d mode:%d\n",\
		config->scan_index, config->bss_type,\
		config->auth_method,config->op_mode);
	init_completion(&w->cmd_done);
	w->cmd_id = GELICW_CMD_SET_CONFIG;
	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_CMD, w->cmd_id,
			lpar, sizeof(struct common_config),
			&w->cmd_tag, &val);
	if (status) {
		w->cmd_tag = 0;
		dev_dbg(ntodev(netdev), "GELICW_CMD_SET_CONFIG failed:%d\n", status);
		return status;
	}
	wait_for_completion_interruptible(&w->cmd_done);

	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_RES, w->cmd_tag, 0, 0,
			&res, &val);
	w->cmd_tag = 0;
	if (status || res) {
		dev_dbg(ntodev(netdev), "GELICW_CMD_SET_CONFIG res:%d,%ld\n", status, res);
		return -EFAULT;
	}

	w->cmd_send_flg |= GELICW_CMD_SEND_COMMON;

	return 0;
}

#define h2i(c)    (isdigit(c) ? c - '0' : toupper(c) - 'A' + 10)
/* send WEP/WPA configuration */
static int gelicw_cmd_encode(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 res, val, lpar;
	int status;
	struct wep_config *config;
	struct wpa_config *wpa_config;

	u8 *key, key_len;

	if (w->state < GELICW_STATE_SCAN_DONE)
		return -EIO;

	lpar = ps3_mm_phys_to_lpar(__pa(w->data_buf));

	if (w->key_alg == IW_ENCODE_ALG_WEP ||
	    w->key_alg == IW_ENCODE_ALG_NONE) {
		/* WEP */
		config = w->data_buf;
		memset(config, 0, sizeof(struct wep_config));

		/* check key len */
		key_len = w->key_len[w->key_index];
		key = w->key[w->key_index];
		if (w->key_alg == IW_ENCODE_ALG_NONE)
			config->sec = GELICW_WEP_SEC_NONE;
		else
			config->sec = (key_len == 5) ? GELICW_WEP_SEC_40BIT :
							GELICW_WEP_SEC_104BIT;
		/* copy key */
		memcpy(config->key[w->key_index], key, key_len);

		/* send wep config */
		dev_dbg(ntodev(netdev), "GELICW_CMD_SET_WEP\n");
		init_completion(&w->cmd_done);
		w->cmd_id = GELICW_CMD_SET_WEP;
		status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_CMD, w->cmd_id,
			lpar, sizeof(struct wep_config),
			&w->cmd_tag, &val);
		if (status) {
			w->cmd_tag = 0;
			dev_dbg(ntodev(netdev), "GELICW_CMD_SET_WEP failed:%d\n", status);
			goto err;
		}
		wait_for_completion_interruptible(&w->cmd_done);

		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_GET_RES, w->cmd_tag, 0, 0,
				&res, &val);
		w->cmd_tag = 0;
		if (status || res) {
			dev_dbg(ntodev(netdev), "GELICW_CMD_SET_WEP res:%d,%ld\n", status, res);
			status = -EFAULT;
			goto err;
		}
	} else {
		/* WPA */
		wpa_config = w->data_buf;
		memset(wpa_config, 0, sizeof(struct wpa_config));

		switch (w->key_alg) {
		case IW_ENCODE_ALG_TKIP:
			wpa_config->sec = GELICW_WPA_SEC_TKIP;
			break;
		default:
		case IW_ENCODE_ALG_CCMP:
			wpa_config->sec = GELICW_WPA_SEC_AES;
			break;
		}
		/* check key len */
		key = w->key[w->key_index];
		key_len = w->key_len[w->key_index];

		if (key_len > 64) key_len = 64;
		if (key_len != 64) {
			/* if key_len isn't 64byte ,it should be passphrase */
			/* pass phrase */
			memcpy(wpa_config->psk_material, key, key_len);
			wpa_config->psk_type = GELICW_PSK_PASSPHRASE;
		} else {
			int i;
			/* 64 hex */
			for (i = 0; i < 32; i++)
				wpa_config->psk_material[i] =
						h2i(key[2 * i]) * 16
						+ h2i(key[2 * i + 1]);
			wpa_config->psk_type = GELICW_PSK_64HEX;
		}

		/* send wpa config */
		dev_dbg(ntodev(netdev), "GELICW_CMD_SET_WPA:type:%d\n", wpa_config->psk_type);
		init_completion(&w->cmd_done);
		w->cmd_id = GELICW_CMD_SET_WPA;
		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_SET_CMD, w->cmd_id,
				lpar, sizeof(struct wpa_config),
				&w->cmd_tag, &val);
		if (status) {
			w->cmd_tag = 0;
			dev_dbg(ntodev(netdev), "GELICW_CMD_SET_WPA failed:%d\n", status);
			goto err;
		}
		wait_for_completion_interruptible(&w->cmd_done);

		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_GET_RES, w->cmd_tag, 0, 0, &res, &val);
		w->cmd_tag = 0;
		if (status || res) {
			dev_dbg(ntodev(netdev), "GELICW_CMD_SET_WPA res:%d,%ld\n", status, res);
			status = -EFAULT;
			goto err;
		}
	}

	w->cmd_send_flg |= GELICW_CMD_SEND_ENCODE;
	/* (re)associate */
	gelicw_reassoc(netdev);

	return 0;
err:
	gelicw_disassoc(netdev);
	return status;
}

static int gelicw_is_ap_11b(struct gelicw_bss *list)
{
	if (list->rates_len + list->rates_ex_len == GELICW_NUM_11B_BITRATES)
		return 1;
	else
		return 0;
}

/* get scan results */
static int gelicw_cmd_get_scan(struct gelic_wireless *w)
{
	u64 lpar, res, val;
	int status;
	struct scan_desc *desc;
	int i, j;
	u8 *p;


	/* get scan */
	dev_dbg(wtodev(w), "GELICW_CMD_GET_SCAN\n");
	init_completion(&w->cmd_done);
	w->cmd_id = GELICW_CMD_GET_SCAN;
	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_SET_CMD, w->cmd_id, 0, 0,
			&w->cmd_tag, &val);
	if (status) {
		w->cmd_tag = 0;
		dev_dbg(wtodev(w), "GELICW_CMD_GET_SCAN failed:%d\n", status);
		return status;
	}
	wait_for_completion_interruptible(&w->cmd_done);

	lpar = ps3_mm_phys_to_lpar(__pa(w->data_buf));
	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_RES, w->cmd_tag, lpar, PAGE_SIZE,
			&res, &val);
	w->cmd_tag = 0;
	if (status || res) {
		dev_dbg(wtodev(w), "GELICW_CMD_GET_SCAN res:%d,%ld\n",
			status, res);
		return -EFAULT;
	}

	desc = w->data_buf;
	for (i = 0;
	     i < val / sizeof(struct scan_desc) && i < MAX_SCAN_BSS;
	     i++) {
		struct gelicw_bss *bss = &w->bss_list[i];

		bss->rates_len = 0;
		for (j = 0; j < MAX_RATES_LENGTH; j++)
			if (desc[i].rate[j])
				bss->rates[bss->rates_len++] = desc[i].rate[j];
		bss->rates_ex_len = 0;
		for (j = 0; j < MAX_RATES_EX_LENGTH; j++)
			if (desc[i].ext_rate[j])
				bss->rates_ex[bss->rates_ex_len++]
						= desc[i].ext_rate[j];

		if (desc[i].capability & 0x3) {
			if (desc[i].capability & 0x1)
				bss->mode = IW_MODE_INFRA;
			else
				bss->mode = IW_MODE_ADHOC;
		}
		bss->channel = desc[i].channel;
		bss->essid_len = strnlen(desc[i].essid, IW_ESSID_MAX_SIZE);
		bss->rssi = (u8)desc[i].rssi;
		bss->capability = desc[i].capability;
		bss->beacon_interval = desc[i].beacon_period;
		memset(bss->essid, 0, sizeof(bss->essid));
		memcpy(bss->essid, desc[i].essid, bss->essid_len);
		p = (u8 *)&desc[i].bssid;
		memcpy(bss->bssid, &p[2], ETH_ALEN);/* bssid:64bit in desc */
		bss->sec_info = desc[i].security;
	}
	w->num_bss_list = i;

	if (w->num_bss_list)
		return 0; /* ap found */
	else
		return -1; /* no ap found */
}

/* search bssid in bss list */
static int gelicw_search_bss_list(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	static const u8 off[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	int i;
	int check_bss = 0, check_11g = 0, found_bss, found_11g;

	if (!w->num_bss_list)
		return -1;	/* no bss list */

	if (memcmp(off, w->wap_bssid, ETH_ALEN))
		check_bss = 1;

	/* wireless op_mode seems not working with CMD_SET_CONFIG */
	if (w->wireless_mode == IEEE_G)
		check_11g = 1;

	if (!check_bss && !check_11g)
		return 0;	/* no check bssid, wmode */

	for (i = 0; i < w->num_bss_list; i++) {
		found_bss = found_11g = 1;
		if (check_bss &&
		    memcmp(w->bss_list[i].bssid, w->wap_bssid, ETH_ALEN))
			found_bss = 0; /* not found */

		if (check_11g &&
		    gelicw_is_ap_11b(&w->bss_list[i]))
			found_11g = 0; /* not found */

		if (found_bss && found_11g)
			break;
	}

	if (i == w->num_bss_list)
		return -1; /* not found */
	else
		return i;
}

/* scan done */
static void gelicw_scan_complete(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	int res;
	int bss_index;

	/* get scan results */
	res = gelicw_cmd_get_scan(w);
	if (w->is_assoc)
		w->state = GELICW_STATE_ASSOCIATED;
	else
		w->state = GELICW_STATE_SCAN_DONE;

	if (res) {
		/* No AP found */
		if (!w->scan_all) {
			/* no specified AP */
			gelicw_disassoc(netdev);
			/* rescan */
			if (w->essid_search && w->essid_len)
				schedule_delayed_work(&w->work_scan_essid,
							GELICW_SCAN_INTERVAL);
			return;
		}
	}

	if (w->scan_all) {
		/* all params should be set again after scan */
		w->cmd_send_flg = 0;
		return;
	}

	bss_index = gelicw_search_bss_list(netdev);
	if (bss_index < 0) {
		/* no wap_bssid in bss_list */
		if (w->essid_search && w->essid_len)
			schedule_delayed_work(&w->work_scan_essid,
						GELICW_SCAN_INTERVAL);
		return;
	}
	w->bss_index = (u8)bss_index;
	w->current_bss = w->bss_list[w->bss_index];

	/* essid search complete */
	w->essid_search = 0;
	w->cmd_send_flg |= GELICW_CMD_SEND_SCAN;

	/* (re)connect to AP */
	if (w->is_assoc) {
		/* notify disassociation */
		w->state = GELICW_STATE_SCAN_DONE;
		notify_assoc_event(netdev);
	}
	schedule_delayed_work(&w->work_common, 0);
	schedule_delayed_work(&w->work_encode, 0);
}

/* start scan */
static int gelicw_cmd_set_scan(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 res, val, lpar;
	int status;
	u8 *p;

	if (w->state < GELICW_STATE_UP) {
		w->scan_all = 0;
		return -EIO;
	}
	if (!w->scan_all && !w->essid_len)
		return -EINVAL; /* unsupported essid ANY */

	/* set device wired to wireless when essid is set */
	if (!w->scan_all && w->wireless < GELICW_WIRELESS_ON) {
		gelicw_vlan_mode(netdev, GELIC_NET_VLAN_WIRELESS);
		gelicw_cmd_set_port(netdev, GELICW_PORT_DOWN);
		w->wireless = GELICW_WIRELESS_ON;
	}

	p = w->data_buf;
	lpar = ps3_mm_phys_to_lpar(__pa(w->data_buf));

	/* avoid frequent scanning */
	if (!w->essid_search && /* background scan off */
	    w->scan_all &&
	    (time_before64(get_jiffies_64(), w->last_scan + 5 * HZ)))
		return 0;

	w->bss_key_alg = IW_ENCODE_ALG_NONE;

	init_completion(&w->cmd_done);
	w->state = GELICW_STATE_SCANNING;
	w->cmd_id = GELICW_CMD_SCAN;

	if (w->scan_all) {
		/* scan all ch */
		dev_dbg(ntodev(netdev), "GELICW_CMD_SCAN all\n");
		w->last_scan = get_jiffies_64(); /* last scan time */
		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_SET_CMD, w->cmd_id, 0, 0,
				&w->cmd_tag, &val);
	} else {
		/* scan essid */
		memset(p, 0, 32);
		memcpy(p, w->essid, w->essid_len);
		dev_dbg(ntodev(netdev), "GELICW_CMD_SCAN essid\n");
		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_SET_CMD, w->cmd_id, lpar, 32,
				&w->cmd_tag, &val);
	}

	if (status) {
		w->cmd_tag = 0;
		dev_dbg(ntodev(netdev), "GELICW_CMD_SCAN failed:%d\n", status);
		return status;
	}
	wait_for_completion_interruptible(&w->cmd_done);

	status = lv1_net_control(bus_id(w), dev_id(w),
			GELICW_GET_RES, w->cmd_tag, 0, 0,
			&res, &val);
	w->cmd_tag = 0;
	if (status || res) {
		dev_dbg(ntodev(netdev), "GELICW_CMD_SCAN res:%d,%ld\n", status, res);
		return -EFAULT;
	}

	return 0;
}

static void gelicw_send_common_config(struct net_device *netdev,
					u8 *cur, u8 mode)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (*cur != mode) {
		*cur = mode;
		if (w->state < GELICW_STATE_SCAN_DONE)
			return;

		if (!(w->cmd_send_flg & GELICW_CMD_SEND_SCAN) &&
		    w->essid_len)
			/* scan essid and set other params */
			schedule_delayed_work(&w->work_scan_essid, 0);
		else {
			schedule_delayed_work(&w->work_common, 0);
			if (w->cmd_send_flg
			    & GELICW_CMD_SEND_ENCODE)
				/* (re)send encode key */
				schedule_delayed_work(&w->work_encode, 0);
		}
	}
}


/*
 * work queue
 */
static void gelicw_work_rssi(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_rssi.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag) {
		schedule_delayed_work(&w->work_rssi, HZ / 5);
		return;
	}

	gelicw_cmd_rssi(netdev);
}

static void gelicw_work_scan_all(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_scan_all.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag || w->state == GELICW_STATE_SCANNING) {
		schedule_delayed_work(&w->work_scan_all, HZ / 5);
		return;
	}

	w->scan_all = 1;
	gelicw_cmd_set_scan(netdev);
}

static void gelicw_work_scan_essid(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_scan_essid.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag || w->scan_all || w->state == GELICW_STATE_SCANNING) {
		schedule_delayed_work(&w->work_scan_essid, HZ / 5);
		return;
	}
	w->bss_index = 0;
	w->scan_all = 0;
	gelicw_cmd_set_scan(netdev);
}

static void gelicw_work_common(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_common.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag) {
		schedule_delayed_work(&w->work_common, HZ / 5);
		return;
	}
	gelicw_cmd_common(netdev);
}

static void gelicw_work_encode(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_encode.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag) {
		schedule_delayed_work(&w->work_encode, HZ / 5);
		return;
	}
	gelicw_cmd_encode(netdev);
}

static void gelicw_work_start(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_start.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag) {
		schedule_delayed_work(&w->work_start, HZ / 5);
		return;
	}
	gelicw_cmd_start(netdev);
}

static void gelicw_work_start_done(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_start_done);
	struct net_device *netdev = w->card->netdev;

	gelicw_cmd_start_done(netdev);
}

static void gelicw_work_stop(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_stop.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag) {
		schedule_delayed_work(&w->work_stop, HZ / 5);
		return;
	}
	gelicw_cmd_stop(netdev);
}

static void gelicw_work_roam(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_roam.work);
	struct net_device *netdev = w->card->netdev;

	if (w->cmd_tag || w->scan_all || w->state == GELICW_STATE_SCANNING) {
		schedule_delayed_work(&w->work_roam, HZ / 5);
		return;
	}
	gelicw_cmd_stop(netdev);
	w->bss_index = 0;
	w->scan_all = 0;
	gelicw_cmd_set_scan(netdev);
}

/*
 * Event handler
 */
#define GELICW_EVENT_LOOP_MAX 16
static void gelicw_event(struct work_struct *work)
{
	struct gelic_wireless *w =
		container_of(work, struct gelic_wireless, work_event);
	struct net_device *netdev = w->card->netdev;
	u64 event_type, val;
	int i, status;

	for (i = 0; i < GELICW_EVENT_LOOP_MAX; i++) {
		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_GET_EVENT, 0, 0 , 0,
				&event_type, &val);
		if (status == GELICW_EVENT_NO_ENTRY)
			/* got all events */
			break;
		else if (status){
			dev_dbg(ntodev(netdev), "GELICW_GET_EVENT failed:%d\n", status);
			return;
		}
		switch(event_type) {
		case GELICW_EVENT_DEVICE_READY:
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_DEVICE_READY\n");
			break;
		case GELICW_EVENT_SCAN_COMPLETED:
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_SCAN_COMPLETED\n");
			gelicw_scan_complete(netdev);
			break;
		case GELICW_EVENT_BEACON_LOST:
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_BEACON_LOST\n");
			w->state = GELICW_STATE_SCAN_DONE;
			notify_assoc_event(netdev);
			/* roaming */
			w->essid_search = 1;
			schedule_delayed_work(&w->work_roam, 0);
			break;
		case GELICW_EVENT_CONNECTED:
		{
			u16 ap_sec;
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_CONNECTED\n");
			/* this event ocuured with any key_alg */
			ap_sec = w->current_bss.sec_info;
			if (w->key_alg == IW_ENCODE_ALG_NONE) {
				/* no encryption */
				if (ap_sec == 0) {
					w->state = GELICW_STATE_ASSOCIATED;
					notify_assoc_event(netdev);
				}
			} else if (w->key_alg == IW_ENCODE_ALG_WEP){
				if ((ap_sec & GELICW_SEC_TYPE_WEP_MASK)
				    == GELICW_SEC_TYPE_WEP) {
					/* wep */
					w->state = GELICW_STATE_ASSOCIATED;
					notify_assoc_event(netdev);
				}
			}
			break;
		}
		case GELICW_EVENT_WPA_CONNECTED:
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_WPA_CONNECTED\n");
			w->state = GELICW_STATE_ASSOCIATED;
			notify_assoc_event(netdev);
			break;
		case GELICW_EVENT_WPA_ERROR:
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_WPA_ERROR\n");
			break;
		default:
			dev_dbg(ntodev(netdev), "  GELICW_EVENT_UNKNOWN\n");
			break;
		}
	}
}

static void gelicw_clear_event(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u64 event_type, val;
	int i, status;

	for (i = 0; i < GELICW_EVENT_LOOP_MAX; i++) {
		status = lv1_net_control(bus_id(w), dev_id(w),
				GELICW_GET_EVENT, 0, 0 , 0,
				&event_type, &val);
		if (status)
			return;/* got all events */

		switch(event_type) {
		case GELICW_EVENT_SCAN_COMPLETED:
			w->state = GELICW_STATE_SCAN_DONE;
			wake_up_interruptible(&w->waitq_scan);
			break;
		default:
			break;
		}
	}
}

/*
 * gelic_net support function
 */
static void gelicw_clear_params(struct gelic_wireless *w)
{
	int i;

	/* clear status */
	w->state = GELICW_STATE_DOWN;
	w->cmd_send_flg = 0;
	w->scan_all = 0;
	w->is_assoc = 0;
	w->essid_search = 0;
	w->cmd_tag = 0;
	w->cmd_id = 0;
	w->last_scan = 0;

	/* default mode and settings */
	w->essid_len = 0;
	w->essid[0] = '\0';
	w->nick[0] = '\0';
	w->iw_mode = IW_MODE_INFRA;
	w->auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
	w->wireless_mode = IEEE_B | IEEE_G;
	w->bss_index = 0;
	memset(w->bssid, 0, ETH_ALEN);
	memset(w->wap_bssid, 0, ETH_ALEN);

	/* init key */
	w->key_index = 0;
	for (i = 0; i < WEP_KEYS; i++) {
		w->key[i][0] = '\0';
		w->key_len[i] = 0;
	}
	w->key_alg = IW_ENCODE_ALG_NONE;
	w->bss_key_alg = IW_ENCODE_ALG_NONE;
}

int gelicw_setup_netdev(struct net_device *netdev, int wi)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (wi < 0) {
		/* PS3 low model has no wireless */
		dev_info(ntodev(netdev), "No wireless dvice in this system\n");
		w->wireless = 0;
		return 0;
	}
	/* version check */
	if (ps3_compare_firmware_version(1, 6, 0) < 0) {
		dev_info(ntodev(netdev),
			 "firmware is too old for wireless.\n");
		w->wireless = 0;
		return 0;
	}
	/* we need 4K aligned, 16 units of scan_desc sized */
	BUILD_BUG_ON(PAGE_SIZE < sizeof(struct scan_desc) * MAX_SCAN_BSS);
	w->data_buf = (void *)get_zeroed_page(GFP_KERNEL);
	if (!w->data_buf) {
		w->wireless = 0;
		dev_info(ntodev(netdev), "%s:get_page failed\n", __func__);
		return -ENOMEM;
	}

	w->wireless = GELICW_WIRELESS_SUPPORTED;

	w->ch_info = 0;
	w->channel = 0;
	netdev->wireless_data = &w->wireless_data;
	netdev->wireless_handlers = &gelicw_handler_def;
	INIT_WORK(&w->work_event, gelicw_event);
	INIT_WORK(&w->work_start_done, gelicw_work_start_done);
	INIT_DELAYED_WORK(&w->work_rssi, gelicw_work_rssi);
	INIT_DELAYED_WORK(&w->work_scan_all, gelicw_work_scan_all);
	INIT_DELAYED_WORK(&w->work_scan_essid, gelicw_work_scan_essid);
	INIT_DELAYED_WORK(&w->work_common, gelicw_work_common);
	INIT_DELAYED_WORK(&w->work_encode, gelicw_work_encode);
	INIT_DELAYED_WORK(&w->work_start, gelicw_work_start);
	INIT_DELAYED_WORK(&w->work_stop, gelicw_work_stop);
	INIT_DELAYED_WORK(&w->work_roam, gelicw_work_roam);
	init_waitqueue_head(&w->waitq_cmd);
	init_waitqueue_head(&w->waitq_scan);

	gelicw_clear_params(w);

	return 0;
}

void gelicw_up(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (!w->wireless)
		return;

	dev_dbg(ntodev(netdev), "gelicw_up\n");
	if (w->state < GELICW_STATE_UP)
		w->state = GELICW_STATE_UP;

	/* start essid scanning */
	if (w->essid_len)
		schedule_delayed_work(&w->work_scan_essid, 0);
}

int gelicw_down(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (!w->wireless || w->state == GELICW_STATE_DOWN)
		return 0;

	dev_dbg(ntodev(netdev), "gelicw_down\n");
	w->wireless = GELICW_WIRELESS_SHUTDOWN;
	flush_scheduled_work();

	/* check cmd_tag of CMD_START */
	if (w->cmd_id == GELICW_CMD_START)
		wait_event_interruptible(w->waitq_cmd, !w->cmd_tag);
	/* wait scan done */
	if (w->state == GELICW_STATE_SCANNING) {
		wait_event_interruptible(w->waitq_scan,
					w->state != GELICW_STATE_SCANNING);
		gelicw_cmd_get_scan(w);
	}

	gelicw_cmd_stop(netdev);
	if (w->is_assoc) {
		w->state = GELICW_STATE_DOWN;
		notify_assoc_event(netdev);
	}
	gelicw_clear_params(w);

	/* set device wireless to wired */
	gelicw_vlan_mode(netdev, GELIC_NET_VLAN_WIRED);
	gelicw_cmd_set_port(netdev, GELICW_PORT_UP);
	w->wireless = GELICW_WIRELESS_SUPPORTED;

	return 0;
}

void gelicw_remove(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (!w->wireless)
		return;

	dev_dbg(ntodev(netdev), "gelicw_remove\n");
	gelicw_down(netdev);
	w->wireless = 0;
	netdev->wireless_handlers = NULL;
	free_page((unsigned long)w->data_buf);
}

void gelicw_interrupt(struct net_device *netdev, u64 status)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (!w->wireless)
		return;

	if (status & GELICW_DEVICE_CMD_COMP) {
		dev_dbg(ntodev(netdev), "GELICW_DEVICE_CMD_COMP\n");
		if (w->cmd_id == GELICW_CMD_START)
			schedule_work(&w->work_start_done);
		else
			complete(&w->cmd_done);
	}
	if (status & GELICW_DEVICE_EVENT_RECV) {
		dev_dbg(ntodev(netdev), "GELICW_DEVICE_EVENT_RECV\n");
		if (w->wireless == GELICW_WIRELESS_SHUTDOWN)
			gelicw_clear_event(netdev);
		else
			schedule_work(&w->work_event);
	}
}

int gelicw_is_associated(struct net_device *netdev)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	if (!w->wireless)
		return 0;

	return w->is_assoc;
}


/*
 * Wireless externsions
 */
static int gelicw_get_name(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx: get_name\n");
	if (w->state < GELICW_STATE_UP) {
		strcpy(wrqu->name, "radio off");
		return 0;
	}

	if (w->wireless_mode == IEEE_B ||
	    (w->is_assoc && gelicw_is_ap_11b(&w->current_bss)))
		strcpy(wrqu->name, "IEEE 802.11b");
	else {
		switch (w->wireless_mode) {
		case IEEE_G:
			strcpy(wrqu->name, "IEEE 802.11g");
			break;
		case IEEE_B | IEEE_G:
		default:
			strcpy(wrqu->name, "IEEE 802.11bg");
			break;
		}
	}

	return 0;
}

static int gelicw_set_freq(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_freq *fwrq = &wrqu->freq;
	int ch;

	dev_dbg(ntodev(netdev), "wx: set_freq e:%d m:%d\n", fwrq->e, fwrq->m);
	if (w->is_assoc || w->state < GELICW_STATE_UP)
		return 0;

	/* this setting has no effect for INFRA mode */
	if (fwrq->e == 1) {
		u32 f = fwrq->m / 100000;
		int i;
		for (i = 0; i < ARRAY_SIZE(freq_list); i++)
			if (freq_list[i] == f)
				break;
		if (i == ARRAY_SIZE(freq_list))
			return -EINVAL;
		fwrq->m = i + 1; /* ch number */
		fwrq->e = 0;
	}
	if (fwrq->e > 0)
		return -EINVAL;

	ch = fwrq->m;
	if (ch < 1)
		w->channel = 0; /* auto */
	else if (ch > ARRAY_SIZE(freq_list))
		return -EINVAL;
	else {
		/* check supported channnel */
		if (!w->ch_info)
			gelicw_cmd_get_ch_info(netdev);
		if (w->ch_info & (1 << (ch - 1)))
			w->channel = ch;
		else
			return -EINVAL;
	}
	dev_dbg(ntodev(netdev), " set cnannel: %d\n", w->channel);

	return 0;
}

static int gelicw_get_freq(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx: get_freq:%d\n", w->channel);
	if (w->channel == 0)
		wrqu->freq.m = 0;
	else
		wrqu->freq.m = freq_list[w->channel - 1] * 100000;
	wrqu->freq.e = 1;

	return 0;
}

static int gelicw_set_mode(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	int mode = wrqu->mode;
	u8 iw_mode = IW_MODE_INFRA;

	dev_dbg(ntodev(netdev), "wx: set_mode:%x\n",mode);
	switch (mode) {
	case IW_MODE_ADHOC:
		dev_dbg(ntodev(netdev), "IW_MODE_ADHOC\n");
		iw_mode = mode;
		return -EOPNOTSUPP; /* adhoc not supported */
	case IW_MODE_INFRA:
	default:
		dev_dbg(ntodev(netdev), "IW_MODE_INFRA\n");
		iw_mode = IW_MODE_INFRA;
		break;
	}

	/* send common config */
	gelicw_send_common_config(netdev, &w->iw_mode, iw_mode);

	return 0;
}

static int gelicw_get_mode(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx: get_mode\n");
	wrqu->mode = w->iw_mode;

	return 0;
}

static inline int gelicw_qual2level(int qual)
{
	return (qual * 4 - 820)/10; /* FIXME: dummy */
}

static int gelicw_get_range(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_range *range = (struct iw_range *)extra;
	int num_ch, i;

	dev_dbg(ntodev(netdev), "wx: get_range\n");
	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* wireless extension */
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 19;

	/* supported bitrates */
	if (w->wireless_mode == IEEE_B)
		range->num_bitrates = GELICW_NUM_11B_BITRATES;
	else
		range->num_bitrates = ARRAY_SIZE(bitrate_list);
	range->throughput = bitrate_list[range->num_bitrates -1] / 2; /* half */
	for (i = 0; i < range->num_bitrates; i++)
		range->bitrate[i] = bitrate_list[i];

	range->max_qual.qual = 100; /* relative value */
	range->max_qual.level = 0;
	range->avg_qual.qual = 50;
	range->avg_qual.level = 0;
	range->sensitivity = 0;

	/* encryption capabilities */
	range->encoding_size[0] = 5;	/* 40bit WEP */
	range->encoding_size[1] = 13;	/* 104bit WEP */
	range->encoding_size[2] = 64;	/* WPA-PSK */
	range->num_encoding_sizes = 3;
	range->max_encoding_tokens = WEP_KEYS;
	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	/* freq */
	if (!w->ch_info)
		gelicw_cmd_get_ch_info(netdev); /* get supported freq */

	num_ch = 0;
	for (i = 0; i < ARRAY_SIZE(freq_list); i++)
		if (w->ch_info & (1 << i)) {
			range->freq[num_ch].i = i + 1;
			range->freq[num_ch].m = freq_list[i] * 100000;
			range->freq[num_ch].e = 1;
			if (++num_ch == IW_MAX_FREQUENCIES)
				break;
		}

	range->num_channels = num_ch;
	range->num_frequency = num_ch;

	/* event capabilities */
	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWAP));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;

	return 0;
}

static int gelicw_set_wap(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	static const u8 any[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	static const u8 off[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	dev_dbg(ntodev(netdev), "wx: set_wap\n");
	if (wrqu->ap_addr.sa_family != ARPHRD_ETHER)
		return -EINVAL;

	if (!memcmp(any, wrqu->ap_addr.sa_data, ETH_ALEN) ||
	    !memcmp(off, wrqu->ap_addr.sa_data, ETH_ALEN)) {
		if (!memcmp(off, w->wap_bssid, ETH_ALEN))
			return 0; /* ap off, no change */
		else {
			memset(w->wap_bssid, 0, ETH_ALEN);
			/* start scan */
		}
	} else if (!memcmp(w->wap_bssid, wrqu->ap_addr.sa_data, ETH_ALEN))
		/* no change */
		return 0;
	else if (!memcmp(w->bssid, wrqu->ap_addr.sa_data, ETH_ALEN)) {
		/* current bss */
		memcpy(w->wap_bssid, wrqu->ap_addr.sa_data, ETH_ALEN);
		return 0;
	} else
		memcpy(w->wap_bssid, wrqu->ap_addr.sa_data, ETH_ALEN);

	/* start scan */
	if (w->essid_len && w->state >= GELICW_STATE_SCAN_DONE) {
		gelicw_disassoc(netdev);
		/* scan essid */
		cancel_delayed_work(&w->work_scan_all);
		cancel_delayed_work(&w->work_scan_essid);
		w->essid_search = 1;
		schedule_delayed_work(&w->work_scan_essid, 0);
	}

	return 0;
}

static int gelicw_get_wap(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx: get_wap\n");
	wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(wrqu->ap_addr.sa_data, w->bssid, ETH_ALEN);

	return 0;
}

static int gelicw_set_scan(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx: set_scan\n");
	if (w->state < GELICW_STATE_UP)
		return -EIO;

	/* cancel scan */
	cancel_delayed_work(&w->work_scan_all);
	cancel_delayed_work(&w->work_scan_essid);

	schedule_delayed_work(&w->work_scan_all, 0);

	return 0;
}

#define MAX_CUSTOM_LEN 64
static char *gelicw_translate_scan(struct net_device *netdev,
				char *start, char *stop,
				struct gelicw_bss *list)
{
	char custom[MAX_CUSTOM_LEN];
	struct iw_event iwe;
	int i;
	char *p, *current_val;

	/* BSSID */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, list->bssid, ETH_ALEN);
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_ADDR_LEN);

	/* ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = list->essid_len;
	start = iwe_stream_add_point(start, stop, &iwe, list->essid);

	/* protocol name */
	iwe.cmd = SIOCGIWNAME;
	if (gelicw_is_ap_11b(list))
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	else
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_CHAR_LEN);

	/* MODE */
	iwe.cmd = SIOCGIWMODE;
	iwe.u.mode = list->mode;
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_UINT_LEN);

	/* FREQ */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = list->channel;
	iwe.u.freq.e = 0;
	iwe.u.freq.i = 0;
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_FREQ_LEN);

	/* ENCODE */
	iwe.cmd = SIOCGIWENCODE;
	if (list->capability & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(start, stop, &iwe, list->essid);

	/* QUAL */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated  = IW_QUAL_QUAL_UPDATED |
			IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID;
	iwe.u.qual.qual = list->rssi;
	iwe.u.qual.level = gelicw_qual2level(list->rssi);
	start = iwe_stream_add_event(start, stop, &iwe, IW_EV_QUAL_LEN);

	/* RATE */
	current_val = start + IW_EV_LCP_LEN;
	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	for (i = 0; i < list->rates_len; i++) {
		iwe.u.bitrate.value = ((list->rates[i] & 0x7f) * 500000);
		current_val = iwe_stream_add_value(start, current_val, stop,
					&iwe, IW_EV_PARAM_LEN);
	}
	for (i = 0; i < list->rates_ex_len; i++) {
		iwe.u.bitrate.value = ((list->rates_ex[i] & 0x7f) * 500000);
		current_val = iwe_stream_add_value(start, current_val, stop,
					&iwe, IW_EV_PARAM_LEN);
	}
	if ((current_val - start) > IW_EV_LCP_LEN)
		start = current_val;

	/* Extra */
	/* BEACON */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVCUSTOM;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN, "bcn_int=%d", list->beacon_interval);
	iwe.u.data.length = p - custom;
	start = iwe_stream_add_point(start, stop, &iwe, custom);

	/* AP security */
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVCUSTOM;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN, "ap_sec=%04X", list->sec_info);
	iwe.u.data.length = p - custom;
	start = iwe_stream_add_point(start, stop, &iwe, custom);

	return start;
}

static int gelicw_get_scan(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	int i;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;

	dev_dbg(ntodev(netdev), "wx: get_scan \n");
	switch (w->state) {
	case GELICW_STATE_DOWN:
	case GELICW_STATE_UP:
		return 0; /* no scan results */
	case GELICW_STATE_SCANNING:
		return -EAGAIN; /* now scanning */
	case GELICW_STATE_SCAN_DONE:
		if (!w->scan_all) /* essid scan */
			return -EAGAIN;
		break;
	default:
		break;
	}

	w->scan_all = 0;
	for (i = 0; i < w->num_bss_list; i++)
		ev = gelicw_translate_scan(netdev, ev, stop, &w->bss_list[i]);
	wrqu->data.length = ev - extra;
	wrqu->data.flags = 0;

	/* start background scan */
	if (w->essid_search)
		schedule_delayed_work(&w->work_scan_essid,
					GELICW_SCAN_INTERVAL);

	return 0;
}

static int gelicw_set_essid(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u16 length = 0;

	dev_dbg(ntodev(netdev), "wx:set_essid\n");
	/* cancel scan */
	w->essid_search = 0;
	cancel_delayed_work(&w->work_scan_all);
	cancel_delayed_work(&w->work_scan_essid);

	if (wrqu->essid.flags && wrqu->essid.length)
		length = wrqu->essid.length;

	if (length == 0) {
		/* essid ANY scan not supported */
		dev_dbg(ntodev(netdev), "ESSID ANY\n");
		w->essid_len = 0; /* clear essid */
		w->essid[0] = '\0';
		return 0;
	} else {
		/* check essid */
		if (length > IW_ESSID_MAX_SIZE)
			return -EINVAL;
		if (w->essid_len == length &&
		    !strncmp(w->essid, extra, length)) {
			/* same essid */
			if (w->is_assoc)
				return 0;
		} else {
			/* set new essid */
			w->essid_len = length;
			memcpy(w->essid, extra, length);
		}
	}
	/* start essid scan */
	w->essid_search = 1;
	schedule_delayed_work(&w->work_scan_essid, 0);

	return 0;
}

static int gelicw_get_essid(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx:get_essid\n");
	if (w->essid_len) {
		memcpy(extra, w->essid, w->essid_len);
		wrqu->essid.length = w->essid_len;
		wrqu->essid.flags = 1;
	} else {
		wrqu->essid.length = 0;
		wrqu->essid.flags = 0;
	}

	return 0;
}

static int gelicw_set_nick(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	u32 len = wrqu->data.length;

	dev_dbg(ntodev(netdev), "wx:set_nick\n");
	if (len > IW_ESSID_MAX_SIZE)
		return -EINVAL;

	memset(w->nick, 0, sizeof(w->nick));
	memcpy(w->nick, extra, len);

	return 0;
}

static int gelicw_get_nick(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx:get_nick\n");
	wrqu->data.length = strlen(w->nick);
	memcpy(extra, w->nick, wrqu->data.length);
	wrqu->data.flags = 1;

	return 0;
}

static int gelicw_set_rate(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	dev_dbg(ntodev(netdev), "wx:set_rate:%d\n", wrqu->bitrate.value);
	if (wrqu->bitrate.value == -1)
		return 0;	/* auto rate only */

	return -EOPNOTSUPP;
}

static int gelicw_get_rate(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx:get_rate\n");

	if (w->wireless_mode == IEEE_B ||
	    (w->is_assoc && gelicw_is_ap_11b(&w->current_bss)))
		wrqu->bitrate.value = bitrate_list[GELICW_NUM_11B_BITRATES -1];
	else
		wrqu->bitrate.value = bitrate_list[ARRAY_SIZE(bitrate_list) -1];

	wrqu->bitrate.fixed = 0;

	return 0;
}

static int gelicw_set_encode(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_point *enc = &wrqu->encoding;
	int i, index, key_index;

	dev_dbg(ntodev(netdev), "wx:set_encode: flags:%x\n", enc->flags );
	index = enc->flags & IW_ENCODE_INDEX;
	if (index < 0 || index > WEP_KEYS)
		return -EINVAL;
	index--;

	if (enc->length > IW_ENCODING_TOKEN_MAX)
		return -EINVAL;

	if (index != -1)
		w->key_index = index;
	key_index = w->key_index;

	if (enc->flags & IW_ENCODE_DISABLED) {
		/* disable encryption */
		if (index == -1) {
			/* disable all */
			w->key_alg = IW_ENCODE_ALG_NONE;
			for (i = 0; i < WEP_KEYS; i++)
				w->key_len[i] = 0;
		} else
			w->key_len[key_index] = 0;
	} else if (enc->flags & IW_ENCODE_NOKEY) {
		/* key not changed */
		if (w->key_alg == IW_ENCODE_ALG_NONE)
			w->key_alg = IW_ENCODE_ALG_WEP; /* default wep */
	} else {
		/* enable encryption */
		w->key_len[key_index] = enc->length;
		if (w->key_alg == IW_ENCODE_ALG_NONE)
			w->key_alg = IW_ENCODE_ALG_WEP; /* default wep */
		memcpy(w->key[key_index], extra, w->key_len[key_index]);
	}
	dev_dbg(ntodev(netdev), "key %d len:%d alg:%x\n",\
		key_index, w->key_len[key_index], w->key_alg);

	if (w->state >= GELICW_STATE_SCAN_DONE &&
	    w->cmd_send_flg == 0 && w->essid_len)
		/* scan essid and set other params */
		schedule_delayed_work(&w->work_scan_essid, 0);
	else
		schedule_delayed_work(&w->work_encode, 0);

	return 0;
}

static int gelicw_get_encode(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_point *enc = &wrqu->encoding;
	int index, key_index;

	dev_dbg(ntodev(netdev), "wx:get_encode\n");
	index = enc->flags & IW_ENCODE_INDEX;
	if (index < 0 || index > WEP_KEYS)
		return -EINVAL;

	index--;
	key_index = (index == -1 ? w->key_index : index);
	enc->flags = key_index + 1;

	if (w->key_alg == IW_ENCODE_ALG_NONE || !w->key_len[key_index]) {
		/* no encryption */
		enc->flags |= IW_ENCODE_DISABLED;
		enc->length = 0;
	} else {
		enc->flags |= IW_ENCODE_NOKEY;
		enc->length = w->key_len[key_index];
		memset(extra, 0, w->key_len[key_index]);
	}

	return 0;
}

static int gelicw_set_auth(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_param *param = &wrqu->param;
	int value = param->value;
	int ret = 0;

	dev_dbg(ntodev(netdev), "wx:set_auth:%x\n", param->flags & IW_AUTH_INDEX);
	switch(param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_CIPHER_PAIRWISE:
	case IW_AUTH_CIPHER_GROUP:
	case IW_AUTH_KEY_MGMT:
	case IW_AUTH_TKIP_COUNTERMEASURES:
	case IW_AUTH_DROP_UNENCRYPTED:
	case IW_AUTH_WPA_ENABLED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_ROAMING_CONTROL:
	case IW_AUTH_PRIVACY_INVOKED:
		/* ignore */
		dev_dbg(ntodev(netdev), "IW_AUTH(%x)\n", param->flags & IW_AUTH_INDEX);
		break;
	case IW_AUTH_80211_AUTH_ALG:
		dev_dbg(ntodev(netdev), "IW_AUTH_80211_AUTH_ALG:\n");
		if (value & IW_AUTH_ALG_SHARED_KEY)
			w->auth_mode = IW_AUTH_ALG_SHARED_KEY;
		else if (value & IW_AUTH_ALG_OPEN_SYSTEM)
			w->auth_mode = IW_AUTH_ALG_OPEN_SYSTEM;
		else
			ret = -EINVAL;
		break;
	default:
		dev_dbg(ntodev(netdev), "IW_AUTH_UNKNOWN flags:%x\n", param->flags);
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static int gelicw_get_auth(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_param *param = &wrqu->param;

	dev_dbg(ntodev(netdev), "wx:get_auth\n");
	switch(param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_80211_AUTH_ALG:
		param->value = w->auth_mode;
		break;
	case IW_AUTH_WPA_ENABLED:
		if ((w->key_alg & IW_ENCODE_ALG_TKIP) ||
		    (w->key_alg & IW_ENCODE_ALG_CCMP))
			param->value = 1;
		else
			param->value = 0;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int gelicw_set_encodeext(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_point *enc = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int i, index, key_index;

	dev_dbg(ntodev(netdev), "wx:set_encodeext\n");
	index = enc->flags & IW_ENCODE_INDEX;
	if (index < 0 || index > WEP_KEYS)
		return -EINVAL;

	index--;
	if (ext->key_len > IW_ENCODING_TOKEN_MAX)
		return -EINVAL;

	if (index != -1)
		w->key_index = index;
	key_index = w->key_index;

	if (enc->flags & IW_ENCODE_DISABLED) {
		/* disable encryption */
		if (index == -1) {
			/* disable all */
			w->key_alg = IW_ENCODE_ALG_NONE;
			for (i = 0; i < WEP_KEYS; i++)
				w->key_len[i] = 0;
		} else
			w->key_len[key_index] = 0;
	} else if (enc->flags & IW_ENCODE_NOKEY)
		/* key not changed */
		w->key_alg = ext->alg;
	else {
		w->key_len[key_index] = ext->key_len;
		w->key_alg = ext->alg;
		if (w->key_alg != IW_ENCODE_ALG_NONE && w->key_len[key_index])
			memcpy(w->key[key_index], ext->key, w->key_len[key_index]);
	}
	dev_dbg(ntodev(netdev), "key %d len:%d alg:%x\n",\
		key_index, w->key_len[key_index], w->key_alg);

	if (w->state >= GELICW_STATE_SCAN_DONE &&
	    w->cmd_send_flg == 0 && w->essid_len)
		/* scan essid and set other params */
		schedule_delayed_work(&w->work_scan_essid, 0);
	else
		schedule_delayed_work(&w->work_encode, 0);

	return 0;
}

static int gelicw_get_encodeext(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	struct iw_point *enc = &wrqu->encoding;
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	int index, key_index, key_len;

	dev_dbg(ntodev(netdev), "wx:get_encodeext\n");
	key_len = enc->length - sizeof(*ext);
	if (key_len < 0)
		return -EINVAL;

	index = enc->flags & IW_ENCODE_INDEX;
	if (index < 0 || index > WEP_KEYS)
		return -EINVAL;

	index--;
	key_index = (index == -1 ? w->key_index : index);

	memset(ext, 0, sizeof(*ext));
	enc->flags = key_index + 1;

	if (w->key_alg == IW_ENCODE_ALG_NONE || !w->key_len[key_index]) {
		/* no encryption */
		enc->flags |= IW_ENCODE_DISABLED;
		ext->alg = IW_ENCODE_ALG_NONE;
		ext->key_len = 0;
	} else {
		enc->flags |= IW_ENCODE_NOKEY;
		ext->alg = w->key_alg;
		ext->key_len = w->key_len[key_index];
	}

	return 0;
}

/*
 * wireless stats
 */
static struct iw_statistics *gelicw_get_wireless_stats(struct net_device *netdev)
{
	static struct iw_statistics wstats;
	struct gelic_wireless *w = gelicw_priv(netdev);

	dev_dbg(ntodev(netdev), "wx:wireless_stats\n");
	if (w->state < GELICW_STATE_ASSOCIATED) {
		wstats.qual.updated  = IW_QUAL_QUAL_UPDATED |
				IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID;
		wstats.qual.qual = 0;
		wstats.qual.level = 0;
		return &wstats;
	}
	init_completion(&w->rssi_done);
	schedule_delayed_work(&w->work_rssi, 0);

	wait_for_completion_interruptible(&w->rssi_done);
	wstats.qual.updated  = IW_QUAL_QUAL_UPDATED |
			IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID;
	wstats.qual.qual = w->rssi;
	wstats.qual.level = gelicw_qual2level(w->rssi);

	return &wstats;
}

/*
 * private handler
 */
static int gelicw_priv_set_alg_mode(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	int mode = *(int *)extra;

	dev_dbg(ntodev(netdev), "wx:priv_set_alg\n");
	switch (mode) {
	case IW_ENCODE_ALG_NONE:
	case IW_ENCODE_ALG_WEP:
	case IW_ENCODE_ALG_TKIP:
	case IW_ENCODE_ALG_CCMP:
		break;
	default:
		return -EINVAL;
	}
	/* send common config */
	gelicw_send_common_config(netdev, &w->key_alg, (u8)mode);

	return 0;
}

static int gelicw_priv_get_alg_mode(struct net_device *netdev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct gelic_wireless *w = gelicw_priv(netdev);
	char *p;

	dev_dbg(ntodev(netdev), "wx:priv_get_alg\n");
	switch (w->key_alg) {
	case IW_ENCODE_ALG_NONE:
		strncpy(extra, "OFF", MAX_IW_PRIV_SIZE);
		break;
	case IW_ENCODE_ALG_WEP:
		strncpy(extra, "WEP", MAX_IW_PRIV_SIZE);
		break;
	case IW_ENCODE_ALG_TKIP:
		strncpy(extra, "TKIP", MAX_IW_PRIV_SIZE);
		break;
	case IW_ENCODE_ALG_CCMP:
		strncpy(extra, "AES-CCMP", MAX_IW_PRIV_SIZE);
		break;
	default:
		break;
	}
	p = extra + strlen(extra);

	if (w->key_alg == IW_ENCODE_ALG_TKIP ||
	    w->key_alg == IW_ENCODE_ALG_CCMP) {
		if (w->key_len[w->key_index] == 64) /* current key index */
			strncpy(p, " hex", MAX_IW_PRIV_SIZE);
		else
			strncpy(p, " passphrase", MAX_IW_PRIV_SIZE);
	}
	wrqu->data.length = strlen(extra);

	return 0;
}


/*
 * Wireless handlers
 */
static const iw_handler gelicw_handler[] =
{
	[IW_IOCTL_IDX(SIOCGIWNAME)]      = gelicw_get_name,
	[IW_IOCTL_IDX(SIOCSIWFREQ)]      = gelicw_set_freq,
	[IW_IOCTL_IDX(SIOCGIWFREQ)]      = gelicw_get_freq,
	[IW_IOCTL_IDX(SIOCSIWMODE)]      = gelicw_set_mode,
	[IW_IOCTL_IDX(SIOCGIWMODE)]      = gelicw_get_mode,
	[IW_IOCTL_IDX(SIOCGIWRANGE)]     = gelicw_get_range,
	[IW_IOCTL_IDX(SIOCSIWAP)]        = gelicw_set_wap,
	[IW_IOCTL_IDX(SIOCGIWAP)]        = gelicw_get_wap,
	[IW_IOCTL_IDX(SIOCSIWSCAN)]      = gelicw_set_scan,
	[IW_IOCTL_IDX(SIOCGIWSCAN)]      = gelicw_get_scan,
	[IW_IOCTL_IDX(SIOCSIWESSID)]     = gelicw_set_essid,
	[IW_IOCTL_IDX(SIOCGIWESSID)]     = gelicw_get_essid,
	[IW_IOCTL_IDX(SIOCSIWNICKN)]     = gelicw_set_nick,
	[IW_IOCTL_IDX(SIOCGIWNICKN)]     = gelicw_get_nick,
	[IW_IOCTL_IDX(SIOCSIWRATE)]      = gelicw_set_rate,
	[IW_IOCTL_IDX(SIOCGIWRATE)]      = gelicw_get_rate,
	[IW_IOCTL_IDX(SIOCSIWENCODE)]    = gelicw_set_encode,
	[IW_IOCTL_IDX(SIOCGIWENCODE)]    = gelicw_get_encode,
	[IW_IOCTL_IDX(SIOCSIWAUTH)]      = gelicw_set_auth,
	[IW_IOCTL_IDX(SIOCGIWAUTH)]      = gelicw_get_auth,
	[IW_IOCTL_IDX(SIOCSIWENCODEEXT)] = gelicw_set_encodeext,
	[IW_IOCTL_IDX(SIOCGIWENCODEEXT)] = gelicw_get_encodeext,
};

/*
 * Private wireless handlers
 */
enum {
	GELICW_PRIV_SET_AUTH  = SIOCIWFIRSTPRIV,
	GELICW_PRIV_GET_AUTH
};

static struct iw_priv_args gelicw_private_args[] = {
	{
	 .cmd = GELICW_PRIV_SET_AUTH,
	 .set_args = IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 .name = "set_alg"
	},
	{
	 .cmd = GELICW_PRIV_GET_AUTH,
	 .get_args = IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_IW_PRIV_SIZE,
	 .name = "get_alg"
	},
};

static const iw_handler gelicw_private_handler[] =
{
	gelicw_priv_set_alg_mode,
	gelicw_priv_get_alg_mode,
};

static struct iw_handler_def gelicw_handler_def =
{
	.num_standard	= ARRAY_SIZE(gelicw_handler),
	.num_private	= ARRAY_SIZE(gelicw_private_handler),
	.num_private_args = ARRAY_SIZE(gelicw_private_args),
	.standard	= (iw_handler *)gelicw_handler,
	.private	= (iw_handler *)gelicw_private_handler,
	.private_args	= (struct iw_priv_args *)gelicw_private_args,
	.get_wireless_stats = gelicw_get_wireless_stats
};
