/*
 *  IBM/3270 Driver -- Copyright (C) 2000 UTS Global LLC
 *
 *  tubttybld.c -- Linemode tty driver screen-building functions
 *
 *
 *
 *
 *
 *  Author:  Richard Hitt
 */

#include "tubio.h"

extern int tty3270_io(tub_t *);
static void tty3270_set_status_area(tub_t *, char **);
static int tty3270_next_char(tub_t *);
static void tty3270_update_log_area(tub_t *, char **);
static void tty3270_update_log_area_esc(tub_t *, char **);
static void tty3270_clear_log_area(tub_t *, char **);
static void tty3270_tub_bufadr(tub_t *, int, char **);

/*
 * tty3270_clear_log_area(tub_t *tubp, char **cpp)
 */
static void
tty3270_clear_log_area(tub_t *tubp, char **cpp)
{
	*(*cpp)++ = TO_SBA;
	TUB_BUFADR(GEOM_LOG, cpp);
	*(*cpp)++ = TO_SF;
	*(*cpp)++ = TF_LOG;
	*(*cpp)++ = TO_RA;
	TUB_BUFADR(GEOM_INPUT, cpp);
	*(*cpp)++ = '\0';
	tubp->tty_oucol = tubp->tty_nextlogx = 0;
	*(*cpp)++ = TO_SBA;
	TUB_BUFADR(tubp->tty_nextlogx, cpp);
}

static void
tty3270_update_log_area(tub_t *tubp, char **cpp)
{
	int lastx = GEOM_INPUT;
	int c;
	int next, fill, i;
	int sba_needed = 1;
	char *overrun = &(*tubp->ttyscreen)[tubp->ttyscreenl - TS_LENGTH];

	/* Place characters */
	while (tubp->tty_bcb.bc_cnt != 0) {
		if (tubp->tty_nextlogx >= lastx) {
			if (sba_needed == 0 || tubp->stat == TBS_RUNNING) {
				tubp->stat = TBS_MORE;
				tty3270_set_status_area(tubp, cpp);
				tty3270_scl_settimer(tubp);
			}
			break;
		}

		/* Check for room for another char + possible ESCs */
		if (&(*cpp)[tubp->tty_escx + 1] >= overrun)
			break;

		/* Fetch a character */
		if ((c = tty3270_next_char(tubp)) == -1)
			break;

		/* Add a Set-Buffer-Address Order if we haven't */
		if (sba_needed) {
			sba_needed = 0;
			*(*cpp)++ = TO_SBA;
			TUB_BUFADR(tubp->tty_nextlogx, cpp);
		}

		switch(c) {
		default:
			if (c < ' ')    /* Blank it if we don't know it */
				c = ' ';
			for (i = 0; i < tubp->tty_escx; i++)
				*(*cpp)++ = tubp->tty_esca[i];
			tubp->tty_escx = 0;
			*(*cpp)++ = tub_ascebc[(int)c];
			tubp->tty_nextlogx++;
			tubp->tty_oucol++;
			break;
		case 0x1b:              /* ESC */
			tty3270_update_log_area_esc(tubp, cpp);
			break;
		case '\r':
			break;          /* completely ignore 0x0d = CR. */
		case '\n':
			if (tubp->tty_oucol == GEOM_COLS) {
				tubp->tty_oucol = 0;
				break;
			}
			next = (tubp->tty_nextlogx + GEOM_COLS) /
				GEOM_COLS * GEOM_COLS;
			next = MIN(next, lastx);
			fill = next - tubp->tty_nextlogx;
			if (fill < 5) {
				for (i = 0; i < fill; i++)
					*(*cpp)++ = tub_ascebc[' '];
			} else {
				*(*cpp)++ = TO_RA;
				TUB_BUFADR(next, cpp);
				*(*cpp)++ = tub_ascebc[' '];
			}
			tubp->tty_nextlogx = next;
			tubp->tty_oucol = 0;
			break;
		case '\t':
			fill = (tubp->tty_nextlogx % GEOM_COLS) % 8;
			for (; fill < 8; fill++) {
				if (tubp->tty_nextlogx >= lastx)
					break;
				*(*cpp)++ = tub_ascebc[' '];
				tubp->tty_nextlogx++;
				tubp->tty_oucol++;
			}
			break;
		case '\a':
			tubp->flags |= TUB_ALARM;
			break;
		case '\f':
			tty3270_clear_log_area(tubp, cpp);
			break;
		}
	}
}

#define NUMQUANT 8
static void
tty3270_update_log_area_esc(tub_t *tubp, char **cpp)
{
	int lastx = GEOM_INPUT;
	int c;
	int i;
	int start, next, fill;
	int quant[NUMQUANT];

	if ((c = tty3270_next_char(tubp)) != '[') {
		return;
	}

	/*
	 * Parse potentially empty string "nn;nn;nn..."
	 */
	i = -1;
	c = ';';
	do {
		if (c == ';') {
			if (++i == NUMQUANT)
				break;
			quant[i] = 0;
		} else if (c < '0' || c > '9') {
			break;
		} else {
			quant[i] = quant[i] * 10 + c - '0';
		}
	} while ((c = tty3270_next_char(tubp)) != -1);
	if (c == -1) {
		return;
	}
	if (i >= NUMQUANT) {
		return;
	}
	switch(c) {
	case -1:
		return;
	case 'm':		/* Set Attribute */
		for (next = 0; next <= i; next++) {
			int type = -1, value = 0;

			if (tubp->tty_escx + 3 > MAX_TTY_ESCA)
				break;
			switch(quant[next]) {
			case 0:		/* Reset */
				tubp->tty_esca[tubp->tty_escx++] = TO_SA;
				tubp->tty_esca[tubp->tty_escx++] = TAT_RESET;
				tubp->tty_esca[tubp->tty_escx++] = TAR_RESET;
				break;
			case 1:		/* Bright */
			case 2:		/* Dim */
			case 4:		/* Underscore */
			case 5:		/* Blink */
			case 7:		/* Reverse */
			case 8:		/* Hidden */
				break;		/* For now ... */
			/* Foreground Colors */
			case 30:	/* Black */
				type = TAT_COLOR; value = TAC_DEFAULT;
				break;
			case 31:	/* Red */
				type = TAT_COLOR; value = TAC_RED;
				break;
			case 32:	/* Green */
				type = TAT_COLOR; value = TAC_GREEN;
				break;
			case 33:	/* Yellow */
				type = TAT_COLOR; value = TAC_YELLOW;
				break;
			case 34:	/* Blue */
				type = TAT_COLOR; value = TAC_BLUE;
				break;
			case 35:	/* Magenta */
				type = TAT_COLOR; value = TAC_PINK;
				break;
			case 36:	/* Cyan */
				type = TAT_COLOR; value = TAC_TURQ;
				break;
			case 37:	/* White */
				type = TAT_COLOR; value = TAC_WHITE;
				break;
			case 39:	/* Black */
				type = TAT_COLOR; value = TAC_DEFAULT;
				break;
			/* Background Colors */
			case 40:	/* Black */
			case 41:	/* Red */
			case 42:	/* Green */
			case 43:	/* Yellow */
			case 44:	/* Blue */
			case 45:	/* Magenta */
			case 46:	/* Cyan */
			case 47:	/* White */
				break;		/* For now ... */
			/* Oops */
			default:
				break;
			}
			if (type != -1) {
				tubp->tty_esca[tubp->tty_escx++] = TO_SA;
				tubp->tty_esca[tubp->tty_escx++] = type;
				tubp->tty_esca[tubp->tty_escx++] = value;
			}
		}
		break;
	case 'H':		/* Cursor Home */
	case 'f':		/* Force Cursor Position */
		return;
	case 'A':		/* Cursor Up */
		return;
	case 'B':		/* Cursor Down */
		return;
	case 'C':		/* Cursor Forward */
		next = tubp->tty_nextlogx % GEOM_COLS;
		start = tubp->tty_nextlogx - next;
		next = start + MIN(next + quant[i], GEOM_COLS - 1);
		next = MIN(next, lastx);
do_fill:
		fill = next - tubp->tty_nextlogx;
		if (fill < 5) {
			for (i = 0; i < fill; i++)
				*(*cpp)++ = tub_ascebc[' '];
		} else {
			*(*cpp)++ = TO_RA;
			TUB_BUFADR(next, cpp);
			*(*cpp)++ = tub_ascebc[' '];
		}
		tubp->tty_nextlogx = next;
		tubp->tty_oucol = tubp->tty_nextlogx % GEOM_COLS;
		break;
	case 'D':		/* Cursor Backward */
		next = MIN(quant[i], tubp->tty_nextlogx % GEOM_COLS);
		tubp->tty_nextlogx -= next;
		tubp->tty_oucol = tubp->tty_nextlogx % GEOM_COLS;
		*(*cpp)++ = TO_SBA;
		TUB_BUFADR(tubp->tty_nextlogx, cpp);
		break;
	case 'G':
		start = tubp->tty_nextlogx / GEOM_COLS * GEOM_COLS;
		next = MIN(quant[i], GEOM_COLS - 1) + start;
		next = MIN(next, lastx);
		goto do_fill;
	}
}


static int
tty3270_next_char(tub_t *tubp)
{
	int c;
	bcb_t *ib;

	ib = &tubp->tty_bcb;
	if (ib->bc_cnt == 0)
		return -1;
	c = ib->bc_buf[ib->bc_rd++];
	if (ib->bc_rd == ib->bc_len)
		ib->bc_rd = 0;
	ib->bc_cnt--;
	return c;
}


static void
tty3270_clear_input_area(tub_t *tubp, char **cpp)
{
	*(*cpp)++ = TO_SBA;
	TUB_BUFADR(GEOM_INPUT, cpp);
	*(*cpp)++ = TO_SF;
	*(*cpp)++ = tubp->tty_inattr;
	*(*cpp)++ = TO_IC;
	*(*cpp)++ = TO_RA;
	TUB_BUFADR(GEOM_STAT, cpp);
	*(*cpp)++ = '\0';
}

static void
tty3270_update_input_area(tub_t *tubp, char **cpp)
{
	int len;

	*(*cpp)++ = TO_SBA;
	TUB_BUFADR(GEOM_INPUT, cpp);
	*(*cpp)++ = TO_SF;
	*(*cpp)++ = TF_INMDT;
	len = strlen(tubp->tty_input);
	memcpy(*cpp, tubp->tty_input, len);
	*cpp += len;
	*(*cpp)++ = TO_IC;
	len = GEOM_INPLEN - len;
	if (len > 4) {
		*(*cpp)++ = TO_RA;
		TUB_BUFADR(GEOM_STAT, cpp);
		*(*cpp)++ = '\0';
	} else {
		for (; len > 0; len--)
			*(*cpp)++ = '\0';
	}
}

/*
 * tty3270_set_status_area(tub_t *tubp, char **cpp)
 */
static void
tty3270_set_status_area(tub_t *tubp, char **cpp)
{
	char *sp;

	if (tubp->stat == TBS_RUNNING)
		sp = TS_RUNNING;
	else if (tubp->stat == TBS_MORE)
		sp = TS_MORE;
	else if (tubp->stat == TBS_HOLD)
		sp = TS_HOLD;
	else
		sp = "Linux Whatstat";

	*(*cpp)++ = TO_SBA;
	TUB_BUFADR(GEOM_STAT, cpp);
	*(*cpp)++ = TO_SF;
	*(*cpp)++ = TF_STAT;
	memcpy(*cpp, sp, sizeof TS_RUNNING);
	TUB_ASCEBC(*cpp, sizeof TS_RUNNING);
	*cpp += sizeof TS_RUNNING;
}

/*
 * tty3270_build() -- build an output stream
 */
int
tty3270_build(tub_t *tubp)
{
	char *cp, *startcp;
	int chancmd;
	int writecc = TW_KR;
	int force = 0;

	if (tubp->mode == TBM_FS)
		return 0;

	cp = startcp = *tubp->ttyscreen + 1;

	switch(tubp->cmd) {
	default:
		printk(KERN_WARNING "tty3270_build unknown command %d\n", tubp->cmd);
		return 0;
	case TBC_OPEN:
tbc_open:
		tubp->flags &= ~TUB_INPUT_HACK;
		chancmd = TC_EWRITEA;
		tty3270_clear_input_area(tubp, &cp);
		tty3270_set_status_area(tubp, &cp);
		tty3270_clear_log_area(tubp, &cp);
		break;
	case TBC_UPDLOG:
		if (tubp->flags & TUB_INPUT_HACK)
			goto tbc_open;
		chancmd = TC_WRITE;
		writecc = TW_NONE;
		tty3270_update_log_area(tubp, &cp);
		break;
	case TBC_KRUPDLOG:
		chancmd = TC_WRITE;
		force = 1;
		tty3270_update_log_area(tubp, &cp);
		break;
	case TBC_CLRUPDLOG:
		chancmd = TC_WRITE;
		tty3270_set_status_area(tubp, &cp);
		tty3270_clear_log_area(tubp, &cp);
		tty3270_update_log_area(tubp, &cp);
		break;
	case TBC_UPDATE:
		chancmd = TC_EWRITEA;
		tubp->tty_oucol = tubp->tty_nextlogx = 0;
		tty3270_clear_input_area(tubp, &cp);
		tty3270_set_status_area(tubp, &cp);
		tty3270_update_log_area(tubp, &cp);
		break;
	case TBC_UPDSTAT:
		chancmd = TC_WRITE;
		tty3270_set_status_area(tubp, &cp);
		break;
	case TBC_CLRINPUT:
		chancmd = TC_WRITE;
		tty3270_clear_input_area(tubp, &cp);
		break;
	case TBC_UPDINPUT:
		chancmd = TC_WRITE;
		tty3270_update_input_area(tubp, &cp);
		break;
	}

	/* Set Write Control Character and start I/O */
	if (force == 0 && cp == startcp &&
	    (tubp->flags & TUB_ALARM) == 0)
		return 0;
	if (tubp->flags & TUB_ALARM) {
		tubp->flags &= ~TUB_ALARM;
		writecc |= TW_PLUSALARM;
	}
	**tubp->ttyscreen = writecc;
	tubp->ttyccw.cmd_code = chancmd;
	tubp->ttyccw.flags = CCW_FLAG_SLI;
	tubp->ttyccw.cda = virt_to_phys(*tubp->ttyscreen);
	tubp->ttyccw.count = cp - *tubp->ttyscreen;
	tty3270_io(tubp);
	return 1;
}

static void
tty3270_tub_bufadr(tub_t *tubp, int adr, char **cpp)
{
	if (tubp->tty_14bitadr) {
		*(*cpp)++ = (adr >> 8) & 0x3f;
		*(*cpp)++ = adr & 0xff;
	} else {
		*(*cpp)++ = tub_ebcgraf[(adr >> 6) & 0x3f];
		*(*cpp)++ = tub_ebcgraf[adr & 0x3f];
	}
}
