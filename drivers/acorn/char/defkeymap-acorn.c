/*
 *  linux/drivers/acorn/char/defkeymap.c
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/types.h>
#include <linux/keyboard.h>
#include <linux/kd.h>

/* Normal (maps 1:1 with no processing) */
#define KTn	0xF0
/* Function keys */
#define KTf	0xF1
/* Special (Performs special house-keeping funcs) */
#define KTs	0xF2
#define KIGNORE		K(KTs, 0)	/* Ignore */
#define KENTER		K(KTs, 1)	/* Enter */
#define KREGS		K(KTs, 2)	/* Regs */
#define KMEM		K(KTs, 3)	/* Mem */
#define KSTAT		K(KTs, 4)	/* State */
#define KINTR		K(KTs, 5)	/* Intr */
#define Ksl	6	/* Last console */
#define KCAPSLK		K(KTs, 7)	/* Caps lock */
#define KNUMLK		K(KTs, 8)	/* Num-lock */
#define KSCRLLK		K(KTs, 9)	/* Scroll-lock */
#define KSCRLFOR	K(KTs,10)	/* Scroll forward */
#define KSCRLBAK	K(KTs,11)	/* Scroll back */
#define KREBOOT		K(KTs,12)	/* Reboot */
#define KCAPSON		K(KTs,13)	/* Caps on */
#define KCOMPOSE	K(KTs,14)	/* Compose */
#define KSAK		K(KTs,15)	/* SAK */
#define CONS_DEC	K(KTs,16)	/* Dec console */
#define CONS_INC	K(KTs,17)	/* Incr console */
#define KFLOPPY		K(KTs,18)	/* Floppy */
/* Key pad (0-9 = digits, 10=+, 11=-, 12=*, 13=/, 14=enter, 16=., 17=# */
#define KTp	0xF3
#define KPAD_0		K(KTp, 0 )
#define KPAD_1  	K(KTp, 1 )
#define KPAD_2		K(KTp, 2 )
#define KPAD_3		K(KTp, 3 )
#define KPAD_4		K(KTp, 4 )
#define KPAD_5		K(KTp, 5 )
#define KPAD_6		K(KTp, 6 )
#define KPAD_7		K(KTp, 7 )
#define KPAD_8		K(KTp, 8 )
#define KPAD_9		K(KTp, 9 )
#define KPAD_PL		K(KTp,10 )
#define KPAD_MI		K(KTp,11 )
#define KPAD_ML		K(KTp,12 )
#define KPAD_DV		K(KTp,13 )
#define KPAD_EN		K(KTp,14 )
#define KPAD_DT		K(KTp,16 )
#define KPAD_HS		K(KTp,20 )
/* Console switching */
#define KCn	0xF5
/* Cursor */
#define KTc	0xF6
#define Kcd	0	/* Cursor down */
#define Kcl	1	/* Cursor left */
#define Kcr	2	/* Cursor right */
#define Kcu	3	/* Cursor up */
/* Shift/alt modifiers etc */
#define KMd	0xF7
#define KSHIFT		K(KMd, 0 )
#define KALTGR		K(KMd, 1 )
#define KCTRL		K(KMd, 2 )
#define KALT		K(KMd, 3 )
/* Meta */
#define KMt	0xF8
#define KAs	0xF9
#define KPADA_0		K(KAs, 0 )
#define KPADA_1		K(KAs, 1 )
#define KPADA_2		K(KAs, 2 )
#define KPADA_3		K(KAs, 3 )
#define KPADA_4		K(KAs, 4 )
#define KPADA_5		K(KAs, 5 )
#define KPADA_6		K(KAs, 6 )
#define KPADA_7		K(KAs, 7 )
#define KPADA_8		K(KAs, 8 )
#define KPADA_9		K(KAs, 9 )
#define KPADB_0		K(KAs,10 )
#define KPADB_1		K(KAs,11 )
#define KPADB_2		K(KAs,12 )
#define KPADB_3		K(KAs,13 )
#define KPADB_4		K(KAs,14 )
#define KPADB_5		K(KAs,15 )
#define KPADB_6		K(KAs,16 )
#define KPADB_7		K(KAs,17 )
#define KPADB_8		K(KAs,18 )
#define KPADB_9		K(KAs,19 )
/* Locking keys */
#define KLk	0xFA
/* Letters */
#define KTl	0xFB

u_short plain_map[NR_KEYS]=
{
  K(KTn, 27),K(KTf,  0),K(KTf,  1),K(KTf,  2 ),K(KTf,  3),K(KTf,  4),K(KTf,  5 ),K(KTf,  6),
  K(KTf,  7),K(KTf,  8),K(KTf,  9),K(KTf, 10 ),K(KTf, 11),KIGNORE   ,KSCRLLK    ,KINTR     ,
  K(KTn,'`'),K(KTn,'1'),K(KTn,'2'),K(KTn,'3' ),K(KTn,'4'),K(KTn,'5'),K(KTn,'6' ),K(KTn,'7'),
  K(KTn,'8'),K(KTn,'9'),K(KTn,'0'),K(KTn,'-' ),K(KTn,'='),K(KTn,'£'),K(KTn,127 ),K(KTf,21 ),
  K(KTf,20 ),K(KTf,24 ),KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,K(KTn,  9 ),K(KTl,'q'),
  K(KTl,'w'),K(KTl,'e'),K(KTl,'r'),K(KTl,'t' ),K(KTl,'y'),K(KTl,'u'),K(KTl,'i' ),K(KTl,'o'),
  K(KTl,'p'),K(KTn,'['),K(KTn,']'),K(KTn,'\\'),K(KTf,22 ),K(KTf,23 ),K(KTf,25  ),KPAD_7    ,
  KPAD_8    ,KPAD_9    ,KPAD_MI   ,KCTRL      ,K(KTl,'a'),K(KTl,'s'),K(KTl,'d' ),K(KTl,'f'),
  K(KTl,'g'),K(KTl,'h'),K(KTl,'j'),K(KTl,'k' ),K(KTl,'l'),K(KTn,';'),K(KTn,'\''),KENTER    ,
  KPAD_4    ,KPAD_5    ,KPAD_6    ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KTl,'z' ),K(KTl,'x'),
  K(KTl,'c'),K(KTl,'v'),K(KTl,'b'),K(KTl,'n' ),K(KTl,'m'),K(KTn,','),K(KTn,'.' ),K(KTn,'/'),
  KSHIFT    ,K(KTc,Kcu),KPAD_1    ,KPAD_2     ,KPAD_3    ,KCAPSLK   ,KALT       ,K(KTn,' '),
  KALTGR    ,KCTRL     ,K(KTc,Kcl),K(KTc,Kcd ),K(KTc,Kcr),KPAD_0    ,KPAD_DT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

u_short shift_map[NR_KEYS]=
{
  K(KTn, 27),K(KTf, 10),K(KTf, 11),K(KTf, 12 ),K(KTf, 13),K(KTf, 14),K(KTf, 15 ),K(KTf, 16),
  K(KTf, 17),K(KTf, 18),K(KTf, 19),K(KTf, 20 ),K(KTf, 21),KIGNORE   ,KMEM       ,KINTR     ,
  K(KTn,'~'),K(KTn,'!'),K(KTn,'@'),K(KTn,'#' ),K(KTn,'$'),K(KTn,'%'),K(KTn,'^' ),K(KTn,'&'),
  K(KTn,'*'),K(KTn,'('),K(KTn,')'),K(KTn,'_' ),K(KTn,'+'),K(KTn,'¤'),K(KTn,127 ),K(KTf,21 ),
  K(KTf,20 ),KSCRLBAK  ,KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,K(KTn,  9 ),K(KTl,'Q'),
  K(KTl,'W'),K(KTl,'E'),K(KTl,'R'),K(KTl,'T' ),K(KTl,'Y'),K(KTl,'U'),K(KTl,'I' ),K(KTl,'O'),
  K(KTl,'P'),K(KTn,'{'),K(KTn,'}'),K(KTn,'|' ),K(KTf,22 ),K(KTf,23 ),KSCRLFOR   ,KPAD_7    ,
  KPAD_8    ,KPAD_9    ,KPAD_MI   ,KCTRL      ,K(KTl,'A'),K(KTl,'S'),K(KTl,'D' ),K(KTl,'F'),
  K(KTl,'G'),K(KTl,'H'),K(KTl,'J'),K(KTl,'K' ),K(KTl,'L'),K(KTn,':'),K(KTn,'"' ),KENTER    ,
  KPAD_4    ,KPAD_5    ,KPAD_6    ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KTl,'Z' ),K(KTl,'X'),
  K(KTl,'C'),K(KTl,'V'),K(KTl,'B'),K(KTl,'N' ),K(KTl,'M'),K(KTn,'<'),K(KTn,'>' ),K(KTn,'?'),
  KSHIFT    ,K(KTc,Kcu),KPAD_1    ,KPAD_2     ,KPAD_3    ,KCAPSLK   ,KALT       ,K(KTn,' '),
  KALTGR    ,KCTRL     ,K(KTc,Kcl),K(KTc,Kcd ),K(KTc,Kcr),KPAD_0    ,KPAD_DT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

u_short altgr_map[NR_KEYS]=
{
  KIGNORE   ,K(KCn,12 ),K(KCn,13 ),K(KCn,14  ),K(KCn,15 ),K(KCn,16 ),K(KCn,17  ),K(KCn, 18),
  K(KCn, 19),K(KCn,20 ),K(KCn,21 ),K(KCn,22  ),K(KCn,23 ),KIGNORE   ,KREGS      ,KINTR     ,
  KIGNORE   ,KIGNORE   ,K(KTn,'@'),KIGNORE    ,K(KTn,'$'),KIGNORE   ,KIGNORE    ,K(KTn,'{'),
  K(KTn,'['),K(KTn,']'),K(KTn,'}'),K(KTn,'\\'),KIGNORE   ,KIGNORE   ,KIGNORE    ,K(KTf,21 ),
  K(KTf,20 ),K(KTf,24 ),KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,KIGNORE    ,K(KTl,'q'),
  K(KTl,'w'),K(KTl,'e'),K(KTl,'r'),K(KTl,'t' ),K(KTl,'y'),K(KTl,'u'),K(KTl,'i' ),K(KTl,'o'),
  K(KTl,'p'),KIGNORE   ,K(KTn,'~'),KIGNORE    ,K(KTf,22 ),K(KTf,23 ),K(KTf,25  ),KPADB_7   ,
  KPADB_8   ,KPADB_9   ,KPAD_MI   ,KCTRL      ,K(KAs,20 ),K(KTl,'s'),K(KAs,23  ),K(KAs,25 ),
  K(KTl,'g'),K(KTl,'h'),K(KTl,'j'),K(KTl,'k' ),K(KTl,'l'),KIGNORE   ,KIGNORE    ,KENTER    ,
  KPADB_4   ,KPADB_5   ,KPADB_6   ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KTl,'z' ),K(KTl,'x'),
  K(KAs,22 ),K(KTl,'v'),K(KTl,21 ),K(KTl,'n' ),K(KTl,'m'),KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KSHIFT    ,K(KTc,Kcu),KPADB_1   ,KPADB_2    ,KPADB_3   ,KCAPSLK   ,KALT       ,KIGNORE   ,
  KALTGR    ,KCTRL     ,K(KTc,Kcl),K(KTc,Kcd ),K(KTc,Kcr),KPADB_0   ,KPAD_DT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

u_short ctrl_map[NR_KEYS]=
{
  KIGNORE   ,K(KTf,  0),K(KTf,  1),K(KTf,  2 ),K(KTf,  3),K(KTf,  4),K(KTf, 5  ),K(KTf,  6),
  K(KTf,  7),K(KTf,  8),K(KTf,  9),K(KTf, 10 ),K(KTf, 11),KIGNORE   ,KSTAT      ,KINTR     ,
  KIGNORE   ,K(KTn, 1 ),K(KTn, 2 ),K(KTn, 3  ),K(KTn, 4 ),K(KTn, 5 ),K(KTn, 6  ),K(KTn, 7 ),
  K(KTn, 8 ),K(KTn, 9 ),K(KTn, 0 ),K(KTn,31  ),KIGNORE   ,KIGNORE   ,K(KTn, 8  ),K(KTf,21 ),
  K(KTf,20 ),K(KTf,24 ),KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,KIGNORE    ,K(KTn,17 ),
  K(KTn,23 ),K(KTn, 5 ),K(KTn,18 ),K(KTn,20  ),K(KTn,25 ),K(KTn,21 ),K(KTn, 9  ),K(KTn,15 ),
  K(KTn,16 ),K(KTn,27 ),K(KTn,29 ),K(KTn,28  ),K(KTf,22 ),K(KTf,23 ),K(KTf,25  ),KPAD_7    ,
  KPAD_8    ,KPAD_9    ,KPAD_MI   ,KCTRL      ,K(KTn, 1 ),K(KTn,19 ),K(KTn, 4  ),K(KTn, 6 ),
  K(KTn, 7 ),K(KTn, 8 ),K(KTn,10 ),K(KTn,11  ),K(KTn,12 ),KIGNORE   ,K(KTn, 7  ),KENTER    ,
  KPAD_4    ,KPAD_5    ,KPAD_6    ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KTn,26  ),K(KTn,24 ),
  K(KTn, 3 ),K(KTn,22 ),K(KTn, 2 ),K(KTn,14  ),K(KTn,13 ),KIGNORE   ,KCOMPOSE   ,K(KTn,127),
  KSHIFT    ,K(KTc,Kcu),KPAD_1    ,KPAD_2     ,KPAD_3    ,KCAPSLK   ,KALT       ,K(KTn, 0 ),
  KALTGR    ,KCTRL     ,K(KTc,Kcl),K(KTc,Kcd ),K(KTc,Kcr),KPAD_0    ,KPAD_DT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

u_short shift_ctrl_map[NR_KEYS]=
{
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KFLOPPY    ,KINTR     ,
  KIGNORE   ,KIGNORE   ,K(KTn, 0 ),KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,K(KTn,31  ),KIGNORE   ,KIGNORE   ,KIGNORE    ,K(KTf,21 ),
  K(KTf,20 ),K(KTf,24 ),KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,KIGNORE    ,K(KTn,17 ),
  K(KTn,23 ),K(KTn, 5 ),K(KTn,18 ),K(KTn,20  ),K(KTn,25 ),K(KTn,21 ),K(KTn, 9  ),K(KTn,15 ),
  K(KTn,16 ),KIGNORE   ,KIGNORE   ,KIGNORE    ,K(KTf,22 ),K(KTf,23 ),K(KTf,25  ),KPAD_7    ,
  KPAD_8    ,KPAD_9    ,KPAD_MI   ,KCTRL      ,K(KTn, 1 ),K(KTn,19 ),K(KTn, 4  ),K(KTn, 6 ),
  K(KTn, 7 ),K(KTn, 8 ),K(KTn,10 ),K(KTn,11  ),K(KTn,12 ),KIGNORE   ,K(KTn, 7  ),KENTER    ,
  KPAD_4    ,KPAD_5    ,KPAD_6    ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KTn,26  ),K(KTn,24 ),
  K(KTn, 3 ),K(KTn,22 ),K(KTn, 2 ),K(KTn,14  ),K(KTn,13 ),KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KSHIFT    ,K(KTc,Kcu),KPAD_1    ,KPAD_2     ,KPAD_3    ,KCAPSLK   ,KALT       ,K(KTn, 0 ),
  KALTGR    ,KCTRL     ,K(KTc,Kcl),K(KTc,Kcd ),K(KTc,Kcr),KPAD_0    ,KPAD_DT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

u_short alt_map[NR_KEYS]=
{
  K(KMt,27 ),K(KCn, 0 ),K(KCn, 1 ),K(KCn, 2  ),K(KCn, 3 ),K(KCn, 4 ),K(KCn, 5  ),K(KCn, 6 ),
  K(KCn, 7 ),K(KCn, 8 ),K(KCn, 9 ),K(KCn,10  ),K(KCn,11 ),KIGNORE   ,KSCRLLK    ,KINTR     ,
  K(KMt,'`'),K(KMt,'1'),K(KMt,'2'),K(KMt,'3' ),K(KMt,'4'),K(KMt,'5'),K(KMt,'6' ),K(KMt,'7'),
  K(KMt,'8'),K(KMt,'9'),K(KMt,'0'),K(KMt,'-' ),K(KMt,'='),K(KMt,'£'),K(KMt,127 ),K(KTf,21 ),
  K(KTf,20 ),K(KTf,24 ),KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,K(KMt, 9  ),K(KMt,'q'),
  K(KMt,'w'),K(KMt,'e'),K(KMt,'r'),K(KMt,'t' ),K(KMt,'y'),K(KMt,'u'),K(KMt,'i' ),K(KMt,'o'),
  K(KMt,'p'),K(KMt,'['),K(KMt,']'),K(KMt,'\\'),K(KTf,22 ),K(KTf,23 ),K(KTf,25  ),KPADA_7   ,
  KPADA_8   ,KPADA_9   ,KPAD_MI   ,KCTRL      ,K(KMt,'a'),K(KMt,'s'),K(KMt,'d' ),K(KMt,'f'),
  K(KMt,'g'),K(KMt,'h'),K(KMt,'j'),K(KMt,'k' ),K(KMt,'l'),K(KMt,';'),K(KMt,'\''),K(KMt,13 ),
  KPADA_4   ,KPADA_5   ,KPADA_6   ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KMt,'z' ),K(KMt,'x'),
  K(KMt,'c'),K(KMt,'v'),K(KMt,'b'),K(KMt,'n' ),K(KMt,'m'),K(KMt,','),K(KMt,'.' ),KIGNORE   ,
  KSHIFT    ,K(KTc,Kcu),KPADA_1   ,KPADA_2    ,KPADA_3   ,KCAPSLK   ,KALT       ,K(KMt,' '),
  KALTGR    ,KCTRL     ,CONS_DEC  ,K(KTc,Kcd ),CONS_INC  ,KPADA_0   ,KPAD_DT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

u_short ctrl_alt_map[NR_KEYS]=
{
  KIGNORE   ,K(KCn, 0 ),K(KCn, 1 ),K(KCn, 2  ),K(KCn, 3 ),K(KCn, 4 ),K(KCn, 5  ),K(KCn, 6 ),
  K(KCn, 7 ),K(KCn, 8 ),K(KCn, 9 ),K(KCn,10  ),K(KCn,11 ),KIGNORE   ,KIGNORE    ,KINTR     ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,K(KTf,21 ),
  K(KTf,20 ),K(KTf,24 ),KNUMLK    ,KPAD_DV    ,KPAD_ML   ,KPAD_HS   ,KIGNORE    ,K(KMt,17 ),
  K(KMt,23 ),K(KMt, 5 ),K(KMt,18 ),K(KMt,20  ),K(KMt,25 ),K(KMt,21 ),K(KMt, 9  ),K(KMt,15 ),
  K(KMt,16 ),KIGNORE   ,KIGNORE   ,KIGNORE    ,KREBOOT   ,K(KTf,23 ),K(KTf,25  ),KPAD_7    ,
  KPAD_8    ,KPAD_9    ,KPAD_MI   ,KCTRL      ,K(KMt, 1 ),K(KMt,19 ),K(KMt, 4  ),K(KMt, 6 ),
  K(KMt, 7 ),K(KMt, 8 ),K(KMt,10 ),K(KMt,11  ),K(KMt,12 ),KIGNORE   ,KIGNORE    ,KENTER    ,
  KPAD_4    ,KPAD_5    ,KPAD_6    ,KPAD_PL    ,KSHIFT    ,KIGNORE   ,K(KMt,26  ),K(KMt,24 ),
  K(KMt, 3 ),K(KMt,22 ),K(KMt, 2 ),K(KMt,14  ),K(KMt,13 ),KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KSHIFT    ,K(KTc,Kcu),KPAD_1    ,KPAD_2     ,KPAD_3    ,KCAPSLK   ,KALT       ,KIGNORE   ,
  KALTGR    ,KCTRL     ,K(KTc,Kcl),K(KTc,Kcd ),K(KTc,Kcr),KPAD_0    ,KREBOOT    ,KPAD_EN   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
  KIGNORE   ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,KIGNORE   ,KIGNORE    ,KIGNORE   ,
};

ushort *key_maps[MAX_NR_KEYMAPS] = {
	plain_map, shift_map, altgr_map, 0,
	ctrl_map, shift_ctrl_map, 0, 0,
	alt_map, 0, 0, 0,
	ctrl_alt_map,	0
};

unsigned int keymap_count = 7;

/*
 * Philosophy: most people do not define more strings, but they who do
 * often want quite a lot of string space. So, we statically allocate
 * the default and allocate dynamically in chunks of 512 bytes.
 */

char func_buf[] = {
	'\033', '[', '[', 'A', 0,
	'\033', '[', '[', 'B', 0,
	'\033', '[', '[', 'C', 0,
	'\033', '[', '[', 'D', 0,
	'\033', '[', '[', 'E', 0,
	'\033', '[', '1', '7', '~', 0,
	'\033', '[', '1', '8', '~', 0,
	'\033', '[', '1', '9', '~', 0,
	'\033', '[', '2', '0', '~', 0,
	'\033', '[', '2', '1', '~', 0,
	'\033', '[', '2', '3', '~', 0,
	'\033', '[', '2', '4', '~', 0,
	'\033', '[', '2', '5', '~', 0,
	'\033', '[', '2', '6', '~', 0,
	'\033', '[', '2', '8', '~', 0,
	'\033', '[', '2', '9', '~', 0,
	'\033', '[', '3', '1', '~', 0,
	'\033', '[', '3', '2', '~', 0,
	'\033', '[', '3', '3', '~', 0,
	'\033', '[', '3', '4', '~', 0,
	'\033', '[', '1', '~', 0,
	'\033', '[', '2', '~', 0,
	'\033', '[', '3', '~', 0,
	'\033', '[', '4', '~', 0,
	'\033', '[', '5', '~', 0,
	'\033', '[', '6', '~', 0,
	'\033', '[', 'M', 0,
	'\033', '[', 'P', 0,
};

char *funcbufptr = func_buf;
int funcbufsize = sizeof(func_buf);
int funcbufleft;          /* space left */

char *func_table[MAX_NR_FUNC] = {
	func_buf + 0,
	func_buf + 5,
	func_buf + 10,
	func_buf + 15,
	func_buf + 20,
	func_buf + 25,
	func_buf + 31,
	func_buf + 37,
	func_buf + 43,
	func_buf + 49,
	func_buf + 55,
	func_buf + 61,
	func_buf + 67,
	func_buf + 73,
	func_buf + 79,
	func_buf + 85,
	func_buf + 91,
	func_buf + 97,
	func_buf + 103,
	func_buf + 109,
	func_buf + 115,
	func_buf + 120,
	func_buf + 125,
	func_buf + 130,
	func_buf + 135,
	func_buf + 140,
	func_buf + 145,
	0,
	0,
	func_buf + 149,
	0,
};

struct kbdiacr accent_table[MAX_DIACR] = {
	{'`', 'A', '\300'},	{'`', 'a', '\340'},
	{'\'', 'A', '\301'},	{'\'', 'a', '\341'},
	{'^', 'A', '\302'},	{'^', 'a', '\342'},
	{'~', 'A', '\303'},	{'~', 'a', '\343'},
	{'"', 'A', '\304'},	{'"', 'a', '\344'},
	{'O', 'A', '\305'},	{'o', 'a', '\345'},
	{'0', 'A', '\305'},	{'0', 'a', '\345'},
	{'A', 'A', '\305'},	{'a', 'a', '\345'},
	{'A', 'E', '\306'},	{'a', 'e', '\346'},
	{',', 'C', '\307'},	{',', 'c', '\347'},
	{'`', 'E', '\310'},	{'`', 'e', '\350'},
	{'\'', 'E', '\311'},	{'\'', 'e', '\351'},
	{'^', 'E', '\312'},	{'^', 'e', '\352'},
	{'"', 'E', '\313'},	{'"', 'e', '\353'},
	{'`', 'I', '\314'},	{'`', 'i', '\354'},
	{'\'', 'I', '\315'},	{'\'', 'i', '\355'},
	{'^', 'I', '\316'},	{'^', 'i', '\356'},
	{'"', 'I', '\317'},	{'"', 'i', '\357'},
	{'-', 'D', '\320'},	{'-', 'd', '\360'},
	{'~', 'N', '\321'},	{'~', 'n', '\361'},
	{'`', 'O', '\322'},	{'`', 'o', '\362'},
	{'\'', 'O', '\323'},	{'\'', 'o', '\363'},
	{'^', 'O', '\324'},	{'^', 'o', '\364'},
	{'~', 'O', '\325'},	{'~', 'o', '\365'},
	{'"', 'O', '\326'},	{'"', 'o', '\366'},
	{'/', 'O', '\330'},	{'/', 'o', '\370'},
	{'`', 'U', '\331'},	{'`', 'u', '\371'},
	{'\'', 'U', '\332'},	{'\'', 'u', '\372'},
	{'^', 'U', '\333'},	{'^', 'u', '\373'},
	{'"', 'U', '\334'},	{'"', 'u', '\374'},
	{'\'', 'Y', '\335'},	{'\'', 'y', '\375'},
	{'T', 'H', '\336'},	{'t', 'h', '\376'},
	{'s', 's', '\337'},	{'"', 'y', '\377'},
	{'s', 'z', '\337'},	{'i', 'j', '\377'},
};

unsigned int accent_table_size = 68;
