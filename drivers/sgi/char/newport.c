/*
 * newport.c: context switching the newport graphics card and
 *            newport graphics support.
 *
 * Author: Miguel de Icaza
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <asm/types.h>
#include <asm/gfx.h>
#include <asm/ng1.h>
#include <asm/uaccess.h>
#include <video/newport.h>
#include <linux/module.h>

struct newport_regs *npregs;

EXPORT_SYMBOL(npregs);

/* Kernel routines for supporting graphics context switching */

void newport_save (void *y)
{
	newport_ctx *x = y;
	newport_wait ();

#define LOAD(val) x->val = npregs->set.val;
#define LOADI(val) x->val = npregs->set.val.word;
#define LOADC(val) x->val = npregs->cset.val;
	
	LOAD(drawmode1);
	LOAD(drawmode0);
	LOAD(lsmode);
	LOAD(lspattern);
	LOAD(lspatsave);
	LOAD(zpattern);
	LOAD(colorback);
	LOAD(colorvram);
	LOAD(alpharef);
	LOAD(smask0x);
	LOAD(smask0y);
	LOADI(_xstart);
	LOADI(_ystart);
	LOADI(_xend);
	LOADI(_yend);
	LOAD(xsave);
	LOAD(xymove);
	LOADI(bresd);
	LOADI(bress1);
	LOAD(bresoctinc1);
	LOAD(bresrndinc2);
	LOAD(brese1);
	LOAD(bress2);
	LOAD(aweight0);
	LOAD(aweight1);
	LOADI(colorred);
	LOADI(coloralpha);
	LOADI(colorgrn);
	LOADI(colorblue);
	LOADI(slopered);
	LOADI(slopealpha);
	LOADI(slopegrn);
	LOADI(slopeblue);
	LOAD(wrmask);
	LOAD(hostrw0);
	LOAD(hostrw1);
	
        /* configregs */
	
	LOADC(smask1x);
	LOADC(smask1y);
	LOADC(smask2x);
	LOADC(smask2y);
	LOADC(smask3x);
	LOADC(smask3y);
	LOADC(smask4x);
	LOADC(smask4y);
	LOADC(topscan);
	LOADC(xywin);
	LOADC(clipmode);
	LOADC(config);

	/* Mhm, maybe I am missing something, but it seems that
	 * saving/restoring the DCB is only a matter of saving these
	 * registers
	 */

	newport_bfwait ();
	LOAD (dcbmode);
	newport_bfwait ();
	x->dcbdata0 = npregs->set.dcbdata0.byword;
	newport_bfwait ();
	LOAD(dcbdata1);
}

/*
 * Importat things to keep in mind when restoring the newport context:
 *
 * 1. slopered register is stored as a 2's complete (s12.11);
 *    needs to be converted to a signed magnitude (s(8)12.11).
 *
 * 2. xsave should be stored after xstart.
 *
 * 3. None of the registers should be written with the GO address.
 *    (read the docs for more details on this).
 */
void newport_restore (void *y)
{
	newport_ctx *x = y;
#define STORE(val) npregs->set.val = x->val
#define STOREI(val) npregs->set.val.word = x->val
#define STOREC(val) npregs->cset.val = x->val
	newport_wait ();
	
	STORE(drawmode1);
	STORE(drawmode0);
	STORE(lsmode);
	STORE(lspattern);
	STORE(lspatsave);
	STORE(zpattern);
	STORE(colorback);
	STORE(colorvram);
	STORE(alpharef);
	STORE(smask0x);
	STORE(smask0y);
	STOREI(_xstart);
	STOREI(_ystart);
	STOREI(_xend);
	STOREI(_yend);
	STORE(xsave);
	STORE(xymove);
	STOREI(bresd);
	STOREI(bress1);
	STORE(bresoctinc1);
	STORE(bresrndinc2);
	STORE(brese1);
	STORE(bress2);
	STORE(aweight0);
	STORE(aweight1);
	STOREI(colorred);
	STOREI(coloralpha);
	STOREI(colorgrn);
	STOREI(colorblue);
	STOREI(slopered);
	STOREI(slopealpha);
	STOREI(slopegrn);
	STOREI(slopeblue);
	STORE(wrmask);
	STORE(hostrw0);
	STORE(hostrw1);
	
        /* configregs */
	
	STOREC(smask1x);
	STOREC(smask1y);
	STOREC(smask2x);
	STOREC(smask2y);
	STOREC(smask3x);
	STOREC(smask3y);
	STOREC(smask4x);
	STOREC(smask4y);
	STOREC(topscan);
	STOREC(xywin);
	STOREC(clipmode);
	STOREC(config);

	/* FIXME: restore dcb thingies */
}

int
newport_ioctl (int card, int cmd, unsigned long arg)
{
	switch (cmd){
	case NG1_SETDISPLAYMODE: {
		struct ng1_setdisplaymode_args request;
		
		if (copy_from_user (&request, (void *) arg, sizeof (request)))
			return -EFAULT;

		newport_wait ();
		newport_bfwait ();
		npregs->set.dcbmode = DCB_XMAP0 | XM9_CRS_FIFO_AVAIL |
			DCB_DATAWIDTH_1 | R_DCB_XMAP9_PROTOCOL;
		xmap9FIFOWait (npregs);
		
		/* FIXME: timing is wrong, just be extracted from 
		 * the per-board timing table.  I still have to figure
		 * out where this comes from
		 *
		 * This is used to select the protocol used to talk to
		 * the xmap9.  For now I am using 60, selecting the
		 * WSLOW_DCB_XMAP9_PROTOCOL.
		 *
		 * Robert Tray comments on this issue:
		 *
		 * cfreq refers to the frequency of the monitor
		 * (ie. the refresh rate).  Our monitors run typically
		 * between 60 Hz and 76 Hz.  But it will be as low as
		 * 50 Hz if you're displaying NTSC/PAL and as high as
		 * 120 Hz if you are runining in stereo mode.  You
		 * might want to try the WSLOW values.
		 */
		xmap9SetModeReg (npregs, request.wid, request.mode, 60);
		return 0;
	}
	case NG1_SET_CURSOR_HOTSPOT: {
		struct ng1_set_cursor_hotspot request;
		
		if (copy_from_user (&request, (void *) arg, sizeof (request)))
			return -EFAULT;
		/* FIXME: make request.xhot, request.yhot the hot spot */
		return 0;
	}
	
	case NG1_SETGAMMARAMP0:
		/* FIXME: load the gamma ramps :-) */
		return 0;

	}
	return -EINVAL;
}
