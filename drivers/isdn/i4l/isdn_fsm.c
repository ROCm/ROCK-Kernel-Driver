/* Linux ISDN subsystem, finite state machine
 *
 * Author       Karsten Keil
 * Copyright              by Karsten Keil      <keil@isdn4linux.de>
 *              2001-2002 by Kai Germaschewski <kai@germaschewski.name>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to    Jan den Ouden
 *              Fritz Elfert
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/isdn/fsm.h>

int
fsm_new(struct fsm *fsm)
{
	int i;
	int size = sizeof(fsm_fn) * fsm->st_cnt * fsm->ev_cnt;

	fsm->jumpmatrix = kmalloc(size, GFP_KERNEL);
	if (!fsm->jumpmatrix)
		return -ENOMEM;

	memset(fsm->jumpmatrix, 0, size);

	for (i = 0; i < fsm->fn_cnt; i++) {
		if (fsm->fn_tbl[i].st >= fsm->st_cnt || 
		    fsm->fn_tbl[i].ev >= fsm->ev_cnt) {
			printk(KERN_ERR "FsmNew Error line %d st(%d/%d) ev(%d/%d)\n", i,
			       fsm->fn_tbl[i].st, fsm->st_cnt, 
			       fsm->fn_tbl[i].ev, fsm->ev_cnt);
			continue;
		}
		fsm->jumpmatrix[fsm->st_cnt * fsm->fn_tbl[i].ev + fsm->fn_tbl[i].st] = fsm->fn_tbl[i].fn;
	}
	return 0;
}

void
fsm_free(struct fsm *fsm)
{
	kfree(fsm->jumpmatrix);
}

int
fsm_event(struct fsm_inst *fi, int event, void *arg)
{
	fsm_fn fn;

	if (fi->state >= fi->fsm->st_cnt || 
	    event >= fi->fsm->ev_cnt) {
		printk(KERN_ERR "FsmEvent Error st(%d/%d) ev(%d/%d)\n",
		       fi->state, fi->fsm->st_cnt,event, 
		       fi->fsm->ev_cnt);
		return -EINVAL;
	}
	fn = fi->fsm->jumpmatrix[fi->fsm->st_cnt * event + fi->state];
	if (!fn) {
		if (fi->debug)
			fi->printdebug(fi, "State %s Event %s no routine",
				       fi->fsm->st_str[fi->state],
				       fi->fsm->ev_str[event]);
		return -ESRCH;
	}
	if (fi->debug)
		fi->printdebug(fi, "State %s Event %s",
			       fi->fsm->st_str[fi->state],
			       fi->fsm->ev_str[event]);

	return fn(fi, event, arg);
}

void
fsm_change_state(struct fsm_inst *fi, int newstate)
{
	fi->state = newstate;
	if (fi->debug)
		fi->printdebug(fi, "ChangeState %s",
			       fi->fsm->st_str[newstate]);
}

#if 0
static void
FsmExpireTimer(struct FsmTimer *ft)
{
#if FSM_TIMER_DEBUG
	if (ft->fi->debug)
		ft->fi->printdebug(ft->fi, "FsmExpireTimer %lx", (long) ft);
#endif
	FsmEvent(ft->fi, ft->event, ft->arg);
}

void
FsmInitTimer(struct FsmInst *fi, struct FsmTimer *ft)
{
	ft->fi = fi;
	ft->tl.function = (void *) FsmExpireTimer;
	ft->tl.data = (long) ft;
#if FSM_TIMER_DEBUG
	if (ft->fi->debug)
		ft->fi->printdebug(ft->fi, "FsmInitTimer %lx", (long) ft);
#endif
	init_timer(&ft->tl);
}

void
FsmDelTimer(struct FsmTimer *ft, int where)
{
#if FSM_TIMER_DEBUG
	if (ft->fi->debug)
		ft->fi->printdebug(ft->fi, "FsmDelTimer %lx %d", (long) ft, where);
#endif
	del_timer(&ft->tl);
}

int
FsmAddTimer(struct FsmTimer *ft,
	    int millisec, int event, void *arg, int where)
{

#if FSM_TIMER_DEBUG
	if (ft->fi->debug)
		ft->fi->printdebug(ft->fi, "FsmAddTimer %lx %d %d",
			(long) ft, millisec, where);
#endif

	if (timer_pending(&ft->tl)) {
		printk(KERN_WARNING "FsmAddTimer: timer already active!\n");
		ft->fi->printdebug(ft->fi, "FsmAddTimer already active!");
		return -1;
	}
	init_timer(&ft->tl);
	ft->event = event;
	ft->arg = arg;
	ft->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&ft->tl);
	return 0;
}

void
FsmRestartTimer(struct FsmTimer *ft,
	    int millisec, int event, void *arg, int where)
{

#if FSM_TIMER_DEBUG
	if (ft->fi->debug)
		ft->fi->printdebug(ft->fi, "FsmRestartTimer %lx %d %d",
			(long) ft, millisec, where);
#endif

	if (timer_pending(&ft->tl))
		del_timer(&ft->tl);
	init_timer(&ft->tl);
	ft->event = event;
	ft->arg = arg;
	ft->tl.expires = jiffies + (millisec * HZ) / 1000;
	add_timer(&ft->tl);
}
#endif
