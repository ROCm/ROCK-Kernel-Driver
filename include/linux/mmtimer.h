/*
 * Intel Multimedia Timer device interface
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2003 Silicon Graphics, Inc.  All rights reserved.
 *
 * This file should define an interface compatible with the IA-PC Multimedia
 * Timers Draft Specification (rev. 0.97) from Intel.  Note that some
 * hardware may not be able to safely export its registers to userspace,
 * so the ioctl interface should support all necessary functionality.
 *
 * 11/01/01 - jbarnes - initial revision
 * 9/10/04 - Christoph Lameter - remove interrupt support
 */

#ifndef _LINUX_MMTIMER_H
#define _LINUX_MMTIMER_H

/* name of the device, usually in /dev */
#define MMTIMER_NAME "mmtimer"
#define MMTIMER_FULLNAME "/dev/mmtimer"
#define MMTIMER_DESC "IA-PC Multimedia Timer"
#define MMTIMER_VERSION "1.0"

/*
 * Breakdown of the ioctl's available.  An 'optional' next to the command
 * indicates that supporting this command is optional, while 'required'
 * commands must be implemented if conformance is desired.
 *
 * MMTIMER_GETOFFSET - optional
 *   Should return the offset (relative to the start of the page where the
 *   registers are mapped) for the counter in question.
 *
 * MMTIMER_GETRES - required
 *   The resolution of the clock in femto (10^-15) seconds
 *
 * MMTIMER_GETFREQ - required
 *   Frequency of the clock in Hz
 *
 * MMTIMER_GETBITS - required
 *   Number of bits in the clock's counter
 *
 * MMTIMER_MMAPAVAIL - required
 *   Returns nonzero if the registers can be mmap'd into userspace, 0 otherwise
 *
 * MMTIMER_GETCOUNTER - required
 *   Gets the current value in the counter
 */
#define MMTIMER_IOCTL_BASE 'm'

#define MMTIMER_GETOFFSET _IO(MMTIMER_IOCTL_BASE, 0)
#define MMTIMER_GETRES _IOR(MMTIMER_IOCTL_BASE, 1, unsigned long)
#define MMTIMER_GETFREQ _IOR(MMTIMER_IOCTL_BASE, 2, unsigned long)
#define MMTIMER_GETBITS _IO(MMTIMER_IOCTL_BASE, 4)
#define MMTIMER_MMAPAVAIL _IO(MMTIMER_IOCTL_BASE, 6)
#define MMTIMER_GETCOUNTER _IOR(MMTIMER_IOCTL_BASE, 9, unsigned long)

/*
 * An mmtimer verification program.  WARNINGs are ok, but ERRORs indicate
 * that the device doesn't fully support the interface defined here.
 */
#ifdef _MMTIMER_TEST_PROGRAM

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include "mmtimer.h"

int main(int argc, char *argv[])
{
	int result, fd;
	unsigned long val = 0;
	unsigned long i;

	if((fd = open("/dev/"MMTIMER_NAME, O_RDONLY)) == -1) {
		printf("failed to open /dev/%s", MMTIMER_NAME);
		return 1;
	}

        /*
         * Can we mmap in the counter?
         */
        if((result = ioctl(fd, MMTIMER_MMAPAVAIL, 0)) == 1) {
                printf("mmap available\n");
	        /* ... so try getting the offset for each clock */
	        if((result = ioctl(fd, MMTIMER_GETOFFSET, 0)) != -ENOSYS)
	                printf("offset: %d\n", result);
	        else
	                printf("WARNING: offset unavailable for clock\n");
	}
        else
                printf("WARNING: mmap unavailable\n");

	/*
	 * Get the resolution in femtoseconds
	 */
        if((result = ioctl(fd, MMTIMER_GETRES, &val)) != -ENOSYS)
                printf("resolution: %ld femtoseconds\n", val);
        else
                printf("ERROR: failed to get resolution\n");

	/*
	 * Get the frequency in Hz
	 */
        if((result = ioctl(fd, MMTIMER_GETFREQ, &val)) != -ENOSYS)
		if(val < 10000000) /* less than 10 MHz? */
			printf("ERROR: frequency only %ld MHz, should be >= 10 MHz\n", val/1000000);
		else
			printf("frequency: %ld MHz\n", val/1000000);
        else
                printf("ERROR: failed to get frequency\n");

	/*
	 * How many bits in the counter?
	 */
        if((result = ioctl(fd, MMTIMER_GETBITS, 0)) != -ENOSYS)
                printf("bits in counter: %d\n", result);
        else
                printf("ERROR: can't get number of bits in counter\n");

	if((result = ioctl(fd, MMTIMER_GETCOUNTER, &val)) != -ENOSYS)
		printf("counter value: %ld\n", val);
	else
		printf("ERROR: can't get counter value\n");

	return 0;
}

#endif /* _MMTIMER_TEST_PROGRM */

#endif /* _LINUX_MMTIMER_H */
