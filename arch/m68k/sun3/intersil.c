/*
 * arch/m68k/sun3/intersil.c
 *
 * basic routines for accessing the intersil clock within the sun3 machines
 *
 * started 11/12/1999 Sam Creasey
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/kd.h>

#include <asm/system.h>
#include <asm/intersil.h>


/* bits to set for start/run of the intersil */
#define STOP_VAL (INTERSIL_STOP | INTERSIL_INT_ENABLE | INTERSIL_24H_MODE)
#define START_VAL (INTERSIL_RUN | INTERSIL_INT_ENABLE | INTERSIL_24H_MODE)

/* does this need to be implemented? */
unsigned long sun3_gettimeoffset(void)
{ 
  return 1;
}

void sun3_gettod (int *yearp, int *monp, int *dayp,
                   int *hourp, int *minp, int *secp)
{
	u_char wday;
	volatile struct intersil_dt* todintersil;
	unsigned long flags;

        todintersil = (struct intersil_dt *) &intersil_clock->counter;

	save_and_cli(flags);

	intersil_clock->cmd_reg = STOP_VAL;

	*secp  = todintersil->csec;
        *hourp = todintersil->hour;
        *minp  = todintersil->minute;
        *secp  = todintersil->second; 
        *monp  = todintersil->month;
        *dayp  = todintersil->day;
        *yearp = todintersil->year+68; /* The base year for sun3 is 1968 */
	wday = todintersil->weekday;

	intersil_clock->cmd_reg = START_VAL;

	restore_flags(flags);
}


/* get/set hwclock */

int sun3_hwclk(int set, struct hwclk_time *t)
{
	volatile struct intersil_dt *todintersil;
	unsigned long flags;

        todintersil = (struct intersil_dt *) &intersil_clock->counter;

	save_and_cli(flags);

	intersil_clock->cmd_reg = STOP_VAL;

	/* set or read the clock */
	if(set) {
		todintersil->csec = 0;
		todintersil->hour = t->hour;
		todintersil->minute = t->min;
		todintersil->second = t->sec;
		todintersil->month = t->mon;
		todintersil->day = t->day;
		todintersil->year = t->year - 68;
		todintersil->weekday = t->wday;
	} else {
		/* read clock */
		t->sec = todintersil->csec;
		t->hour = todintersil->hour;
		t->min = todintersil->minute;
		t->sec = todintersil->second;
		t->mon = todintersil->month;
		t->day = todintersil->day;
		t->year = todintersil->year + 68;
		t->wday = todintersil->weekday;
	}

	intersil_clock->cmd_reg = START_VAL;

	restore_flags(flags);

	return 0;

}

