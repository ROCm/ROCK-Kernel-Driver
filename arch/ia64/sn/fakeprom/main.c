/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc.  All rights reserved.
 */



#include <linux/config.h>
#include <linux/types.h>
#include <asm/bitops.h>

extern void klgraph_init(void);
void bedrock_init(int);
void synergy_init(int, int);
void sys_fw_init (const char *args, int arglen, int bsp);

volatile int	bootmaster=0;		/* Used to pick bootmaster */
volatile int	nasidmaster[128]={0};	/* Used to pick node/synergy masters */
int		init_done=0;
extern int	bsp_lid;

#define get_bit(b,p)	(((*p)>>(b))&1)

int
fmain(int lid, int bsp) {
	int	syn, nasid, cpu;

	/*
	 * First lets figure out who we are. This is done from the
	 * LID passed to us.
	 */
	nasid = (lid>>16)&0xfff;
	cpu = (lid>>28)&3;
	syn = 0;
	
	/*
	 * Now pick a nasid master to initialize Bedrock registers.
	 */
	if (test_and_set_bit(8, &nasidmaster[nasid]) == 0) {
		bedrock_init(nasid);
		test_and_set_bit(9, &nasidmaster[nasid]);
	} else
		while (get_bit(9, &nasidmaster[nasid]) == 0);
	

	/*
	 * Now pick a BSP & finish init.
	 */
	if (test_and_set_bit(0, &bootmaster) == 0) {
		sys_fw_init(0, 0, bsp);
		test_and_set_bit(1, &bootmaster);
	} else
		while (get_bit(1, &bootmaster) == 0);

	return (lid == bsp_lid);
}


void
bedrock_init(int nasid)
{
	nasid = nasid;		/* to quiet gcc */
#if 0
	/*
	 * Undef if you need fprom to generate a 1 node klgraph
	 * information .. only works for 1 node for nasid 0.
	 */
	klgraph_init();
#endif
}


void
synergy_init(int nasid, int syn)
{
	long	*base;
	long	off;

	/*
	 * Enable all FSB flashed interrupts.
	 * I'd really like defines for this......
	 */
	base = (long*)0x80000e0000000000LL;		/* base of synergy regs */
	for (off = 0x2a0; off < 0x2e0; off+=8)		/* offset for VEC_MASK_{0-3}_A/B */
		*(base+off/8) = -1LL;

	/*
	 * Set the NASID in the FSB_CONFIG register.
	 */
	base = (long*)0x80000e0000000450LL;
	*base = (long)((nasid<<16)|(syn<<9));
}


/* Why isnt there a bcopy/memcpy in lib64.a */

void* 
memcpy(void * dest, const void *src, size_t count)
{
	char *s, *se, *d;

	for(d=dest, s=(char*)src, se=s+count; s<se; s++, d++)
		*d = *s;
	return dest;
}
