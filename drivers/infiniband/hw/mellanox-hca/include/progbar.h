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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/
#ifndef H_PROG_BAR_H
#define H_PROG_BAR_H

#define MAX_STR_SIZE 128
#define OBJ_SIG_PROG 0x24323244


typedef struct {
    char      text[MAX_STR_SIZE];      /*  Progress bar text */
    u_int32_t bar_off;                 /*  Offest in text where progress bar is found */
    u_int32_t prc_off;                 /*  Offset in text where completion percent is found */
    u_int32_t slots;                   /*  Total number of slot in this progress bar. */
    u_int32_t tick_count;              /*  Current number of ticks. */
    u_int32_t obj_sig;                 /*  Object signature. */
} prog_bar_t;

/*  Function: init_prog */

/*  Description: Create new progress bar object. */

/*  Author: Eitan Rabin  */

/*  Changes */

prog_bar_t * init_prog(int slots, char * text);

/*  Function: prog_tick */

/*  Description: increment tick count of a progress bar */

/*  Author: Eitan Rabin  */

/*  Changes */

void prog_tick(prog_bar_t * probj);


/*  Fuction: prog_destroy */

/*  Description: destory progress bug. */

/*  Author: Eitan Rabin */

/*  Changes */

void prog_destroy(prog_bar_t * probj);

#endif
