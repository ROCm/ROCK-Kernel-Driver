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

#include "mosal_priv.h"

#include <mtl_common.h>

unsigned int mosal_major=0;
extern struct file_operations mosal_fops;

/* k2u_cbk prototype. Implemented in mosal_k2u_cbk.c */
call_result_t MOSAL_k2u_cbk_mod_init(void);


/******************************************************************************
 *  Function (kernel-mode-only)
 *      calib_counts_per_sec
 *
 *  Description: calibrate counts in 1 sec and store in a static global variable
 *               for use by MOSAL_get_counts_per_sec
 *
 *  Parameters:
 *
 *  Returns:
 *
 ******************************************************************************/
static void calib_counts_per_sec(void);


u_int32_t MOSAL_letobe32(u_int32_t le)
{
    return __cpu_to_be32(le);
}


u_int32_t MOSAL_betole32(u_int32_t be)
{
  return __be32_to_cpu(be);
}


/*
 *  MOSAL_get_exec_ctx - get execution context
 */
MOSAL_exec_ctx_t MOSAL_get_exec_ctx()
{
	if (in_interrupt())
		return  MOSAL_IN_ISR;
	/* pseudo-code
	if (in_tasklet())
		return  MOSAL_IN_DPC;
	*/
	return MOSAL_IN_TASK;
}



/*
 * MOSAL physical addresses access end
 * -----------------------------------
 */


MOSAL_pid_t MOSAL_getpid(void) { return current->pid; }
MT_bool MOSAL_is_privileged()     { return TRUE;         }

call_result_t MOSAL_init(unsigned int major)
{
  int i;
  int new_major = major;
  call_result_t rc;


  rc = MOSAL_ioremap_init();
  if ( rc != MT_OK ) {
    goto ex_MOSAL_ioremap_init_fail;
  }

  rc = MOSAL_iobuf_init();
  if (rc != MT_OK ) {
    goto ex_MOSAL_iobuf_init_fail;
  }

  /*Initialize memory locking mechanizm*/
  rc = MOSAL_mlock_init();
  if ( rc != MT_OK ) {
    goto ex_MOSAL_mlock_init_fail;
  }

  /* Initialize k2u_cbk mechanism */
  rc = MOSAL_k2u_cbk_mod_init();
  if ( rc != MT_OK ) {
    goto ex_MOSAL_k2u_cbk_mod_init_fail;
  }
  
  calib_counts_per_sec();


  rc = mosal_chrdev_register(&new_major, MOSAL_CHAR_DEV_NAME, &mosal_fops);
  if (rc != MT_OK ) {
    goto ex_mosal_chrdev_register_fail;
  }

  mosal_major=new_major;

  for (i= 0; i < MOSAL_MAX_QHANDLES; i++)  qhandles[i]= NULL;

  return MT_OK;


ex_mosal_chrdev_register_fail:
ex_MOSAL_k2u_cbk_mod_init_fail:
  MOSAL_mlock_cleanup();
ex_MOSAL_mlock_init_fail:
  MOSAL_iobuf_cleanup();
ex_MOSAL_iobuf_init_fail:
  MOSAL_ioremap_cleanup();
ex_MOSAL_ioremap_init_fail:
  return rc;
}

void MOSAL_cleanup()
{
  int i;

  for (i= 0; i < MOSAL_MAX_QHANDLES; i++)  {
    if (qhandles[i] != NULL)  MOSAL_qdestroy(i);
  }

  mosal_chrdev_unregister(mosal_major, MOSAL_CHAR_DEV_NAME);
  MOSAL_mlock_cleanup();
  MOSAL_iobuf_cleanup();
  MOSAL_ioremap_cleanup();
  mtl_common_cleanup();
}


#define CALIBRATE_LATCH	(5 * LATCH)
#define CALIBRATE_TIME	(5 * 1000020/HZ)


static u_int64_t priv_clocks_per_sec; /* this variable holds the number of clocks per 1 sec */


u_int64_t MOSAL_get_counts_per_sec(void)
{
  return priv_clocks_per_sec;
}



u_int64_t my_calib_cps(void)
{
  unsigned long start_jiffies;
  u_int64_t start_cps;

  start_jiffies = jiffies;
  start_cps = MOSAL_get_time_counter();
  do {
    schedule();
  }
  while ( jiffies < (start_jiffies+HZ) );
  return MOSAL_get_time_counter()-start_cps;
}

/*
 *  MOSAL_calib_counts_per_sec
 */
static void calib_counts_per_sec(void)
{
#ifdef __i386__
       /* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Now let's take care of CTC channel 2
	 *
	 * Set the Gate high, program CTC channel 2 for mode 0,
	 * (interrupt on terminal count mode), binary count,
	 * load 5 * LATCH count, (LSB and MSB) to begin countdown.
	 */
	outb(0xb0, 0x43);			/* binary, mode 0, LSB/MSB, Ch 2 */
	outb(CALIBRATE_LATCH & 0xff, 0x42);	/* LSB of count */
	outb(CALIBRATE_LATCH >> 8, 0x42);	/* MSB of count */

	{
		unsigned long startlow, starthigh;
		unsigned long endlow, endhigh;
		unsigned long count;

		rdtsc(startlow,starthigh);
		count = 0;
		do {
			count++;
		} while ((inb(0x61) & 0x20) == 0);
		rdtsc(endlow,endhigh);

		/* Error: ECTCNEVERSET */
		if (count <= 1)
			goto bad_ctc;

		/* 64-bit subtract - gcc just messes up with long longs */
		__asm__("subl %2,%0\n\t"
			"sbbl %3,%1"
			:"=a" (endlow), "=d" (endhigh)
			:"g" (startlow), "g" (starthigh),
			 "0" (endlow), "1" (endhigh));

		/* Error: ECPUTOOFAST */
		if (endhigh)
			goto bad_ctc;

		/* Error: ECPUTOOSLOW */
		if (endlow <= CALIBRATE_TIME)
			goto bad_ctc;

		__asm__("divl %2"
			:"=a" (endlow), "=d" (endhigh)
			:"r" (endlow), "0" (0), "1" (CALIBRATE_TIME));

		priv_clocks_per_sec = (0xffffffff/endlow)*1000000 + 1000000/endlow;
	}

	/*
	 * The CTC wasn't reliable: we got a hit on the very first read,
	 * or the CPU was so fast/slow that the quotient wouldn't fit in
	 * 32 bits..
	 */
bad_ctc:
	return;
#elif defined(__x86_64__) && defined(cpu_khz)
  priv_clocks_per_sec = cpu_khz * 1000;
#elif defined(powerpc) || defined(__powerpc__)
{
		#define PPC_CALIBRATION_TIME_USECS		1000000
		#define PER_SEC_RATIO		((int)(100000 / (PPC_CALIBRATION_TIME_USECS / 10)))
        u_int64_t before, after;
        before = MOSAL_ppc_cputime_get();
        MOSAL_delay_execution(PPC_CALIBRATION_TIME_USECS);	/* wait for a while */
        after = MOSAL_ppc_cputime_get();
        priv_clocks_per_sec = (after - before) * PER_SEC_RATIO;
        MTL_ERROR1("calib_counts_per_sec: before" U64_FMT ", after " U64_FMT ", diff %d, clocks %d\n",
        	before, after, (int)(after - before), (int)priv_clocks_per_sec);
}
#else
  priv_clocks_per_sec = my_calib_cps();
#endif
}
