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

  $Id: cm_proc.c 32 2004-04-09 03:57:42Z roland $
*/

#include "cm_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

static struct {
	char    *name;
	atomic_t received;
	atomic_t sent;
	atomic_t resent;
} cm_packet_count[] = {
	[IB_COM_MGT_REQ]             = { .name = "REQ"       },
	[IB_COM_MGT_REJ]             = { .name = "REJ"       },
	[IB_COM_MGT_REP]             = { .name = "REP"       },
	[IB_COM_MGT_RTU]             = { .name = "RTU"       },
	[IB_COM_MGT_REJ]             = { .name = "REJ"       },
	[IB_COM_MGT_DREQ]            = { .name = "DREQ"      },
	[IB_COM_MGT_DREP]            = { .name = "DREP"      },
	[IB_COM_MGT_LAP]             = { .name = "LAP"       },
	[IB_COM_MGT_APR]             = { .name = "APR"       },
	[IB_COM_MGT_MRA]             = { .name = "MRA"       },
	[IB_COM_MGT_CLASS_PORT_INFO] = { .name = "PORT_INFO" },
	[IB_COM_MGT_SIDR_REQ]        = { .name = "SIDR_REQ"  },
	[IB_COM_MGT_SIDR_REP]        = { .name = "SIDR_REP"  }
};
static const int max_id = ARRAY_SIZE(cm_packet_count);

static struct proc_dir_entry *count_proc_file;

void ib_cm_count_receive(struct ib_mad *packet)
{
	uint16_t attribute_id = be16_to_cpu(packet->attribute_id);
	if (attribute_id < max_id)
		atomic_inc(&cm_packet_count[attribute_id].received);
}

void ib_cm_count_send(struct ib_mad *packet)
{
	uint16_t attribute_id = be16_to_cpu(packet->attribute_id);
	if (attribute_id < max_id)
		atomic_inc(&cm_packet_count[attribute_id].sent);
}

void ib_cm_count_resend(struct ib_mad *packet)
{
	uint16_t attribute_id = be16_to_cpu(packet->attribute_id);
	if (attribute_id < max_id) {
		atomic_inc(&cm_packet_count[attribute_id].sent);
		atomic_inc(&cm_packet_count[attribute_id].resent);
	}
}

static void *ib_cm_count_seq_start(struct seq_file *file,
				   loff_t *pos)
{
	if (!*pos)
		seq_puts(file, " msg type    #rcvd    #sent    #resent\n");

	return *pos < max_id ? (void *) (long) (*pos + 1) : NULL;
}

static void *ib_cm_count_seq_next(struct seq_file *file,
				  void *arg,
				  loff_t *pos)
{
	long n = (long) arg;

	(*pos)++;
	return n < max_id ? (void *) (n + 1) : NULL;
}

static void ib_cm_count_seq_stop(struct seq_file *file,
				 void *arg)
{
	/* nothing for now */
}

				 static int ib_cm_count_seq_show(struct seq_file *file,
								 void *arg)
{
	int n = (long) arg - 1;

	if (cm_packet_count[n].name)
		seq_printf(file,
			   "%-9s %8d %8d   %8d\n",
			   cm_packet_count[n].name,
			   atomic_read(&cm_packet_count[n].received),
			   atomic_read(&cm_packet_count[n].sent),
			   atomic_read(&cm_packet_count[n].resent));

	return 0;
}

static struct seq_operations count_seq_operations = {
	.start = ib_cm_count_seq_start,
	.next  = ib_cm_count_seq_next,
	.stop  = ib_cm_count_seq_stop,
	.show  = ib_cm_count_seq_show
};

static int ib_cm_count_proc_open(struct inode *inode,
				 struct file  *file)
{
	return seq_open(file, &count_seq_operations);
}

static struct file_operations count_proc_operations = {
	.owner   = THIS_MODULE,
	.open    = ib_cm_count_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

int ib_cm_proc_init(void)
{
	count_proc_file = create_proc_entry("cm_stats",
					    S_IRUGO,
					    tsKernelProcDirGet());
	if (!count_proc_file) {
		TS_REPORT_FATAL(MOD_IB_CM,
				"Can't create cm_stats proc file");
		return -ENOMEM;
	}

	count_proc_file->proc_fops = &count_proc_operations;
	return 0;
}

void ib_cm_proc_cleanup(void)
{
	if (count_proc_file) {
		remove_proc_entry("cm_stats",
				  tsKernelProcDirGet());
	}
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
