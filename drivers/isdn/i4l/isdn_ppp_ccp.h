/* Linux ISDN subsystem, PPP CCP support
 *
 * Copyright 1994-1998  by Fritz Elfert (fritz@isdn4linux.de)
 *           1995,96    by Thinking Objects Software GmbH Wuerzburg
 *           1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 *           1999-2002  by Kai Germaschewski <kai@germaschewski.name>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/kernel.h>
#include <linux/isdn_ppp.h>

/* for ippp_ccp::flags */

#define SC_DECOMP_ON		0x01
#define SC_COMP_ON		0x02
#define SC_DECOMP_DISCARD	0x04
#define SC_COMP_DISCARD		0x08

/* SC_DC_ERROR/FERROR go in here as well, but are defined elsewhere

   #define SC_DC_FERROR	0x00800000
   #define SC_DC_ERROR	0x00400000
*/

struct ippp_ccp {
	u16                         proto;
	struct isdn_ppp_compressor *compressor;
	struct isdn_ppp_compressor *decompressor;
	void                       *comp_stat;
	void                       *decomp_stat;
	unsigned long               compflags;
	struct ippp_ccp_reset      *reset;
	int                         mru;
	int                         debug;
	void                       *priv;
	void            (*xmit)(void *priv, struct sk_buff *skb, u16 proto);
	void            (*kick_up)(void *priv);
	struct sk_buff *(*alloc_skb)(void *priv, int len, int gfp_mask);
};

struct ippp_ccp *
ippp_ccp_alloc(void);

void
ippp_ccp_free(struct ippp_ccp *ccp);

int
ippp_ccp_set_mru(struct ippp_ccp *ccp, unsigned int mru);

unsigned int
ippp_ccp_get_flags(struct ippp_ccp *ccp);

struct sk_buff *
ippp_ccp_compress(struct ippp_ccp *ccp, struct sk_buff *skb, u16 *proto);

struct sk_buff *
ippp_ccp_decompress(struct ippp_ccp *ccp, struct sk_buff *skb, u16 *proto);

void
ippp_ccp_send_ccp(struct ippp_ccp *ccp, struct sk_buff *skb);

void
ippp_ccp_receive_ccp(struct ippp_ccp *ccp, struct sk_buff *skb);

void
ippp_ccp_get_compressors(unsigned long protos[8]);

int
ippp_ccp_set_compressor(struct ippp_ccp *ccp, int unit,
			struct isdn_ppp_comp_data *data);


