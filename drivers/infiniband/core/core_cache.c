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

  $Id: core_cache.c 70 2004-04-21 03:47:12Z roland $
*/

#include "core_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>

int ib_cached_node_guid_get(tTS_IB_DEVICE_HANDLE device_handle,
			    tTS_IB_GUID          node_guid)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;
	memcpy(node_guid, priv->node_guid, sizeof (tTS_IB_GUID));

	return 0;
}

int ib_cached_port_properties_get(tTS_IB_DEVICE_HANDLE       device_handle,
				  tTS_IB_PORT                port,
				  struct ib_port_properties *properties)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;
	unsigned int seq;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port)
		return -EINVAL;

	do {
		seq = tsSeqLockReadBegin(&priv->port_data[port].lock);
		memcpy(properties,
		       &priv->port_data[port].properties,
		       sizeof (struct ib_port_properties));
	} while (tsSeqLockReadEnd(&priv->port_data[port].lock, seq));

	return 0;
}

int ib_cached_sm_path_get(tTS_IB_DEVICE_HANDLE device_handle,
			  tTS_IB_PORT          port,
			  struct ib_sm_path   *sm_path)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;
	unsigned int seq;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port)
		return -EINVAL;

	do {
		seq = tsSeqLockReadBegin(&priv->port_data[port].lock);
		memcpy(sm_path,
		       &priv->port_data[port].sm_path,
		       sizeof (struct ib_sm_path));
	} while (tsSeqLockReadEnd(&priv->port_data[port].lock, seq));

	return 0;
}

int ib_cached_lid_get(tTS_IB_DEVICE_HANDLE device_handle,
		      tTS_IB_PORT          port,
		      struct ib_port_lid  *port_lid)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;
	unsigned int seq;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port)
		return -EINVAL;

	do {
		seq = tsSeqLockReadBegin(&priv->port_data[port].lock);
		memcpy(port_lid,
		       &priv->port_data[port].port_lid,
		       sizeof (struct ib_port_lid));
	} while (tsSeqLockReadEnd(&priv->port_data[port].lock, seq));

	return 0;
}

int ib_cached_gid_get(tTS_IB_DEVICE_HANDLE device_handle,
		      tTS_IB_PORT          port,
		      int                  index,
		      tTS_IB_GID           gid)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;
	unsigned int seq;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port)
		return -EINVAL;

	if (index < 0 || index >= priv->port_data[port].properties.gid_table_length)
		return -EINVAL;

	do {
		seq = tsSeqLockReadBegin(&priv->port_data[port].lock);
		memcpy(gid,
		       priv->port_data[port].gid_table[index],
		       sizeof (tTS_IB_GID));
	} while (tsSeqLockReadEnd(&priv->port_data[port].lock, seq));

	return 0;
}

int ib_cached_gid_find(tTS_IB_GID            gid,
		       tTS_IB_DEVICE_HANDLE *device,
		       tTS_IB_PORT          *port,
		       int                  *index)
{
	struct ib_device         *d;
	struct ib_device_private *priv;
	tTS_IB_PORT p;
	int i = 0;
	int j;
	int f;
	unsigned int seq;

	while ((d = ib_device_get_by_index(i++)) != TS_IB_HANDLE_INVALID) {
		priv = d->core;

		for (p = priv->start_port; p <= priv->end_port; ++p) {
			do {
				seq = tsSeqLockReadBegin(&priv->port_data[p].lock);
				f = 0;
				for (j = 0;
				     j < priv->port_data[p].properties.gid_table_length;
				     ++j) {
					if (!memcmp(gid,
						    priv->port_data[p].gid_table[j],
						    sizeof (tTS_IB_GID))) {
						f = 1;
						break;
					}
				}
			} while (tsSeqLockReadEnd(&priv->port_data[p].lock, seq));

			if (f)
				goto found;
		}
	}

	TS_REPORT_WARN(MOD_KERNEL_IB,
		       "No match for source GID "
		       "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"
		       "%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		       gid[ 0], gid[ 1], gid[ 2], gid[ 3],
		       gid[ 4], gid[ 5], gid[ 6], gid[ 7],
		       gid[ 8], gid[ 9], gid[10], gid[11],
		       gid[12], gid[13], gid[14], gid[15]);
	return -EINVAL;

 found:
	if (device)
		*device = d;
	if (port)
		*port = p;
	if (index)
		*index = i;

	return 0;
}

int ib_cached_pkey_get(tTS_IB_DEVICE_HANDLE device_handle,
		       tTS_IB_PORT          port,
		       int                  index,
		       tTS_IB_PKEY         *pkey)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;
	unsigned int              seq;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port)
		return -EINVAL;

	if (index < 0 || index >= priv->port_data[port].properties.pkey_table_length)
		return -EINVAL;

	do {
		seq = tsSeqLockReadBegin(&priv->port_data[port].lock);
		*pkey = priv->port_data[port].pkey_table[index];
	} while (tsSeqLockReadEnd(&priv->port_data[port].lock, seq));

	return 0;
}

int ib_cached_pkey_find(tTS_IB_DEVICE_HANDLE device_handle,
			tTS_IB_PORT          port,
			tTS_IB_PKEY          pkey,
			int                 *index)
{
	struct ib_device         *device = device_handle;
	struct ib_device_private *priv;
	unsigned int              seq;
	int                       i;
	int                       found;

	TS_IB_CHECK_MAGIC(device, DEVICE);

	priv = device->core;

	if (port < priv->start_port || port > priv->end_port)
		return -EINVAL;

	do {
		seq = tsSeqLockReadBegin(&priv->port_data[port].lock);
		found = -1;
		for (i = 0; i < priv->port_data[port].properties.pkey_table_length; ++i) {
			if ((priv->port_data[port].pkey_table[i] & 0x7fff) ==
			    (pkey & 0x7fff)) {
				found = i;
				break;
			}
		}
	} while (tsSeqLockReadEnd(&priv->port_data[port].lock, seq));

	if (found < 0) {
		return -ENOENT;
	} else {
		*index = found;
		return 0;
	}
}

int ib_cache_setup(struct ib_device *device)
{
	struct ib_device_private *priv = device->core;
	struct ib_port_properties prop;
	int                       p;
	int                       ret;

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		priv->port_data[p].gid_table  = NULL;
		priv->port_data[p].pkey_table = NULL;
	}

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		tsSeqLockInit(&priv->port_data[p].lock);
		ret = device->port_query(device, p, &prop);
		if (ret) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "port_query failed for %s",
				       device->name);
			goto error;
		}
		priv->port_data[p].gid_table_alloc_length = prop.gid_table_length;
		priv->port_data[p].gid_table = kmalloc(prop.gid_table_length * sizeof (tTS_IB_GID),
						       GFP_KERNEL);
		if (!priv->port_data[p].gid_table) {
			ret = -ENOMEM;
			goto error;
		}

		priv->port_data[p].pkey_table_alloc_length = prop.pkey_table_length;
		priv->port_data[p].pkey_table = kmalloc(prop.pkey_table_length * sizeof (tTS_IB_PKEY),
							GFP_KERNEL);
		if (!priv->port_data[p].pkey_table) {
			ret = -ENOMEM;
			goto error;
		}

		ib_cache_update(device, p);
	}

	return 0;

 error:
	for (p = priv->start_port; p <= priv->end_port; ++p) {
		kfree(priv->port_data[p].gid_table);
		kfree(priv->port_data[p].pkey_table);
	}

	return ret;
}

void ib_cache_cleanup(struct ib_device *device)
{
	struct ib_device_private *priv = device->core;
	int                       p;

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		kfree(priv->port_data[p].gid_table);
		kfree(priv->port_data[p].pkey_table);
	}
}

void ib_cache_update(struct ib_device *device,
		     tTS_IB_PORT       port)
{
	struct ib_device_private  *priv = device->core;
	struct ib_port_data       *info = &priv->port_data[port];
	struct ib_port_properties *tprops = NULL;
	tTS_IB_GID                *tgid = NULL;
	tTS_IB_PKEY               *tpkey = NULL;
	int                        i;
	int                        ret;

	TS_TRACE(MOD_KERNEL_IB, T_VERY_VERBOSE, TRACE_KERNEL_IB_GEN,
		 "Updating cached port info for %s port %d",
		 device->name, port);

	tprops = kmalloc(sizeof *tprops, GFP_KERNEL);
	if (!tprops)
		goto out;

	ret = device->port_query(device, port, tprops);
	if (ret) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "port_query failed (%d) for %s",
			       ret, device->name);
		goto out;
	}

	tprops->gid_table_length = min(tprops->gid_table_length,
				       info->gid_table_alloc_length);
	tgid = kmalloc(tprops->gid_table_length * sizeof (tTS_IB_GID),
		       GFP_KERNEL);
	if (!tgid)
		goto out;

	for (i = 0; i < tprops->gid_table_length; ++i) {
		ret = device->gid_query(device, port, i, tgid[i]);
		if (ret) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "gid_query failed (%d) for %s (index %d)",
				       ret, device->name, i);
			goto out;
		}
	}

	tprops->pkey_table_length = min(tprops->pkey_table_length,
					info->pkey_table_alloc_length);
	tpkey = kmalloc(tprops->pkey_table_length * sizeof (tTS_IB_PKEY),
			GFP_KERNEL);
	if (!tpkey)
		goto out;

	for (i = 0; i < tprops->pkey_table_length; ++i) {
		ret = device->pkey_query(device, port, i, &tpkey[i]);
		if (ret) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "pkey_query failed (%d) for %s, port %d, index %d",
				       ret, device->name, port, i);
			goto out;
		}
	}

	tsSeqLockWriteBegin(&info->lock);

	info->properties = *tprops;

	info->sm_path.sm_lid = info->properties.sm_lid;
	info->sm_path.sm_sl  = info->properties.sm_sl;

	info->port_lid.lid = info->properties.lid;
	info->port_lid.lmc = info->properties.lmc;

	memcpy(info->gid_table, tgid,
	       tprops->gid_table_length * sizeof(tTS_IB_GID));
	memcpy(info->pkey_table, tpkey,
	       tprops->pkey_table_length * sizeof(tTS_IB_PKEY));

	tsSeqLockWriteEnd(&info->lock);

 out:
	kfree(tprops);
	kfree(tpkey);
	kfree(tgid);
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
