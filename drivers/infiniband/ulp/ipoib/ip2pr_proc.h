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

  $Id: ip2pr_proc.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IP2PR_PROC_H
#define _TS_IP2PR_PROC_H

#include <linux/proc_fs.h>
#define TS_IP2PR_PROC_DIR_NAME "ip2pr"

/* --------------------------------------------------------------------- */
/* read callback prototype.                                              */
/* --------------------------------------------------------------------- */
typedef tINT32 (* tIP2PR_PROC_READ_CB_FUNC) (tSTR buffer,
					  tINT32  max_size,
					  tINT32  start,
                                          long *end
					  );
typedef struct tIP2PR_PROC_SUB_ENTRY_STRUCT tIP2PR_PROC_SUB_ENTRY_STRUCT,
              *tIP2PR_PROC_SUB_ENTRY;

struct tIP2PR_PROC_SUB_ENTRY_STRUCT {
  tSTR                   name;
  tINT32                 type;
  struct proc_dir_entry *entry;
  tIP2PR_PROC_READ_CB_FUNC  read;
  write_proc_t          *write;
}; /* tIP2PR_PROC_SUB_ENTRY_STRUCT */
/* --------------------------------------------------------------------- */
/* entry write parsing                                                   */
/* --------------------------------------------------------------------- */
/*
 * write types
 */
typedef enum {
  TS_IP2PR_PROC_WRITE_INT,
  TS_IP2PR_PROC_WRITE_STR,
  TS_IP2PR_PROC_WRITE_U64
} tIP2PR_PROC_WRITE_TYPE;

typedef enum {
  TS_IP2PR_PROC_ENTRY_ARP_WAIT   = 0, /* wait for ARP response table */
  TS_IP2PR_PROC_ENTRY_PATH_TABLE = 1, /* path record cache */
  TS_IP2PR_PROC_ENTRY_MAX_RETRIES = 2,/* max number of retries */
  TS_IP2PR_PROC_ENTRY_TIMEOUT = 3,    /* timeout between each retry */
  TS_IP2PR_PROC_ENTRY_BACKOUT = 4,    /* max backout value*/
  TS_IP2PR_PROC_ENTRY_CACHE_TIMEOUT = 5,    /* cache timeout */
  TS_IP2PR_PROC_ENTRY_TOTAL_REQ = 6,
  TS_IP2PR_PROC_ENTRY_ARP_TIMEOUT = 7,
  TS_IP2PR_PROC_ENTRY_PATH_TIMEOUT = 8,
  TS_IP2PR_PROC_ENTRY_TOTAL_FAIL = 9,

  TS_IP2PR_PROC_ENTRIES              /* number of entries in framework */
} TS_IP2PR_PROC_ENTRY_LIST;

typedef struct tIP2PR_PROC_ENTRY_WRITE_STRUCT tIP2PR_PROC_ENTRY_WRITE_STRUCT, \
              *tIP2PR_PROC_ENTRY_WRITE;
typedef struct tIP2PR_PROC_ENTRY_PARSE_STRUCT tIP2PR_PROC_ENTRY_PARSE_STRUCT, \
              *tIP2PR_PROC_ENTRY_PARSE;

struct tIP2PR_PROC_ENTRY_WRITE_STRUCT {
  tINT16  id;
  tINT16  type;
  union {
    tINT32  i;
    tSTR    s;
  } value;
}; /* tIP2PR_PROC_WRITE_STRUCT */

struct tIP2PR_PROC_ENTRY_PARSE_STRUCT {
  tINT16 id;
  tINT16 type;
  tSTR   value;
}; /* tIP2PR_PROC_ENTRY_PARSE_STRUCT */

#endif  /* _TS_IP2PR_PROC_H */
