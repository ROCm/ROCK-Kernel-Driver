/* $Id: lmgr.c,v 1.7 2000/06/26 08:59:14 keil Exp $
 *
 * Author       Karsten Keil (keil@isdn4linux.de)
 *
 *  Layermanagement module
 *
 * This file is (c) under GNU PUBLIC LICENSE
 *
 */

#define __NO_VERSION__
#include "hisax.h"

static void
error_handling_dchan(struct PStack *st, int Error)
{
	switch (Error) {
		case 'C':
		case 'D':
		case 'G':
		case 'H':
			st->l2.l2tei(st, MDL_ERROR | REQUEST, NULL);
			break;
	}
}

static void
hisax_manager(struct PStack *st, int pr, void *arg)
{
	long Code;

	switch (pr) {
		case (MDL_ERROR | INDICATION):
			Code = (long) arg;
			HiSax_putstatus(st->l1.hardware, "manager: MDL_ERROR",
				" %c %s", (char)Code, 
				test_bit(FLG_LAPD, &st->l2.flag) ?
				"D-channel" : "B-channel");
			if (test_bit(FLG_LAPD, &st->l2.flag))
				error_handling_dchan(st, Code);
			break;
	}
}

void
setstack_manager(struct PStack *st)
{
	st->ma.layer = hisax_manager;
}
