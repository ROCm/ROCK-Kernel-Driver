/*
 * (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  Library for common functions for Intel SpeedStep v.1 and v.2 support
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */



/* processors */

#define SPEEDSTEP_PROCESSOR_PIII_C_EARLY	0x00000001  /* Coppermine core */
#define SPEEDSTEP_PROCESSOR_PIII_C		0x00000002  /* Coppermine core */
#define SPEEDSTEP_PROCESSOR_PIII_T 		0x00000003  /* Tualatin core */
#define SPEEDSTEP_PROCESSOR_P4M			0x00000004  /* P4-M with 100 MHz FSB */

/* speedstep states -- only two of them */

#define SPEEDSTEP_HIGH                  0x00000000
#define SPEEDSTEP_LOW                   0x00000001


/* detect a speedstep-capable processor */
extern unsigned int speedstep_detect_processor (void);

/* detect the current speed (in khz) of the processor */
extern unsigned int speedstep_get_processor_frequency(unsigned int processor);


/* detect the low and high speeds of the processor. The callback 
 * set_state"'s first argument is either SPEEDSTEP_HIGH or 
 * SPEEDSTEP_LOW; the second argument is zero so that no 
 * cpufreq_notify_transition calls are initiated.
 */
extern unsigned int speedstep_get_freqs(unsigned int processor,
	  unsigned int *low_speed,
	  unsigned int *high_speed,
	  void (*set_state) (unsigned int state, unsigned int notify));
