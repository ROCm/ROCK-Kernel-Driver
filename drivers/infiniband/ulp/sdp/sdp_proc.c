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

  $Id: sdp_proc.c 32 2004-04-09 03:57:42Z roland $
*/

#include "sdp_main.h"

static const char _dir_name_root[] = TS_SDP_PROC_DIR_NAME;
static struct proc_dir_entry *_dir_root = NULL;

/* ------------------------------------------------------------------- */
/*                                                                     */
/* Generic static functions used by read/write functions               */
/*                                                                     */
/* ------------------------------------------------------------------- */
#if 0 /* currently not used, because the two tables are not writable */
/* ========================================================================= */
/*.._tsSdpProcWriteParse -- parse a buffer for write commands. */
static tINT32 _tsSdpProcWriteParse
(
 tSDP_PROC_ENTRY_PARSE parse_list,
 tSTR                 buffer,
 tUINT32              size,
 tINT32              *result_array,
 tINT32               result_max,   /* result array size IN BYTES! */
 tSTR                *next
)
{
  tSDP_PROC_ENTRY_PARSE parse_item;
  tINT32 elements;
  tINT32 counter;
  tINT32 end;
  tSTR   name;
  tSTR   value;
  /*
   * double check a few constants we'll be using to make sure everything
   * is safe.
   */
  if (0 != (result_max % sizeof(tINT32))) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: result structure of an incorrect size. <%d>", result_max);
    return -EFAULT;
  } /* if */
  else {
    result_max = result_max/sizeof(tINT32);
  } /* else */
  /*
   * pre parse, to determine number of elements in this line.
   */
  for (end = 0, elements = 0;
       '\n' != buffer[end] && size > end;
       end++) {

    elements += (':' == buffer[end]) ? 1 : 0;
  } /* for */

  buffer[end] = '\0'; /* terminate the command */
  /*
   * update pointer for next iteration of this function
   */
  if (end < (size - 1)) {
    end++;
    *next = (buffer + end);
  } /* if */
  else {
    end = size;
    *next = NULL;
  } /* else */

  if (0 < elements) {
    /*
     * walk the buffer parsing tokens
     */
    elements = 0;
    counter = 0;

    while('\0' != buffer[counter]) {
      /*
       * advance over spaces
       */
      if (isspace(buffer[counter])) {
	counter++;
	continue;
      } /* if */
      /*
       * Isolate name
       */
      name = (buffer + counter);
      while (isgraph(buffer[counter]) &&
	     ':' != buffer[counter]) {

	counter++;
      } /* for */

      if (':' != buffer[counter]) {

	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_STAGE,
		 "PROC: Config write parser. incorrect command format.");
	goto error;
      } /* if */

      buffer[counter] = '\0';
      counter++;
      /*
       * match name.
       */
      for (parse_item = parse_list; NULL != parse_item->value; parse_item++) {

	if (0 == (strcmp(parse_item->value, name))) {
	  break;
	} /* if */
      } /* for */

      if (NULL == parse_item->value) {
	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_STAGE,
		 "PROC: Config write parser. unknown command. <%s>", name);
	goto error;
      } /* else */
      /*
       * isolate value
       */
      value = (buffer + counter);
      while (isgraph(buffer[counter])) {

	counter++;
      } /* for */

      if ('\0' != buffer[counter]) {
	buffer[counter] = '\0';
	counter++;
      } /* if */

      if ('\0' == value[0]) {
	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_STAGE,
		 "PROC: Config write parser. no value for command. <%s>",
		 name);
	goto error;
      } /* if */
      /*
       * update the result array/structure with an entry value
       */
      if (parse_item->id < result_max) {

	switch (parse_item->type) {
	case TS_SDP_PROC_WRITE_INT:
          sscanf(value, "%x", &result_array[parse_item->id]);
	  break;
	case TS_SDP_PROC_WRITE_STR:
	  result_array[parse_item->id] = (tINT32)value;
	  break;
	case TS_SDP_PROC_WRITE_U64:
          sscanf(value, "%Lx", (tUINT64 *)&result_array[parse_item->id]);
	  break;
	default:
	  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_STAGE,
		   "PROC: Item <%d> has unknown type. <%d>",
		   parse_item->id, parse_item->type);
	  goto error;
	} /* switch */

	elements++;
      } /* if */
      else {
	TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_STAGE,
		 "PROC: token ID <%d> beyond valid range. <0:%d>",
		 parse_item->id, result_max - 1);
	goto error;
      } /* else */
    } /* while */
  } /* if */
  else {

    TS_TRACE(MOD_LNX_SDP, T_SCREAM, TRACE_FLOW_STAGE,
	     "PROC: Config write parser. Empty command line.");
  } /* else */

  return end;
error:
  return -EINVAL;
} /* _tsSdpProcWriteParse */
#endif
/* ========================================================================= */
/*.._tsSdpProcReadParse -- read function for the injection table */
static tINT32 _tsSdpProcReadParse
(
 char   *page,
 char  **start,
 off_t   offset,
 tINT32  count,
 tINT32 *eof,
 tPTR    data
)
{
  tSDP_PROC_SUB_ENTRY sub_entry = (tSDP_PROC_SUB_ENTRY)data;
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
} /* _tsSdpProcReadParse */
/* ------------------------------------------------------------------- */
/*                                                                     */
/* Static read/write functions for each proc/framework directory entry */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*
 * Initialization structure, each table in the gateway framework directory
 * (anything that is not a module) should create an entry and define read
 * write function.
 */
static tSDP_PROC_SUB_ENTRY_STRUCT _file_entry_list[TS_SDP_PROC_ENTRIES] = {
  { entry : NULL,
    type  : TS_SDP_PROC_ENTRY_MAIN_BUFF,
    name  : "buffer_pool",
    read  : tsSdpBuffMainDump,
    write : NULL },
  { entry : NULL,
    type  : TS_SDP_PROC_ENTRY_MAIN_CONN,
    name  : "conn_main",
    read  : tsSdpConnTableMainDump,
    write : NULL },
  { entry : NULL,
    type  : TS_SDP_PROC_ENTRY_DATA_CONN,
    name  : "conn_data",
    read  : tsSdpConnTableDataDump,
    write : NULL },
  { entry : NULL,
    type  : TS_SDP_PROC_ENTRY_RDMA_CONN,
    name  : "conn_rdma",
    read  : tsSdpConnTableRdmaDump,
    write : NULL },
  { entry : NULL,
    type  : TS_SDP_PROC_ENTRY_OPT_CONN,
    name  : "opt_conn",
    read  : tsSdpSoptTableDump,
    write : NULL },
  { entry : NULL,
    type  : TS_SDP_PROC_ENTRY_ROOT_TABLE,
    name  : "devices",
    read  : tsSdpDeviceTableDump,
    write : NULL },
};
/* ------------------------------------------------------------------- */
/*                                                                     */
/* SDP module public functions.                                        */
/*                                                                     */
/* ------------------------------------------------------------------- */

/* ========================================================================= */
/*..tsSdpProcFsCleanup -- cleanup the proc filesystem entries  */
tINT32 tsSdpProcFsCleanup
(
 void
)
{
  tSDP_PROC_SUB_ENTRY sub_entry;
  tINT32 counter;

  TS_CHECK_NULL(_dir_root, -EINVAL);
  /*
   * first clean-up the frameworks tables
   */
  for (counter = 0; counter < TS_SDP_PROC_ENTRIES; counter++) {
    sub_entry = &_file_entry_list[counter];

    if (NULL != sub_entry->entry) {
      remove_proc_entry(sub_entry->name, _dir_root);
      sub_entry->entry = NULL;
    } /* if */
  } /* for */
  /*
   * remove SDP directory
   */
  remove_proc_entry(_dir_name_root, tsKernelProcDirGet());
  _dir_root = NULL;

  TS_TRACE(MOD_LNX_SDP, T_VERBOSE, TRACE_FLOW_CLEANUP,
	   "PROC: /proc filesystem cleanup complete.");

  return 0;
} /* tsSdpProcFsCleanup */

/* ========================================================================= */
/*..tsSdpProcFsInit -- initialize the proc filesystem entries  */
tINT32 tsSdpProcFsInit
(
 void
 )
{
  tSDP_PROC_SUB_ENTRY sub_entry;
  tINT32 result;
  tINT32 counter;
  /*
   * XXX still need to check this:
   * validate some assumptions the write parser will be making.
   */
  if (0 &&
      sizeof(tINT32) != sizeof(tSTR)) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: integers and pointers of a different size. <%d:%d>",
	     sizeof(tINT32), sizeof(tSTR));
    return -EFAULT;
  } /* if */

  if (NULL != _dir_root) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: already initialized!");
    return -EINVAL;
  } /* if */
  /*
   * create a gateway root, and main directories
   */
  _dir_root = proc_mkdir(_dir_name_root, tsKernelProcDirGet());
  if (NULL == _dir_root) {

    TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	     "PROC: Failed to create <%s> proc entry.", _dir_name_root);
    return -EINVAL;
  } /* if */

  _dir_root->owner = THIS_MODULE;

  for (counter = 0; counter < TS_SDP_PROC_ENTRIES; counter++) {
    sub_entry = &_file_entry_list[counter];

    if (sub_entry->type != counter) {
      result = -EFAULT;
      goto error;
    } /* if */

    sub_entry->entry = create_proc_entry(sub_entry->name,
					 S_IRUGO | S_IWUGO,
					 _dir_root);
    if (NULL == sub_entry->entry) {

      TS_TRACE(MOD_LNX_SDP, T_TERSE, TRACE_FLOW_FATAL,
	       "PROC: Failed to create <%s> framework proc entry.",
	       sub_entry->name);
      result = -EINVAL;
      goto error;
    } /* if */

    sub_entry->entry->read_proc  = _tsSdpProcReadParse;
    sub_entry->entry->write_proc = sub_entry->write;
    sub_entry->entry->data       = sub_entry;
    sub_entry->entry->owner      = THIS_MODULE;
  } /* for */

  return 0; /* success */
error:
  (void)tsSdpProcFsCleanup();
  return result;
} /* tsSdpProcFsInit */
