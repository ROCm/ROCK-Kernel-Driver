/* 
 * linux/arch/sh/boards/cat68701/setup.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *               2001  Yutaro Ebihara
 *
 * Setup routines for A-ONE Corp CAT-68701 SH7708 Board
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <asm/io.h>
#include <asm/machvec.h>
#include <linux/config.h>
#include <linux/module.h>

const char *get_system_type(void)
{
	return "CAT-68701";
}

void platform_setup()
{
	/* dummy read erea5 (CS8900A) */
}

#ifdef CONFIG_HEARTBEAT
#include <linux/sched.h>
void heartbeat_cat68701()
{
        static unsigned int cnt = 0, period = 0 , bit = 0;
        cnt += 1;
        if (cnt < period) {
                return;
        }
        cnt = 0;

        /* Go through the points (roughly!):
         * f(0)=10, f(1)=16, f(2)=20, f(5)=35,f(inf)->110
         */
        period = 110 - ( (300<<FSHIFT)/
                         ((avenrun[0]/5) + (3<<FSHIFT)) );

	if(bit){ bit=0; }else{ bit=1; }
	outw(bit<<15,0x3fe);
}
#endif /* CONFIG_HEARTBEAT */

