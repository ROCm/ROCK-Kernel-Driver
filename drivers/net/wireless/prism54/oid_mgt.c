/*   
 *  Copyright (C) 2003 Aurelien Alleaume <slts@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "islpci_dev.h"
#include "islpci_mgt.h"
#include "isl_oid.h"
#include "oid_mgt.h"
#include "isl_ioctl.h"

/* to convert between channel and freq */
const int frequency_list_bg[] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};

const int frequency_list_a[] = { 5170, 5180, 5190, 5200, 5210, 5220, 5230,
	5240, 5260, 5280, 5300, 5320
};

#define OID_U32(x) {x, 0, sizeof(u32), OID_FLAG_U32}
#define OID_U32_C(x) {x, 0, sizeof(u32), OID_FLAG_U32 | OID_FLAG_CACHED}
#define OID_STRUCT(x,s) {x, 0, sizeof(s), 0}
#define OID_STRUCT_C(x,s) {x, 0, sizeof(s), OID_FLAG_CACHED}
#define OID_STRUCT_MLME(x){x, 0, sizeof(struct obj_mlme), 0}
#define OID_STRUCT_MLMEEX(x){x, 0, sizeof(struct obj_mlmeex), OID_FLAG_MLMEEX}

#define OID_UNKNOWN(x) {x, 0, 0, 0}

struct oid_t isl_oid[] = {
	[GEN_OID_MACADDRESS] = OID_STRUCT(0x00000000, u8[6]),
	[GEN_OID_LINKSTATE] = OID_U32(0x00000001),
	[GEN_OID_WATCHDOG] = OID_UNKNOWN(0x00000002),
	[GEN_OID_MIBOP] = OID_UNKNOWN(0x00000003),
	[GEN_OID_OPTIONS] = OID_UNKNOWN(0x00000004),
	[GEN_OID_LEDCONFIG] = OID_UNKNOWN(0x00000005),

	/* 802.11 */
	[DOT11_OID_BSSTYPE] = OID_U32_C(0x10000000),
	[DOT11_OID_BSSID] = OID_STRUCT_C(0x10000001, u8[6]),
	[DOT11_OID_SSID] = OID_STRUCT_C(0x10000002, struct obj_ssid),
	[DOT11_OID_STATE] = OID_U32(0x10000003),
	[DOT11_OID_AID] = OID_U32(0x10000004),
	[DOT11_OID_COUNTRYSTRING] = OID_STRUCT(0x10000005, u8[4]),
	[DOT11_OID_SSIDOVERRIDE] = OID_STRUCT_C(0x10000006, struct obj_ssid),

	[DOT11_OID_MEDIUMLIMIT] = OID_U32(0x11000000),
	[DOT11_OID_BEACONPERIOD] = OID_U32_C(0x11000001),
	[DOT11_OID_DTIMPERIOD] = OID_U32(0x11000002),
	[DOT11_OID_ATIMWINDOW] = OID_U32(0x11000003),
	[DOT11_OID_LISTENINTERVAL] = OID_U32(0x11000004),
	[DOT11_OID_CFPPERIOD] = OID_U32(0x11000005),
	[DOT11_OID_CFPDURATION] = OID_U32(0x11000006),

	[DOT11_OID_AUTHENABLE] = OID_U32_C(0x12000000),
	[DOT11_OID_PRIVACYINVOKED] = OID_U32_C(0x12000001),
	[DOT11_OID_EXUNENCRYPTED] = OID_U32_C(0x12000002),
	[DOT11_OID_DEFKEYID] = OID_U32_C(0x12000003),
	[DOT11_OID_DEFKEYX] = {0x12000004, 3, sizeof (struct obj_key), OID_FLAG_CACHED},	/* DOT11_OID_DEFKEY1,...DOT11_OID_DEFKEY4 */
	[DOT11_OID_STAKEY] = OID_UNKNOWN(0x12000008),
	[DOT11_OID_REKEYTHRESHOLD] = OID_U32(0x12000009),
	[DOT11_OID_STASC] = OID_UNKNOWN(0x1200000a),

	[DOT11_OID_PRIVTXREJECTED] = OID_U32(0x1a000000),
	[DOT11_OID_PRIVRXPLAIN] = OID_U32(0x1a000001),
	[DOT11_OID_PRIVRXFAILED] = OID_U32(0x1a000002),
	[DOT11_OID_PRIVRXNOKEY] = OID_U32(0x1a000003),

	[DOT11_OID_RTSTHRESH] = OID_U32_C(0x13000000),
	[DOT11_OID_FRAGTHRESH] = OID_U32_C(0x13000001),
	[DOT11_OID_SHORTRETRIES] = OID_U32_C(0x13000002),
	[DOT11_OID_LONGRETRIES] = OID_U32_C(0x13000003),
	[DOT11_OID_MAXTXLIFETIME] = OID_U32_C(0x13000004),
	[DOT11_OID_MAXRXLIFETIME] = OID_U32(0x13000005),
	[DOT11_OID_AUTHRESPTIMEOUT] = OID_U32(0x13000006),
	[DOT11_OID_ASSOCRESPTIMEOUT] = OID_U32(0x13000007),

	[DOT11_OID_ALOFT_TABLE] = OID_UNKNOWN(0x1d000000),
	[DOT11_OID_ALOFT_CTRL_TABLE] = OID_UNKNOWN(0x1d000001),
	[DOT11_OID_ALOFT_RETREAT] = OID_UNKNOWN(0x1d000002),
	[DOT11_OID_ALOFT_PROGRESS] = OID_UNKNOWN(0x1d000003),
	[DOT11_OID_ALOFT_FIXEDRATE] = OID_U32(0x1d000004),
	[DOT11_OID_ALOFT_RSSIGRAPH] = OID_UNKNOWN(0x1d000005),
	[DOT11_OID_ALOFT_CONFIG] = OID_UNKNOWN(0x1d000006),

	[DOT11_OID_VDCFX] = {0x1b000000, 7, 0, 0},
	[DOT11_OID_MAXFRAMEBURST] = OID_U32(0x1b000008),

	[DOT11_OID_PSM] = OID_U32(0x14000000),
	[DOT11_OID_CAMTIMEOUT] = OID_U32(0x14000001),
	[DOT11_OID_RECEIVEDTIMS] = OID_U32(0x14000002),
	[DOT11_OID_ROAMPREFERENCE] = OID_U32(0x14000003),

	[DOT11_OID_BRIDGELOCAL] = OID_U32(0x15000000),
	[DOT11_OID_CLIENTS] = OID_U32(0x15000001),
	[DOT11_OID_CLIENTSASSOCIATED] = OID_U32(0x15000002),
	[DOT11_OID_CLIENTX] = {0x15000003, 2006, 0, 0},	/* DOT11_OID_CLIENTX,...DOT11_OID_CLIENT2007 */

	[DOT11_OID_CLIENTFIND] = OID_STRUCT(0x150007DB, u8[6]),
	[DOT11_OID_WDSLINKADD] = OID_STRUCT(0x150007DC, u8[6]),
	[DOT11_OID_WDSLINKREMOVE] = OID_STRUCT(0x150007DD, u8[6]),
	[DOT11_OID_EAPAUTHSTA] = OID_STRUCT(0x150007DE, u8[6]),
	[DOT11_OID_EAPUNAUTHSTA] = OID_STRUCT(0x150007DF, u8[6]),
	[DOT11_OID_DOT1XENABLE] = OID_U32_C(0x150007E0),
	[DOT11_OID_MICFAILURE] = OID_UNKNOWN(0x150007E1),
	[DOT11_OID_REKEYINDICATE] = OID_UNKNOWN(0x150007E2),

	[DOT11_OID_MPDUTXSUCCESSFUL] = OID_U32(0x16000000),
	[DOT11_OID_MPDUTXONERETRY] = OID_U32(0x16000001),
	[DOT11_OID_MPDUTXMULTIPLERETRIES] = OID_U32(0x16000002),
	[DOT11_OID_MPDUTXFAILED] = OID_U32(0x16000003),
	[DOT11_OID_MPDURXSUCCESSFUL] = OID_U32(0x16000004),
	[DOT11_OID_MPDURXDUPS] = OID_U32(0x16000005),
	[DOT11_OID_RTSSUCCESSFUL] = OID_U32(0x16000006),
	[DOT11_OID_RTSFAILED] = OID_U32(0x16000007),
	[DOT11_OID_ACKFAILED] = OID_U32(0x16000008),
	[DOT11_OID_FRAMERECEIVES] = OID_U32(0x16000009),
	[DOT11_OID_FRAMEERRORS] = OID_U32(0x1600000A),
	[DOT11_OID_FRAMEABORTS] = OID_U32(0x1600000B),
	[DOT11_OID_FRAMEABORTSPHY] = OID_U32(0x1600000C),

	[DOT11_OID_SLOTTIME] = OID_U32(0x17000000),
	[DOT11_OID_CWMIN] = OID_U32(0x17000001),
	[DOT11_OID_CWMAX] = OID_U32(0x17000002),
	[DOT11_OID_ACKWINDOW] = OID_U32(0x17000003),
	[DOT11_OID_ANTENNARX] = OID_U32(0x17000004),
	[DOT11_OID_ANTENNATX] = OID_U32(0x17000005),
	[DOT11_OID_ANTENNADIVERSITY] = OID_U32(0x17000006),
	[DOT11_OID_CHANNEL] = OID_U32_C(0x17000007),
	[DOT11_OID_EDTHRESHOLD] = OID_U32_C(0x17000008),
	[DOT11_OID_PREAMBLESETTINGS] = OID_U32(0x17000009),
	[DOT11_OID_RATES] = OID_STRUCT(0x1700000A, u8[IWMAX_BITRATES + 1]),
	[DOT11_OID_CCAMODESUPPORTED] = OID_U32(0x1700000B),
	[DOT11_OID_CCAMODE] = OID_U32(0x1700000C),
	[DOT11_OID_RSSIVECTOR] = OID_U32(0x1700000D),
	[DOT11_OID_OUTPUTPOWERTABLE] = OID_U32(0x1700000E),
	[DOT11_OID_OUTPUTPOWER] = OID_U32_C(0x1700000F),
	[DOT11_OID_SUPPORTEDRATES] =
	    OID_STRUCT(0x17000010, u8[IWMAX_BITRATES + 1]),
	[DOT11_OID_FREQUENCY] = OID_U32_C(0x17000011),
	[DOT11_OID_SUPPORTEDFREQUENCIES] = {0x17000012, 0, sizeof (struct
								   obj_frequencies)
					    + sizeof (u16) * IWMAX_FREQ, 0},

	[DOT11_OID_NOISEFLOOR] = OID_U32(0x17000013),
	[DOT11_OID_FREQUENCYACTIVITY] =
	    OID_STRUCT(0x17000014, u8[IWMAX_FREQ + 1]),
	[DOT11_OID_IQCALIBRATIONTABLE] = OID_UNKNOWN(0x17000015),
	[DOT11_OID_NONERPPROTECTION] = OID_U32(0x17000016),
	[DOT11_OID_SLOTSETTINGS] = OID_U32(0x17000017),
	[DOT11_OID_NONERPTIMEOUT] = OID_U32(0x17000018),
	[DOT11_OID_PROFILES] = OID_U32(0x17000019),
	[DOT11_OID_EXTENDEDRATES] =
	    OID_STRUCT(0x17000020, u8[IWMAX_BITRATES + 1]),

	[DOT11_OID_DEAUTHENTICATE] = OID_STRUCT_MLME(0x18000000),
	[DOT11_OID_AUTHENTICATE] = OID_STRUCT_MLME(0x18000001),
	[DOT11_OID_DISASSOCIATE] = OID_STRUCT_MLME(0x18000002),
	[DOT11_OID_ASSOCIATE] = OID_STRUCT_MLME(0x18000003),
	[DOT11_OID_SCAN] = OID_UNKNOWN(0x18000004),
	[DOT11_OID_BEACON] = OID_STRUCT_MLMEEX(0x18000005),
	[DOT11_OID_PROBE] = OID_STRUCT_MLMEEX(0x18000006),
	[DOT11_OID_DEAUTHENTICATEEX] = OID_STRUCT_MLMEEX(0x18000007),
	[DOT11_OID_AUTHENTICATEEX] = OID_STRUCT_MLMEEX(0x18000008),
	[DOT11_OID_DISASSOCIATEEX] = OID_STRUCT_MLMEEX(0x18000009),
	[DOT11_OID_ASSOCIATEEX] = OID_STRUCT_MLMEEX(0x1800000A),
	[DOT11_OID_REASSOCIATE] = OID_STRUCT_MLMEEX(0x1800000B),
	[DOT11_OID_REASSOCIATEEX] = OID_STRUCT_MLMEEX(0x1800000C),

	[DOT11_OID_NONERPSTATUS] = OID_U32(0x1E000000),

	[DOT11_OID_STATIMEOUT] = OID_U32(0x19000000),
	[DOT11_OID_MLMEAUTOLEVEL] = OID_U32_C(0x19000001),
	[DOT11_OID_BSSTIMEOUT] = OID_U32(0x19000002),
	[DOT11_OID_ATTACHMENT] = OID_UNKNOWN(0x19000003),
	[DOT11_OID_PSMBUFFER] = OID_STRUCT_C(0x19000004, struct obj_buffer),

	[DOT11_OID_BSSS] = OID_U32(0x1C000000),
	[DOT11_OID_BSSX] = {0x1C000001, 63, sizeof (struct obj_bss), 0},	/*DOT11_OID_BSS1,...,DOT11_OID_BSS64 */
	[DOT11_OID_BSSFIND] = OID_STRUCT(0x1C000042, struct obj_bss),
	[DOT11_OID_BSSLIST] = {0x1C000043, 0, sizeof (struct
						      obj_bsslist) +
			       sizeof (struct obj_bss[IWMAX_BSS]), 0},

	[OID_INL_TUNNEL] = OID_UNKNOWN(0xFF020000),
	[OID_INL_MEMADDR] = OID_UNKNOWN(0xFF020001),
	[OID_INL_MEMORY] = OID_UNKNOWN(0xFF020002),
	[OID_INL_MODE] = OID_U32_C(0xFF020003),
	[OID_INL_COMPONENT_NR] = OID_UNKNOWN(0xFF020004),
	[OID_INL_VERSION] = OID_UNKNOWN(0xFF020005),
	[OID_INL_INTERFACE_ID] = OID_UNKNOWN(0xFF020006),
	[OID_INL_COMPONENT_ID] = OID_UNKNOWN(0xFF020007),
	[OID_INL_CONFIG] = OID_U32_C(0xFF020008),
	[OID_INL_DOT11D_CONFORMANCE] = OID_U32_C(0xFF02000C),
	[OID_INL_PHYCAPABILITIES] = OID_U32(0xFF02000D),
	[OID_INL_OUTPUTPOWER] = OID_U32_C(0xFF02000F),

};

int
mgt_init(islpci_private *priv)
{
	int i;

	priv->mib = kmalloc(OID_NUM_LAST * sizeof (void *), GFP_KERNEL);
	if (!priv->mib)
		return -ENOMEM;

	memset(priv->mib, 0, OID_NUM_LAST * sizeof (void *));

	/* Alloc the cache */
	for (i = 0; i < OID_NUM_LAST; i++) {
		if (isl_oid[i].flags & OID_FLAG_CACHED) {
			priv->mib[i] = kmalloc(isl_oid[i].size *
					       (isl_oid[i].range + 1),
					       GFP_KERNEL);
			if (!priv->mib[i])
				return -ENOMEM;
			memset(priv->mib[i], 0,
			       isl_oid[i].size * (isl_oid[i].range + 1));
		} else
			priv->mib[i] = NULL;
	}

	init_rwsem(&priv->mib_sem);
	prism54_mib_init(priv);

	return 0;
}

void
mgt_clean(islpci_private *priv)
{
	int i;

	if (!priv->mib)
		return;
	for (i = 0; i < OID_NUM_LAST; i++)
		if (priv->mib[i]) {
			kfree(priv->mib[i]);
			priv->mib[i] = NULL;
		}
	kfree(priv->mib);
	priv->mib = NULL;
}

int
mgt_set_request(islpci_private *priv, enum oid_num_t n, int extra, void *data)
{
	int ret = 0;
	struct islpci_mgmtframe *response;
	int response_op = PIMFOR_OP_ERROR;
	int dlen;
	void *cache, *_data = data;
	u32 oid, u;

	BUG_ON(OID_NUM_LAST <= n);
	BUG_ON(extra > isl_oid[n].range);

	if (!priv->mib)
		/* memory has been freed */
		return -1;

	dlen = isl_oid[n].size;
	cache = priv->mib[n];
	cache += (cache ? extra * dlen : 0);
	oid = isl_oid[n].oid + extra;

	if (data == NULL)
		/* we are requested to re-set a cached value */
		_data = cache;
	if ((isl_oid[n].flags & OID_FLAG_U32) && data) {
		u = cpu_to_le32(*(u32 *) data);
		_data = &u;
	}
	/* If we are going to write to the cache, we don't want anyone to read
	 * it -> acquire write lock.
	 * Else we could acquire a read lock to be sure we don't bother the
	 * commit process (which takes a write lock). But I'm not sure if it's
	 * needed.
	 */
	if (cache)
		down_write(&priv->mib_sem);

	if (islpci_get_state(priv) >= PRV_STATE_INIT) {
		ret = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_SET, oid,
					     _data, dlen, &response);
		if (!ret) {
			response_op = response->header->operation;
			islpci_mgt_release(response);
		}
		if (ret || response_op == PIMFOR_OP_ERROR)
		        ret = -EIO;
	} else if (!cache)
		ret = -EIO;

	if (cache) {
		if (!ret && data)
			memcpy(cache, _data, dlen);
		up_write(&priv->mib_sem);
	}

	return ret;
}

int
mgt_get_request(islpci_private *priv, enum oid_num_t n, int extra, void *data,
		union oid_res_t *res)
{

	int ret = -EIO;
	int reslen = 0;
	struct islpci_mgmtframe *response = NULL;
	
	int dlen;
	void *cache, *_res=NULL;
	u32 oid;

	BUG_ON(OID_NUM_LAST <= n);
	BUG_ON(extra > isl_oid[n].range);

	if (!priv->mib)
		/* memory has been freed */
		return -1;

	dlen = isl_oid[n].size;
	cache = priv->mib[n];
	cache += cache ? extra * dlen : 0;
	oid = isl_oid[n].oid + extra;
	reslen = dlen;

	if (cache)
		down_read(&priv->mib_sem);

	if (islpci_get_state(priv) >= PRV_STATE_INIT) {
		ret = islpci_mgt_transaction(priv->ndev, PIMFOR_OP_GET,
					     oid, data, dlen, &response);
		if (ret || !response ||
			response->header->operation == PIMFOR_OP_ERROR) {
			if (response)
				islpci_mgt_release(response);
			ret = -EIO;
		}
		if (!ret) {
			_res = response->data;
			reslen = response->header->length;
		}
	} else if (cache) {
		_res = cache;
		ret = 0;
	}
	if (isl_oid[n].flags & OID_FLAG_U32) {
		if (ret)
			res->u = 0;
		else
			res->u = le32_to_cpu(*(u32 *) _res);
	} else {
		res->ptr = kmalloc(reslen, GFP_KERNEL);
		BUG_ON(res->ptr == NULL);
		if (ret)
			memset(res->ptr, 0, reslen);
		else
			memcpy(res->ptr, _res, reslen);
	}

	if (cache)
		up_read(&priv->mib_sem);

	if (response && !ret)
		islpci_mgt_release(response);

	if (reslen > isl_oid[n].size)
		printk(KERN_DEBUG
		       "mgt_get_request(0x%x): received data length was bigger "
		       "than expected (%d > %d). Memory is probably corrupted... ",
		       oid, reslen, isl_oid[n].size);
	
	return ret;
}

/* lock outside */
int
mgt_commit_list(islpci_private *priv, enum oid_num_t *l, int n)
{
	int i, ret = 0;
	struct islpci_mgmtframe *response;

	for (i = 0; i < n; i++) {
		struct oid_t *t = &(isl_oid[l[i]]);
		void *data = priv->mib[l[i]];
		int j = 0;
		u32 oid = t->oid;
		BUG_ON(data == NULL);
		while (j <= t->range){
			response = NULL;
			ret |= islpci_mgt_transaction(priv->ndev, PIMFOR_OP_SET,
			                              oid, data, t->size,
						      &response);
			if (response) {
				ret |= (response->header->operation ==
				        PIMFOR_OP_ERROR);
				islpci_mgt_release(response);
			}
			j++;
			oid++;
			data += t->size;
		}
	}
	return ret;
}

/* Lock outside */

void
mgt_set(islpci_private *priv, enum oid_num_t n, void *data)
{
	BUG_ON(OID_NUM_LAST <= n);
	BUG_ON(priv->mib[n] == NULL);

	memcpy(priv->mib[n], data, isl_oid[n].size);
	if (isl_oid[n].flags & OID_FLAG_U32)
		*(u32 *) priv->mib[n] = cpu_to_le32(*(u32 *) priv->mib[n]);
}

/* Commits the cache. If something goes wrong, it restarts the device. Lock
 * outside
 */

static enum oid_num_t commit_part1[] = {
	OID_INL_CONFIG,
	OID_INL_MODE,
	DOT11_OID_BSSTYPE,
	DOT11_OID_CHANNEL,
	DOT11_OID_MLMEAUTOLEVEL
};

static enum oid_num_t commit_part2[] = {
	DOT11_OID_SSID,
	DOT11_OID_PSMBUFFER,
	DOT11_OID_AUTHENABLE,
	DOT11_OID_PRIVACYINVOKED,
	DOT11_OID_EXUNENCRYPTED,
	DOT11_OID_DEFKEYX,	/* MULTIPLE */
	DOT11_OID_DEFKEYID,
	DOT11_OID_DOT1XENABLE,
	OID_INL_DOT11D_CONFORMANCE,
	OID_INL_OUTPUTPOWER,
};

void
mgt_commit(islpci_private *priv)
{
	int rvalue;
	u32 u;
	union oid_res_t r;

	if (islpci_get_state(priv) < PRV_STATE_INIT)
		return;

	rvalue = mgt_commit_list(priv, commit_part1,
				 sizeof (commit_part1) /
				 sizeof (commit_part1[0]));

	if (priv->iw_mode != IW_MODE_MONITOR)
		rvalue |= mgt_commit_list(priv, commit_part2,
					  sizeof (commit_part2) /
					  sizeof (commit_part2[0]));

	u = OID_INL_MODE;
	rvalue |= mgt_commit_list(priv, &u, 1);

	if (rvalue) {
		/* some request have failed. The device might be in an
		   incoherent state. We should reset it ! */
		printk(KERN_DEBUG "%s: mgt_commit has failed. Restart the "
                "device \n", priv->ndev->name);
	}

	/* update the MAC addr. As it's not cached, no lock will be acquired by
	 * the mgt_get_request
	 */
	mgt_get_request(priv, GEN_OID_MACADDRESS, 0, NULL, &r);
	memcpy(priv->ndev->dev_addr, r.ptr, 6);
	kfree(r.ptr);

}

/* This will tell you if you are allowed to answer a mlme(ex) request .*/

inline int
mgt_mlme_answer(islpci_private *priv)
{
	u32 mlmeautolevel;
	/* Acquire a read lock because if we are in a mode change, it's
	 * possible to answer true, while the card is leaving master to managed
	 * mode. Answering to a mlme in this situation could hang the card.
	 */
	down_read(&priv->mib_sem);
	mlmeautolevel =
	    le32_to_cpu(*(u32 *) priv->mib[DOT11_OID_MLMEAUTOLEVEL]);
	up_read(&priv->mib_sem);

	return ((priv->iw_mode == IW_MODE_MASTER) &&
		(mlmeautolevel >= DOT11_MLME_INTERMEDIATE));
}

inline enum oid_num_t
mgt_oidtonum(u32 oid)
{
	int i;

	for (i = 0; i < OID_NUM_LAST - 1; i++)
		if (isl_oid[i].oid == oid)
			return i;

	printk(KERN_DEBUG "looking for an unknown oid 0x%x", oid);

	return 0;
}
