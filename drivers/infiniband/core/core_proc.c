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

  $Id: core_proc.c 92 2004-04-25 16:59:50Z roland $
*/

/*
  We want to make a directory tree like:

  /proc/infiniband/core/
  hca1/
  info
  port1/
  counters
  info
  gid_table
  pkey_table
  port2...
  hca2...
*/

#include "core_priv.h"

#include "pm_access.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>

struct ib_core_proc {
	int                    index;
	char                   dev_dir_name[16];
	struct proc_dir_entry *dev_dir;
	struct proc_dir_entry *dev_info;
	struct ib_port_proc   *port;
};

struct ib_port_proc {
	struct ib_device      *device;
	int                    port_num;
	struct proc_dir_entry *port_dir;
	struct proc_dir_entry *port_data;
	struct proc_dir_entry *gid_table;
	struct proc_dir_entry *pkey_table;
	struct proc_dir_entry *counters;
};

static int index = 1;
static struct proc_dir_entry *core_dir;

static void *ib_dev_info_seq_start(struct seq_file *file,
				   loff_t *pos)
{
	if (*pos)
		return NULL;
	else
		return (void *) 1UL;
}

static void *ib_dev_info_seq_next(struct seq_file *file,
				  void *iter_ptr,
				  loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void ib_dev_info_seq_stop(struct seq_file *file,
				 void *iter_ptr)
{
	/* nothing for now */
}

static int ib_dev_info_seq_show(struct seq_file *file,
				void *iter_ptr)
{
	struct ib_device_properties prop;
	struct ib_device           *proc_device = file->private;

	seq_printf(file, "name:          %s\n", proc_device->name);
	seq_printf(file, "provider:      %s\n", proc_device->provider);

	if (proc_device->device_query(proc_device, &prop))
		return 0;

	seq_printf(file, "node GUID:     %04x:%04x:%04x:%04x\n",
		   be16_to_cpu(((uint16_t *) prop.node_guid)[0]),
		   be16_to_cpu(((uint16_t *) prop.node_guid)[1]),
		   be16_to_cpu(((uint16_t *) prop.node_guid)[2]),
		   be16_to_cpu(((uint16_t *) prop.node_guid)[3]));
	seq_printf(file, "ports:         %d\n", prop.num_port);
	seq_printf(file, "vendor ID:     0x%x\n", prop.vendor_id);
	seq_printf(file, "device ID:     0x%x\n", prop.device_id);
	seq_printf(file, "HW revision:   0x%x\n", prop.hw_rev);
	seq_printf(file, "FW revision:   0x%" TS_U64_FMT "x\n", prop.fw_rev);

	return 0;
}

static struct seq_operations dev_info_seq_ops = {
	.start = ib_dev_info_seq_start,
	.next  = ib_dev_info_seq_next,
	.stop  = ib_dev_info_seq_stop,
	.show  = ib_dev_info_seq_show
};

static int ib_dev_info_open(struct inode *inode,
			    struct file *file)
{
	int ret;

	ret = seq_open(file, &dev_info_seq_ops);
	if (ret)
		return ret;

	((struct seq_file *) file->private_data)->private = PDE(inode)->data;

	return 0;
}

static void *ib_port_info_seq_start(struct seq_file *file,
				  loff_t *pos)
{
	if (*pos)
		return NULL;
	else
		return (void *) 1UL;
}

static void *ib_port_info_seq_next(struct seq_file *file,
				   void *iter_ptr,
				   loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void ib_port_info_seq_stop(struct seq_file *file,
				  void *iter_ptr)
{
	/* nothing for now */
}

static int ib_port_info_seq_show(struct seq_file *file,
				 void *iter_ptr)
{
	struct ib_port_properties prop;
	struct ib_port_proc *proc_port = file->private;

	if (proc_port->device->port_query(proc_port->device, proc_port->port_num, &prop)) {
		return 0;
	}

	seq_printf(file, "state:         ");
	switch (prop.port_state) {
	case TS_IB_PORT_STATE_NOP:
		seq_printf(file, "NOP\n");
		break;
	case TS_IB_PORT_STATE_DOWN:
		seq_printf(file, "DOWN\n");
		break;
	case TS_IB_PORT_STATE_INIT:
		seq_printf(file, "INITIALIZE\n");
		break;
	case TS_IB_PORT_STATE_ARMED:
		seq_printf(file, "ARMED\n");
		break;
	case TS_IB_PORT_STATE_ACTIVE:
		seq_printf(file, "ACTIVE\n");
		break;
	default:
		seq_printf(file, "UNKNOWN\n");
		break;
	}

	seq_printf(file, "LID:           0x%04x\n", prop.lid);
	seq_printf(file, "LMC:           0x%04x\n", prop.lmc);
	seq_printf(file, "SM LID:        0x%04x\n", prop.sm_lid);
	seq_printf(file, "SM SL:         0x%04x\n", prop.sm_sl);
	seq_printf(file, "Capabilities:  ");
	if (prop.capability_mask) {
		static const char *cap_name[] = {
			[1]  = "IsSM",
			[2]  = "IsNoticeSupported",
			[3]  = "IsTrapSupported",
			[5]  = "IsAutomaticMigrationSupported",
			[6]  = "IsSLMappingSupported",
			[7]  = "IsMKeyNVRAM",
			[8]  = "IsPKeyNVRAM",
			[9]  = "IsLEDInfoSupported",
			[10] = "IsSMdisabled",
			[11] = "IsSystemImageGUIDSupported",
			[12] = "IsPKeySwitchExternalPortTrapSupported",
			[16] = "IsCommunicationManagementSupported",
			[17] = "IsSNMPTunnelingSupported",
			[18] = "IsReinitSupported",
			[19] = "IsDeviceManagementSupported",
			[20] = "IsVendorClassSupported",
			[21] = "IsDRNoticeSupported",
			[22] = "IsCapabilityMaskNoticeSupported",
			[23] = "IsBootManagementSupported"
		};
		int i;
		int f = 0;

		for (i = 0; i < ARRAY_SIZE(cap_name); ++i) {
			if (prop.capability_mask & (1 << i)) {
				if (f++) {
					seq_puts(file, "               ");
				}
				if (cap_name[i]) {
					seq_printf(file, "%s\n", cap_name[i]);
				} else {
					seq_printf(file, "RESERVED (%d)\n", i);
				}
			}
		}
	} else {
		seq_puts(file, "NONE\n");
	}


	return 0;
}

static struct seq_operations port_data_seq_ops = {
	.start = ib_port_info_seq_start,
	.next  = ib_port_info_seq_next,
	.stop  = ib_port_info_seq_stop,
	.show  = ib_port_info_seq_show
};

static int ib_port_info_open(struct inode *inode,
			     struct file *file)
{
	int ret;

	ret = seq_open(file, &port_data_seq_ops);
	if (ret)
		return ret;

	((struct seq_file *) file->private_data)->private = PDE(inode)->data;

	return 0;
}

static void *ib_gid_table_seq_start(struct seq_file *file,
				    loff_t *pos)
{
	if (*pos)
		return NULL;
	else
		return (void *) 1UL;
}

static void *ib_gid_table_seq_next(struct seq_file *file,
				   void *iter_ptr,
				   loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void ib_gid_table_seq_stop(struct seq_file *file,
				  void *iter_ptr)
{
	/* nothing for now */
}

static int ib_gid_table_seq_show(struct seq_file *file,
				 void *iter_ptr)
{
	int i, j;
	struct ib_port_proc *proc_port = file->private;
	static const tTS_IB_GID null_gid;
	tTS_IB_GID gid;

	for (i = 0; !ib_cached_gid_get(proc_port->device, proc_port->port_num, i, gid); ++i) {
		if (memcmp(&null_gid[8], &gid[8], 8)) {
			seq_printf(file, "[%3d] ", i);

			for (j = 0; j < sizeof (tTS_IB_GID) / 2; ++j) {
				if (j)
					seq_putc(file, ':');
				seq_printf(file, "%04x", be16_to_cpu(((uint16_t *) gid)[j]));
			}
			seq_putc(file, '\n');
		}
	}

	return 0;
}

static struct seq_operations gid_table_seq_ops = {
	.start = ib_gid_table_seq_start,
	.next  = ib_gid_table_seq_next,
	.stop  = ib_gid_table_seq_stop,
	.show  = ib_gid_table_seq_show
};

static int ib_gid_table_open(struct inode *inode,
			      struct file *file)
{
	int ret;

	ret = seq_open(file, &gid_table_seq_ops);
	if (ret)
		return ret;

	((struct seq_file *) file->private_data)->private = PDE(inode)->data;

	return 0;
}

static void *ib_pkey_table_seq_start(struct seq_file *file,
				     loff_t *pos)
{
	if (*pos) {
		return NULL;
	} else {
		return (void *) 1UL;
	}
}

static void *ib_pkey_table_seq_next(struct seq_file *file,
				    void *iter_ptr,
				    loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void ib_pkey_table_seq_stop(struct seq_file *file,
				   void *iter_ptr)
{
	/* nothing for now */
}

static int ib_pkey_table_seq_show(struct seq_file *file,
				  void *iter_ptr)
{
	int i;
	struct ib_port_proc *proc_port = file->private;
	tTS_IB_PKEY pkey;

	for (i = 0; !ib_cached_pkey_get(proc_port->device, proc_port->port_num, i, &pkey); ++i) {
		if (pkey & 0x7fff) {
			seq_printf(file, "[%3d] %04x\n", i, pkey);
		}
	}

	return 0;
}

static struct seq_operations pkey_table_seq_ops = {
	.start = ib_pkey_table_seq_start,
	.next  = ib_pkey_table_seq_next,
	.stop  = ib_pkey_table_seq_stop,
	.show  = ib_pkey_table_seq_show
};

static int ib_pkey_table_open(
	struct inode *inode,
	struct file *file
	) {
	int ret;

	ret = seq_open(file, &pkey_table_seq_ops);
	if (ret) {
		return ret;
	}
	((struct seq_file *) file->private_data)->private = PDE(inode)->data;

	return 0;
}

static void *ib_counters_seq_start(struct seq_file *file,
				   loff_t *pos)
{
	if (*pos)
		return NULL;
	else
		return (void *) 1UL;
}

static void *ib_counters_seq_next(struct seq_file *file,
				  void *iter_ptr,
				  loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void ib_counters_seq_stop(struct seq_file *file,
				 void *iter_ptr)
{
	/* nothing for now */
}

static int ib_counters_seq_show(struct seq_file *file,
				void *iter_ptr)
{
	struct ib_port_proc *proc_port = file->private;
	struct ib_mad       *in_mad = NULL;
	struct ib_mad       *out_mad = NULL;

	tTS_IB_PM_PORT_COUNTERS counters = NULL;

	if (!proc_port->device->mad_process) {
		seq_puts(file, "<No performance management agent available>\n");
		return 0;
	}

	in_mad = kmalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *in_mad, GFP_KERNEL);
	if (!in_mad || !out_mad) {
		seq_puts(file, "<Failed to allocate MAD buffer>\n");
		goto out;
	}

	counters = kmalloc(sizeof *counters, GFP_KERNEL);
	if (!counters) {
		seq_puts(file, "<Failed to allocate counters buffer>\n");
		goto out;
	}

	memset(in_mad, 0, sizeof *in_mad);
	in_mad->format_version = 1;
	in_mad->mgmt_class     = TS_IB_MGMT_CLASS_PERF;
	in_mad->class_version  = 1;
	in_mad->r_method       = TS_IB_MGMT_METHOD_GET;
	in_mad->attribute_id   = cpu_to_be16(TS_IB_PM_ATTRIB_PORT_COUNTERS);
	in_mad->dqpn           = 1;
	in_mad->port           = proc_port->port_num;

	memset(counters, 0, sizeof *counters);
	counters->port_select = proc_port->port_num;
	tsIbPmPortCountersPack(counters, TS_IB_MAD_TO_PM_DATA(in_mad));

	if ((proc_port->device->mad_process(proc_port->device,
					    1,
					    in_mad,
					    out_mad) &
	     (TS_IB_MAD_RESULT_SUCCESS | TS_IB_MAD_RESULT_REPLY)) !=
	    (TS_IB_MAD_RESULT_SUCCESS | TS_IB_MAD_RESULT_REPLY)) {
		seq_puts(file, "<Performance management query failed>\n");
		goto out;
	}

	tsIbPmPortCountersUnPack(TS_IB_MAD_TO_PM_DATA(out_mad), counters);

	seq_printf(file, "Symbol error counter:                %10u\n",
		   counters->symbol_error_counter);
	seq_printf(file, "Link error recovery counter:         %10u\n",
		   counters->link_error_recovery_counter);
	seq_printf(file, "Link downed counter:                 %10u\n",
		   counters->link_downed_counter);
	seq_printf(file, "Port receive errors:                 %10u\n",
		   counters->port_rcv_errors);
	seq_printf(file, "Port receive remote physical errors: %10u\n",
		   counters->port_rcv_remote_physical_errors);
	seq_printf(file, "Port receive switch relay errors:    %10u\n",
		   counters->port_rcv_switch_relay_errors);
	seq_printf(file, "Port transmit discards:              %10u\n",
		   counters->port_xmit_discards);
	seq_printf(file, "Port transmit constrain errors:      %10u\n",
		   counters->port_xmit_constrain_errors);
	seq_printf(file, "Port receive constrain errors:       %10u\n",
		   counters->port_rcv_constrain_errors);
	seq_printf(file, "Local link integrity errors:         %10u\n",
		   counters->local_link_integrity_errors);
	seq_printf(file, "Excessive buffer overrun errors:     %10u\n",
		   counters->excessive_buffer_overrun_errors);
	seq_printf(file, "VL15 dropped:                        %10u\n",
		   counters->vl15_dropped);
	seq_printf(file, "Port transmit data:                  %10u\n",
		   counters->port_xmit_data);
	seq_printf(file, "Port receive data:                   %10u\n",
		   counters->port_rcv_data);
	seq_printf(file, "Port transmit packets:               %10u\n",
		   counters->port_xmit_pkts);
	seq_printf(file, "Port receive packets:                %10u\n",
		   counters->port_rcv_pkts);

 out:
	kfree(in_mad);
	kfree(out_mad);
	kfree(counters);
	return 0;
}

static struct seq_operations counters_seq_ops = {
	.start = ib_counters_seq_start,
	.next  = ib_counters_seq_next,
	.stop  = ib_counters_seq_stop,
	.show  = ib_counters_seq_show
};

static int ib_counters_open(struct inode *inode,
			    struct file *file)
{
	int ret;

	ret = seq_open(file, &counters_seq_ops);
	if (ret) {
		return ret;
	}
	((struct seq_file *) file->private_data)->private = PDE(inode)->data;

	return 0;
}

static int ib_proc_file_release(struct inode *inode,
				struct file *file)
{
	return seq_release(inode, file);
}

static struct file_operations dev_info_ops = {
	.owner   = THIS_MODULE,
	.open    = ib_dev_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = ib_proc_file_release
};

static struct file_operations port_data_ops = {
	.owner   = THIS_MODULE,
	.open    = ib_port_info_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = ib_proc_file_release
};

static struct file_operations gid_table_ops = {
	.owner   = THIS_MODULE,
	.open    = ib_gid_table_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = ib_proc_file_release
};

static struct file_operations pkey_table_ops = {
	.owner   = THIS_MODULE,
	.open    = ib_pkey_table_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = ib_proc_file_release
};

static struct file_operations counters_ops = {
	.owner   = THIS_MODULE,
	.open    = ib_counters_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = ib_proc_file_release
};

int ib_proc_setup(struct ib_device *device,
		  int               is_switch)
{
	struct ib_device_private *priv = device->core;
	struct ib_core_proc      *core_proc;
	char                      port_name[] = "portNN";
	int p;

	core_proc = kmalloc(sizeof *core_proc, GFP_KERNEL);
	if (!core_proc) {
		return -ENOMEM;
	}

	core_proc->index = index;

	if (is_switch) {
		sprintf(core_proc->dev_dir_name, "switch%d", index);
	} else {
		sprintf(core_proc->dev_dir_name, "ca%d", index);
	}
	core_proc->dev_dir = proc_mkdir(core_proc->dev_dir_name, core_dir);
	if (!core_proc) {
		goto out_free;
	}

	core_proc->dev_info = create_proc_entry("info", S_IRUGO, core_proc->dev_dir);
	if (!core_proc->dev_info) {
		goto out_topdir;
	}
	core_proc->dev_info->proc_fops = &dev_info_ops;
	core_proc->dev_info->data      = device;

	core_proc->port = kmalloc((priv->end_port + 1) * sizeof (struct ib_port_proc),
				  GFP_KERNEL);
	if (!core_proc->port) {
		goto out_info;
	}

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		core_proc->port[p].device     = device;
		core_proc->port[p].port_num   = p;
		core_proc->port[p].port_dir   = NULL;
		core_proc->port[p].port_data  = NULL;
		core_proc->port[p].gid_table  = NULL;
		core_proc->port[p].pkey_table = NULL;
		core_proc->port[p].counters   = NULL;
	}

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		snprintf(port_name, sizeof port_name, "port%d", p);
		core_proc->port[p].port_dir = proc_mkdir(port_name, core_proc->dev_dir);
		if (!core_proc->port[p].port_dir) {
			goto out_port;
		}

		core_proc->port[p].port_data = create_proc_entry("info", S_IRUGO,
								 core_proc->port[p].port_dir);
		if (!core_proc->port[p].port_data) {
			goto out_port;
		}
		core_proc->port[p].port_data->proc_fops = &port_data_ops;
		core_proc->port[p].port_data->data      = &core_proc->port[p];

		core_proc->port[p].gid_table = create_proc_entry("gid_table", S_IRUGO,
								 core_proc->port[p].port_dir);
		if (!core_proc->port[p].gid_table) {
			goto out_port;
		}
		core_proc->port[p].gid_table->proc_fops = &gid_table_ops;
		core_proc->port[p].gid_table->data      = &core_proc->port[p];

		core_proc->port[p].pkey_table = create_proc_entry("pkey_table", S_IRUGO,
								  core_proc->port[p].port_dir);
		if (!core_proc->port[p].pkey_table) {
			goto out_port;
		}
		core_proc->port[p].pkey_table->proc_fops = &pkey_table_ops;
		core_proc->port[p].pkey_table->data      = &core_proc->port[p];

		core_proc->port[p].counters = create_proc_entry("counters", S_IRUGO,
								core_proc->port[p].port_dir);
		if (!core_proc->port[p].counters) {
			goto out_port;
		}
		core_proc->port[p].counters->proc_fops = &counters_ops;
		core_proc->port[p].counters->data      = &core_proc->port[p];
	}

	priv->proc = core_proc;
	++index;
	return 0;

 out_port:
	for (p = priv->start_port; p <= priv->end_port; ++p) {
		if (core_proc->port[p].port_data) {
			remove_proc_entry("info", core_proc->port[p].port_dir);
		}

		if (core_proc->port[p].gid_table) {
			remove_proc_entry("gid_table", core_proc->port[p].port_dir);
		}

		if (core_proc->port[p].pkey_table) {
			remove_proc_entry("pkey_table", core_proc->port[p].port_dir);
		}

		if (core_proc->port[p].counters) {
			remove_proc_entry("counters", core_proc->port[p].port_dir);
		}

		if (core_proc->port[p].port_dir) {
			snprintf(port_name, sizeof port_name, "port%d", p);
			remove_proc_entry(port_name, core_proc->dev_dir);
		}
	}

 out_info:
	remove_proc_entry("info", core_proc->dev_dir);

 out_topdir:
	remove_proc_entry(core_proc->dev_dir_name, core_dir);

 out_free:
	kfree(core_proc);
	return -ENOMEM;
}

void ib_proc_cleanup(struct ib_device *device)
{
	struct ib_device_private *priv = device->core;
	struct ib_core_proc      *core_proc = priv->proc;
	char                      port_name[] = "portNN";
	int p;

	for (p = priv->start_port; p <= priv->end_port; ++p) {
		remove_proc_entry("counters", core_proc->port[p].port_dir);
		remove_proc_entry("pkey_table", core_proc->port[p].port_dir);
		remove_proc_entry("gid_table", core_proc->port[p].port_dir);
		remove_proc_entry("info", core_proc->port[p].port_dir);
		snprintf(port_name, sizeof port_name, "port%d", p);
		remove_proc_entry(port_name, core_proc->dev_dir);
	}

	remove_proc_entry("info", core_proc->dev_dir);
	remove_proc_entry(core_proc->dev_dir_name, core_dir);

	kfree(priv->proc);
}

int ib_create_proc_dir(void)
{
	core_dir = proc_mkdir("core", tsKernelProcDirGet());
	return core_dir ? 0 : -ENOMEM;
}

void ib_remove_proc_dir(void)
{
	remove_proc_entry("core", tsKernelProcDirGet());
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
