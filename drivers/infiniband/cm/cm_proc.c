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

  $Id: cm_proc.c,v 1.6 2004/02/25 00:55:32 roland Exp $
*/

#include "cm_priv.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#ifndef W2K_OS
#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#else
#include <os_dep/win/linux/string.h>
#endif

  /* XXX this #if must be removed */
#ifndef W2K_OS
static struct {
  char    *name;
  atomic_t received;
  atomic_t sent;
  atomic_t resent;
} cm_packet_count[] = {
  [TS_IB_COM_MGT_REQ]             = { .name = "REQ"       },
  [TS_IB_COM_MGT_REJ]             = { .name = "REJ"       },
  [TS_IB_COM_MGT_REP]             = { .name = "REP"       },
  [TS_IB_COM_MGT_RTU]             = { .name = "RTU"       },
  [TS_IB_COM_MGT_REJ]             = { .name = "REJ"       },
  [TS_IB_COM_MGT_DREQ]            = { .name = "DREQ"      },
  [TS_IB_COM_MGT_DREP]            = { .name = "DREP"      },
  [TS_IB_COM_MGT_LAP]             = { .name = "LAP"       },
  [TS_IB_COM_MGT_APR]             = { .name = "APR"       },
  [TS_IB_COM_MGT_MRA]             = { .name = "MRA"       },
  [TS_IB_COM_MGT_CLASS_PORT_INFO] = { .name = "PORT_INFO" },
  [TS_IB_COM_MGT_SIDR_REQ]        = { .name = "SIDR_REQ"  },
  [TS_IB_COM_MGT_SIDR_REP]        = { .name = "SIDR_REP"  }
};
static const int max_id = sizeof cm_packet_count / sizeof cm_packet_count[0];

static struct proc_dir_entry *count_proc_file;
#else

struct
{
  char    * name;
  atomic_t received;
  atomic_t sent;
  atomic_t resent;
} cm_packet_count[0x001f];

struct seq_operations count_seq_operations;

static const int max_id = sizeof(cm_packet_count)/sizeof(cm_packet_count[0]);
static struct proc_dir_entry *count_proc_file;
static void *_tsIbCmCountSeqStart(
                                    struct seq_file *file,
                                    loff_t *pos
                                    ) ;
static void *_tsIbCmCountSeqNext(
                                 struct seq_file *file,
                                 void *arg,
                                 loff_t *pos
                                 );
static void _tsIbCmCountSeqStop(
                                struct seq_file *file,
                                void *arg
                                );
static int _tsIbCmCountSeqShow(
                               struct seq_file *file,
                               void *arg
                               ) ;

void tsIbCmInitStruct(void)
{

  cm_packet_count[TS_IB_COM_MGT_REQ].name = "REQ";
  cm_packet_count[TS_IB_COM_MGT_REJ].name = "REJ";
  cm_packet_count[TS_IB_COM_MGT_REP].name = "REP";
  cm_packet_count[TS_IB_COM_MGT_RTU].name = "RTU";
  cm_packet_count[TS_IB_COM_MGT_REJ].name = "REJ";
  cm_packet_count[TS_IB_COM_MGT_DREQ].name = "DREQ";
  cm_packet_count[TS_IB_COM_MGT_DREP].name = "DREP";
  cm_packet_count[TS_IB_COM_MGT_LAP].name = "LAP";
  cm_packet_count[TS_IB_COM_MGT_APR].name = "APR";
  cm_packet_count[TS_IB_COM_MGT_MRA].name = "MRA";
  cm_packet_count[TS_IB_COM_MGT_CLASS_PORT_INFO].name = "PORT_INFO";
  cm_packet_count[TS_IB_COM_MGT_SIDR_REQ].name = "SIDR_REQ";

  count_seq_operations.start = _tsIbCmCountSeqStart;
  count_seq_operations.next  = _tsIbCmCountSeqNext;
  count_seq_operations.stop  = _tsIbCmCountSeqStop;
  count_seq_operations.show  = _tsIbCmCountSeqShow;
}

#endif

void tsIbCmCountReceive(
                        tTS_IB_MAD packet
                        ) {
  uint16_t attribute_id = be16_to_cpu(packet->attribute_id);
  if (attribute_id < max_id) {
    atomic_inc(&cm_packet_count[attribute_id].received);
  }
}

void tsIbCmCountSend(
                     tTS_IB_MAD packet
                     ) {
  uint16_t attribute_id = be16_to_cpu(packet->attribute_id);
  if (attribute_id < max_id) {
    atomic_inc(&cm_packet_count[attribute_id].sent);
  }
}

void tsIbCmCountResend(
                       tTS_IB_MAD packet
                       ) {
  uint16_t attribute_id = be16_to_cpu(packet->attribute_id);
  if (attribute_id < max_id) {
    atomic_inc(&cm_packet_count[attribute_id].sent);
    atomic_inc(&cm_packet_count[attribute_id].resent);
  }
}

static void *_tsIbCmCountSeqStart(
                                    struct seq_file *file,
                                    loff_t *pos
                                    ) {
  if (!*pos) {
    seq_puts(file, " msg type    #rcvd    #sent    #resent\n");
  }

  return *pos < max_id ? (void *) (long) (*pos + 1) : NULL;
}

static void *_tsIbCmCountSeqNext(
                                 struct seq_file *file,
                                 void *arg,
                                 loff_t *pos
                                 ) {
  long n = (long) arg;

  (*pos)++;
  return n < max_id ? (void *) (n + 1) : NULL;
}

static void _tsIbCmCountSeqStop(
                                struct seq_file *file,
                                void *arg
                                ) {
  /* nothing for now */
}

static int _tsIbCmCountSeqShow(
                               struct seq_file *file,
                               void *arg
                               ) {
  int n = (long) arg - 1;

  if (cm_packet_count[n].name) {
    seq_printf(file,
               "%-9s %8d %8d   %8d\n",
               cm_packet_count[n].name,
               atomic_read(&cm_packet_count[n].received),
               atomic_read(&cm_packet_count[n].sent),
               atomic_read(&cm_packet_count[n].resent));
  }

  return 0;
}

  /* XXX this #if must be removed */
#ifndef W2K_OS
static struct seq_operations count_seq_operations = {
  .start = _tsIbCmCountSeqStart,
  .next  = _tsIbCmCountSeqNext,
  .stop  = _tsIbCmCountSeqStop,
  .show  = _tsIbCmCountSeqShow
};

static int _tsIbCmCountProcOpen(
                                struct inode *inode,
                                struct file  *file
                                ) {
  return seq_open(file, &count_seq_operations);
}

static struct file_operations count_proc_operations = {
  .owner   = THIS_MODULE,
  .open    = _tsIbCmCountProcOpen,
  .read    = seq_read,
  .llseek  = seq_lseek,
  .release = seq_release
};

int tsIbCmProcInit(
                   void
                   ) {
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

void tsIbCmProcCleanup(
                       void
                       ) {
  if (count_proc_file) {
    remove_proc_entry("cm_stats",
                      tsKernelProcDirGet());
  }
}
#endif
