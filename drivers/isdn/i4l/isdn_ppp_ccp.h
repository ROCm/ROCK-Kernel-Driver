
#include <linux/kernel.h>
#include <linux/isdn_ppp.h>

struct ippp_ccp {
	int proto;
	struct isdn_ppp_compressor *compressor;
	struct isdn_ppp_compressor *decompressor;
	void                       *comp_stat;
	void                       *decomp_stat;
	unsigned long               compflags;
	struct ippp_ccp_reset      *reset;
	int                         mru;
	int                         debug;
	void                       *priv;
	void (*xmit_reset)(void *priv, int proto, unsigned char code,
			   unsigned char id, unsigned char *data, int len);
	void (*kick_up)(void *priv, unsigned int flags);
};

struct ippp_ccp *
ippp_ccp_alloc(int proto, void *priv,
	       void (*xmit_reset)(void *priv, int proto, unsigned char code,
				  unsigned char id, unsigned char *data, 
				  int len),
	       void (*kick_up)(void *priv, unsigned int flags));

void
ippp_ccp_free(struct ippp_ccp *ccp);

int
ippp_ccp_set_mru(struct ippp_ccp *ccp, unsigned int mru);

struct sk_buff *
ippp_ccp_compress(struct ippp_ccp *ccp, struct sk_buff *skb, int *proto);

struct sk_buff *
ippp_ccp_decompress(struct ippp_ccp *ccp, struct sk_buff *skb, int *proto);

void
ippp_ccp_send_ccp(struct ippp_ccp *ccp, struct sk_buff *skb);

void
ippp_ccp_receive_ccp(struct ippp_ccp *ccp, struct sk_buff *skb);

void
ippp_ccp_get_compressors(unsigned long protos[8]);

int
ippp_ccp_set_compressor(struct ippp_ccp *ccp, int unit,
			struct isdn_ppp_comp_data *data);


