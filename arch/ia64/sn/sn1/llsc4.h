/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */

#ifdef STANDALONE
#include "lock.h"
#endif


#define DEF_NAPTICKS		0
#define DEF_PASSES		0
#define DEF_AUTO_PASSES		1000000
#define DEF_STOP_ON_ERROR	1
#define DEF_VERBOSE		0
#define DEF_LINECOUNT		2
#define DEF_ITER_MSG		0
#define DEF_VV			0xffffffff
#define DEF_LINEPAD		0x234



#define MAXCPUS			16
#define CACHELINE		64
#define MAX_LINECOUNT		1024
#define K			1024
#define	MB			(K*K)


#define	uint 		unsigned int
#define	ushort		unsigned short
#define vint		volatile int
#define vlong		volatile long

#define LOCKADDR(i)	&linep->lock[(i)]
#define LOCK(i)		set_lock(LOCKADDR(i), lockpat)
#define UNLOCK(i)	clr_lock(LOCKADDR(i), lockpat)
#define GETLOCK(i)	*LOCKADDR(i)
#define ZEROLOCK(i)	zero_lock(LOCKADDR(i))

#define CACHEALIGN(a)	((void*)((long)(a) & ~127L))

typedef uint		lock_t;
typedef uint		share_t;
typedef uint		private_t;

typedef struct {
	lock_t		lock[2];
	share_t		share[2];
	private_t	private[MAXCPUS];
	share_t		share0;
	share_t		share1;
} dataline_t ;


#define LINEPAD			k_linepad
#define LINESTRIDE		(((sizeof(dataline_t)+CACHELINE-1)/CACHELINE)*CACHELINE + LINEPAD)


typedef struct {
	vint		threadstate;
	uint		threadpasses;
	private_t	private[MAX_LINECOUNT];
} threadprivate_t;

typedef struct {
	vlong		sk_go;		/* 0=idle, 1=init, 2=run */
	long		sk_linecount;
	long		sk_passes;
	long		sk_napticks;
	long		sk_stop_on_error;
	long		sk_verbose;
	long		sk_iter_msg;
	long		sk_vv;
	long		sk_linepad;
	long		sk_options;
	long		sk_testnumber;
	vlong		sk_currentpass;
	void 		*sk_blocks;
	threadprivate_t	*sk_threadprivate[MAXCPUS];
} control_t;

/* Run state (k_go) constants */
#define ST_IDLE		0
#define ST_INIT		1
#define ST_RUN		2
#define ST_STOP		3
#define ST_ERRSTOP	4


/* Threadstate constants */
#define TS_STOPPED	0
#define	TS_RUNNING	1
#define TS_KILLED	2



int llsc_main (int cpuid, long mbasex);

