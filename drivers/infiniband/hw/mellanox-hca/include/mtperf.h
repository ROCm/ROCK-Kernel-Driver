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

#ifndef H_MTPERF_H
#define H_MTPERF_H

#ifdef MTPERF


#include <mtl_common.h>
#include <mosal.h>


typedef struct {
  u_int64_t time_accum;   /* Total time of segment accumulated so far */
  u_int32_t samples_cntr; /* Counter for number of time the segment was sampled */
  u_int64_t start;        /* Start time of current sample */
  u_int64_t sample_time;  /* temporary var. to compute sample time */
  u_int64_t sample_accum; /* for supporting pause/cont */
  u_int64_t latency_limit;  /* Limit of sample - to exclude context switches */
  u_int32_t exceed_cntr;    /* counter for measures which exceed latency_limit */
} MTPERF_segment_t;

/* Define a new performance measurement segment */
#define MTPERF_NEW_SEGMENT(segment_name,estimated_tick_latency) \
  MTPERF_segment_t MTPERF_##segment_name = {0,0,0,0,0,estimated_tick_latency*100,0}

/* Declare a segment which was defined in another source file */
#define MTPERF_EXTERN_SEGMENT(segment_name) extern MTPERF_segment_t MTPERF_##segment_name

/* Start a sample of the given segment (sample current time) */
#define MTPERF_TIME_START(segment_name) MTPERF_##segment_name.start = MOSAL_get_time_counter()

/* Pause sample of current segment (time accumulator updated , but counter does not) */
#define MTPERF_TIME_PAUSE(segment_name) /* not supported yet */

/* Continue segment time measure */
#define MTPERF_TIME_CONT(segment_name) /* not supported yet */

/* End time accumulation of the segment (counter++) */
#define MTPERF_TIME_END(segment_name)                         \
  MTPERF_##segment_name.sample_time =                         \
    (MOSAL_get_time_counter()- MTPERF_##segment_name.start) ; \
  if (MTPERF_##segment_name.sample_time > MTPERF_##segment_name.latency_limit) { \
    MTPERF_##segment_name.exceed_cntr++ ;                     \
  } else {  /* normal sample */                             \
    MTPERF_##segment_name.time_accum += MTPERF_##segment_name.sample_time;       \
    MTPERF_##segment_name.samples_cntr++ ;                    \
  }

/* MTPERF_TIME_END only if the given condition is met */
#define MTPERF_TIME_END_IF(segment_name,condition) \
  if (condition) {MTPERF_TIME_END(segment_name)}

/* Return current status */
#define MTPERF_REPORT(segment_name,samples_cnt_p,total_ticks_p,exceed_cntr_p) \
  *samples_cnt_p= MTPERF_##segment_name.samples_cntr;    \
  *total_ticks_p= MTPERF_##segment_name.time_accum;      \
  *exceed_cntr_p= MTPERF_##segment_name.exceed_cntr;     

/* Output current status using printf */

#ifndef MT_KERNEL
#define MTPERF_REPORT_PRINTF(segment_name)                                                 \
  printf("%s segment stats: %d times in " U64_FMT_SPEC "u ticks - average= " U64_FMT_SPEC "u ticks (%d exc.).\n",    \
   #segment_name,MTPERF_##segment_name.samples_cntr,                                       \
   (u_int64_t) MTPERF_##segment_name.time_accum,                                  \
   (u_int64_t) ((MTPERF_##segment_name.samples_cntr > 0) ?                        \
      MTPERF_##segment_name.time_accum/MTPERF_##segment_name.samples_cntr : 0),            \
    MTPERF_##segment_name.exceed_cntr)
#else
#define MTPERF_REPORT_PRINTF(segment_name)                                                 \
  printk("%s segment stats: %d times in " U64_FMT_SPEC "u ticks - average= %d ticks (%d exc.).\n",      \
    #segment_name,MTPERF_##segment_name.samples_cntr,                                      \
    (u_int64_t) MTPERF_##segment_name.time_accum,                                 \
    (MTPERF_##segment_name.samples_cntr > 0) ?                                             \
    (u_int32_t)(MTPERF_##segment_name.time_accum)/MTPERF_##segment_name.samples_cntr : 0,  \
    MTPERF_##segment_name.exceed_cntr)
#endif

/* Reset counter and samples accumulator of given segment */
#define MTPERF_RESET(segment_name)               \
  MTPERF_##segment_name.samples_cntr= 0; \
  MTPERF_##segment_name.time_accum= 0;   \
  MTPERF_##segment_name.exceed_cntr= 0

#else /* MTPERF not defined */
/* Define empty macros */

#define MTPERF_NEW_SEGMENT(segment_name,estimated_tick_latency) 

#define MTPERF_EXTERN_SEGMENT(segment_name) 

#define MTPERF_TIME_START(segment_name) 

#define MTPERF_TIME_PAUSE(segment_name) 

#define MTPERF_TIME_CONT(segment_name) 

#define MTPERF_TIME_END(segment_name)             
            
#define MTPERF_TIME_END_IF(segment_name,condition)

#define MTPERF_REPORT(segment_name,samples_cnt_p,total_ticks_p,exceed_cntr_p) 

#define MTPERF_REPORT_PRINTF(segment_name)                                                 

#define MTPERF_RESET(segment_name)               


#endif /* MTPERF */
#endif /* #ifndef H_MTPERF_H */
