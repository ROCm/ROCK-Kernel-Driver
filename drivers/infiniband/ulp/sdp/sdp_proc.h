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

  $Id: sdp_proc.h 35 2004-04-09 05:34:32Z roland $
*/

#ifndef _TS_SDP_PROC_H
#define _TS_SDP_PRC_H
/*
 * topspin specific headers.
 */
#include <ib_legacy_types.h>
#include <linux/proc_fs.h>

#define TS_SDP_PROC_DIR_NAME "sdp"
/* --------------------------------------------------------------------- */
/* read callback prototype.                                              */
/* --------------------------------------------------------------------- */
typedef tINT32 (* tSDP_PROC_READ_CB_FUNC) (tSTR buffer,
					  tINT32  max_size,
					  tINT32  start,
                                          long *end
					  );
/* --------------------------------------------------------------------- */
/* file and directory entries                                            */
/* --------------------------------------------------------------------- */
/*
 * proc filesystem framework table/file entries
 */
typedef enum {
  TS_SDP_PROC_ENTRY_MAIN_BUFF  = 0, /* buffer pool */
  TS_SDP_PROC_ENTRY_MAIN_CONN  = 1, /* connection table */
  TS_SDP_PROC_ENTRY_DATA_CONN  = 2, /* connection table */
  TS_SDP_PROC_ENTRY_RDMA_CONN  = 3, /* connection table */
  TS_SDP_PROC_ENTRY_OPT_CONN   = 4, /* socket option table */
  TS_SDP_PROC_ENTRY_ROOT_TABLE = 5, /* device table */

  TS_SDP_PROC_ENTRIES              /* number of entries in framework */
} TS_SDP_PROC_ENTRY_LIST;

typedef struct tSDP_PROC_SUB_ENTRY_STRUCT tSDP_PROC_SUB_ENTRY_STRUCT,
              *tSDP_PROC_SUB_ENTRY;

struct tSDP_PROC_SUB_ENTRY_STRUCT {
  tSTR                   name;
  tINT32                 type;
  struct proc_dir_entry *entry;
  tSDP_PROC_READ_CB_FUNC  read;
  write_proc_t          *write;
}; /* tSDP_PROC_SUB_ENTRY_STRUCT */
/* --------------------------------------------------------------------- */
/* entry write parsing                                                   */
/* --------------------------------------------------------------------- */
/*
 * write types
 */
typedef enum {
  TS_SDP_PROC_WRITE_INT,
  TS_SDP_PROC_WRITE_STR,
  TS_SDP_PROC_WRITE_U64
} tSDP_PROC_WRITE_TYPE;

typedef struct tSDP_PROC_ENTRY_WRITE_STRUCT tSDP_PROC_ENTRY_WRITE_STRUCT, \
              *tSDP_PROC_ENTRY_WRITE;
typedef struct tSDP_PROC_ENTRY_PARSE_STRUCT tSDP_PROC_ENTRY_PARSE_STRUCT, \
              *tSDP_PROC_ENTRY_PARSE;

struct tSDP_PROC_ENTRY_WRITE_STRUCT {
  tINT16  id;
  tINT16  type;
  union {
    tINT32  i;
    tSTR    s;
  } value;
}; /* tSDP_PROC_WRITE_STRUCT */

struct tSDP_PROC_ENTRY_PARSE_STRUCT {
  tINT16 id;
  tINT16 type;
  tSTR   value;
}; /* tSDP_PROC_ENTRY_PARSE_STRUCT */

/* --------------------------------------------------------------------- */
/* configuration data types for each table in /proc.                     */
/* --------------------------------------------------------------------- */

#endif /* _TS_SDP_PROC_H */
