/*
 * arch/ppc/kernel/iSeries_asm.h
 *
 * Definitions used by various bits of low-level assembly code on iSeries.
 *
 * Copyright (C) 2001 IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#define CHECKLPQUEUE(ra,rb,rc)         \
	mfspr   rb,SPRG1;       /* Get Paca address */\
	lbz ra,PACALPPACA+LPPACAIPIINT(rb); /* Get IPI int flag */\
	cmpi    0,ra,0;         /* IPI occurred in hypervisor ? */\
	bne 99f;            /* If so, skip rest */\
	lwz ra,PACALPQUEUE(rb); /* Get LpQueue address */\
	cmpi    0,ra,0;         /* Does LpQueue exist? */\
	beq 99f;            /* If not skip rest */\
	lbz rb,LPQINUSEWORD(ra);    /* Test for LpQueue recursion */\
	cmpi    0,rb,1;         /* If we are about to recurse */\
	beq 99f;            /* If so, skip rest */\
	lwz rb,LPQCUREVENTPTR(ra);  /* Get current LpEvent */\
	lbz rb,LPEVENTFLAGS(rb);    /* Get Valid bit */\
	lbz rc,LPQOVERFLOW(ra); /* Get LpQueue overflow */\
	andi.   ra,rb,0x0080;       /* Isolate Valid bit */\
	or. ra,ra,rc;       /* 0 == no pending events */\
99:

#define CHECKDECR(ra,rb)           \
	mfspr   rb,SPRG1;       /* Get Paca address */\
	lbz ra,PACALPPACA+LPPACADECRINT(rb); /* Get DECR int flag */\
	cmpi    0,ra,0;         /* DECR occurred in hypervisor ? */\
	beq 99f;            /* If not, skip rest */\
	xor ra,ra,ra;           \
	stb ra,PACALPPACA+LPPACADECRINT(rb); /* Clear DECR int flag */\
99:

#define CHECKANYINT(ra,rb,rc)			\
	mfspr	rb,SPRG1;		/* Get Paca address */\
	ld	ra,PACALPPACA+LPPACAANYINT(rb); /* Get all interrupt flags */\
					/* Note use of ld, protected by soft/hard disabled */\
	cmpldi	0,ra,0;			/* Any interrupt occurred while soft disabled? */\
	bne	99f;			/* If so, skip rest */\
	lwz	ra,PACALPQUEUE(rb);	/* Get LpQueue address */\
	cmpi	0,ra,0;			/* Does LpQueue exist? */\
	beq	99f;			/* If not skip rest */\
	lwz	rb,LPQINUSEWORD(ra);	/* Test for LpQueue recursion */\
	cmpi	0,rb,1;			/* If we are about to recurse */\
	beq	99f;			/* If so, skip rest */\
	lwz	rb,LPQCUREVENTPTR(ra);	/* Get current LpEvent */\
	lbz	rb,LPEVENTFLAGS(rb);	/* Get Valid bit */\
	lbz	rc,LPQOVERFLOW(ra);	/* Get LpQueue overflow */\
	andi.	ra,rb,0x0080;		/* Isolate Valid bit */\
	or.	ra,ra,rc;		/* 0 == no pending events */\
99:

