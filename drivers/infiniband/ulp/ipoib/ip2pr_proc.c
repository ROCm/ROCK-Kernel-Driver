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

  $Id: ip2pr_proc.c 32 2004-04-09 03:57:42Z roland $
*/

#include "ip2pr_priv.h"

static const char _dir_name_root[] = TS_IP2PR_PROC_DIR_NAME;
static struct proc_dir_entry *_dir_root = NULL;

extern tINT32 tsIp2prPathElementTableDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern tINT32 tsIp2prIpoibWaitTableDump(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern tINT32 tsIp2prProcRetriesRead(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern tINT32 tsIp2prProcTimeoutRead(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern tINT32 tsIp2prProcBackoffRead(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern tINT32 tsIp2prProcCacheTimeoutRead(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern  int tsIp2prProcRetriesWrite(
  struct file *file,
  const char *buffer,
  unsigned long  count,
  void *pos
  );
extern int tsIp2prProcTimeoutWrite(
  struct file *file,
  const char *buffer,
  unsigned long  count,
  void *pos
  );
extern int tsIp2prProcBackoffWrite(
  struct file *file,
  const char *buffer,
  unsigned long count,
  void *pos
  );
extern int tsIp2prProcCacheTimeoutWrite(
  struct file *file,
  const char *buffer,
  unsigned long count,
  void *pos
  );

extern int tsIp2prProcTotalReq(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern int tsIp2prProcArpTimeout(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern int tsIp2prProcPathTimeout(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );
extern int tsIp2prProcTotalFail(
  tSTR   buffer,
  tINT32 max_size,
  tINT32 start_index,
  long *end_index
  );


/* ========================================================================= */
/*.._tsIp2prProcReadParse -- read function for the injection table */
static tINT32 _tsIp2prProcReadParse
(
 char   *page,
 char  **start,
 off_t   offset,
 tINT32  count,
 tINT32 *eof,
 tPTR    data
)
{
  tIP2PR_PROC_SUB_ENTRY sub_entry = (tIP2PR_PROC_SUB_ENTRY)data;
  long end_index = 0;
  tINT32 size;

  TS_CHECK_NULL(sub_entry, -EINVAL);

#if 0
  if (NULL == *start && 0 != offset) {
    return 0; /* I'm not sure why this always gets called twice... */
  } /* if */
#endif

  size = sub_entry->read(page, count, offset, &end_index);
  if (0 < size) {

    if (0 < end_index) {
      *start = (char *)end_index;
      *eof   = 0;
    } /* if */
    else {
      *start = NULL;
      *eof   = 1;
    } /* else */
  } /* if */

  return size;
} /* _tsIp2prProcReadParse */

static tIP2PR_PROC_SUB_ENTRY_STRUCT _file_entry_list[TS_IP2PR_PROC_ENTRIES] = {
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_ARP_WAIT,
    name  : "arp_wait",
    read  : tsIp2prIpoibWaitTableDump,
    write : NULL },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_PATH_TABLE,
    name  : "path_cache",
    read  : tsIp2prPathElementTableDump,
    write : NULL },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_MAX_RETRIES,
    name  : "retries",
    read  : tsIp2prProcRetriesRead,
    write : tsIp2prProcRetriesWrite },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_TIMEOUT,
    name  : "timeout",
    read  : tsIp2prProcTimeoutRead,
    write : tsIp2prProcTimeoutWrite },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_BACKOUT,
    name  : "backoff",
    read  : tsIp2prProcBackoffRead,
    write : tsIp2prProcBackoffWrite },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_CACHE_TIMEOUT,
    name  : "cache_timeout",
    read  : tsIp2prProcCacheTimeoutRead,
    write : tsIp2prProcCacheTimeoutWrite },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_TOTAL_REQ,
    name  : "total_req",
    read  : tsIp2prProcTotalReq,
    write : NULL },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_ARP_TIMEOUT,
    name  : "arp_timeout",
    read  : tsIp2prProcArpTimeout,
    write : NULL },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_PATH_TIMEOUT,
    name  : "path_timeout",
    read  : tsIp2prProcPathTimeout,
    write : NULL },
  { entry : NULL,
    type  : TS_IP2PR_PROC_ENTRY_TOTAL_FAIL,
    name  : "total_fail",
    read  : tsIp2prProcTotalFail,
    write : NULL }
};

/* ========================================================================= */
/*..tsIp2prProcFsCleanup -- cleanup the proc filesystem entries  */
tINT32 tsIp2prProcFsCleanup
(
 void
)
{
  tIP2PR_PROC_SUB_ENTRY sub_entry;
  tINT32 counter;

  TS_CHECK_NULL(_dir_root, -EINVAL);
  /*
   * first clean-up the frameworks tables
   */
  for (counter = 0; counter < TS_IP2PR_PROC_ENTRIES; counter++) {
    sub_entry = &_file_entry_list[counter];

    if (NULL != sub_entry->entry) {
      remove_proc_entry(sub_entry->name, _dir_root);
      sub_entry->entry = NULL;
    } /* if */
  } /* for */
  /*
   * remove IP2PR directory
   */
  remove_proc_entry(_dir_name_root, tsKernelProcDirGet());
  _dir_root = NULL;

  TS_TRACE(MOD_IP2PR, T_VERBOSE, TRACE_FLOW_CLEANUP,
	   "PROC: /proc filesystem cleanup complete.");

  return 0;
} /* tsIp2prProcFsCleanup */

/* ========================================================================= */
/*..tsIp2prProcFsInit -- initialize the proc filesystem entries  */
tINT32 tsIp2prProcFsInit
(
 void
 )
{
  tIP2PR_PROC_SUB_ENTRY sub_entry;
  tINT32 result;
  tINT32 counter;
  /*
   * XXX still need to check this:
   * validate some assumptions the write parser will be making.
   */
  if (0 &&
      sizeof(tINT32) != sizeof(tSTR)) {

    TS_TRACE(MOD_IP2PR, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: integers and pointers of a different size. <%d:%d>",
	     sizeof(tINT32), sizeof(tSTR));
    return -EFAULT;
  } /* if */

  if (NULL != _dir_root) {

    TS_TRACE(MOD_IP2PR, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: already initialized!");
    return -EINVAL;
  } /* if */
  /*
   * create a gateway root, and main directories
   */
  _dir_root = proc_mkdir(_dir_name_root, tsKernelProcDirGet());
  if (NULL == _dir_root) {

    TS_TRACE(MOD_IP2PR, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: Failed to create <%s> proc entry.", _dir_name_root);
    return -EINVAL;
  } /* if */

  _dir_root->owner = THIS_MODULE;

  for (counter = 0; counter < TS_IP2PR_PROC_ENTRIES; counter++) {
    sub_entry = &_file_entry_list[counter];

    if (sub_entry->type != counter) {
      result = -EFAULT;
      goto error;
    } /* if */

    sub_entry->entry = create_proc_entry(sub_entry->name,
					 S_IRUGO | S_IWUGO,
					 _dir_root);
    if (NULL == sub_entry->entry) {

      TS_TRACE(MOD_IP2PR, T_TERSE, TRACE_FLOW_FATAL,
	       "PROC: Failed to create <%s> framework proc entry.",
	       sub_entry->name);
      result = -EINVAL;
      goto error;
    } /* if */

    sub_entry->entry->read_proc  = _tsIp2prProcReadParse;
    sub_entry->entry->write_proc = sub_entry->write;
    sub_entry->entry->data       = sub_entry;
    sub_entry->entry->owner      = THIS_MODULE;
  } /* for */

  return 0; /* success */
error:
  (void)tsIp2prProcFsCleanup();
  return result;
} /* tsIp2prProcFsInit */
