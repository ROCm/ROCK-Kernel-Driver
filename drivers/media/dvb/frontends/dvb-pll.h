/*
 * $Id: dvb-pll.h,v 1.1 2004/12/09 12:51:35 kraxel Exp $
 */

struct dvb_pll_desc {
	char *name;
	u32  min;
	u32  max;
	void (*setbw)(u8 *buf, int bandwidth);
	int  count;
	struct {
		u32 limit;
		u32 offset;
		u32 stepsize;
		u8  cb1;
		u8  cb2;
	} entries[];
};

extern struct dvb_pll_desc dvb_pll_thomson_dtt7579;
extern struct dvb_pll_desc dvb_pll_thomson_dtt759x;
extern struct dvb_pll_desc dvb_pll_lg_z201;
extern struct dvb_pll_desc dvb_pll_unknown_1;

int dvb_pll_configure(struct dvb_pll_desc *desc, u8 *buf,
		      u32 freq, int bandwidth);

/*
 * Local variables:
 * c-basic-offset: 8
 * compile-command: "make DVB=1"
 * End:
 */
