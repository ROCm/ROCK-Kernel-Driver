#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/err.h>

#define RPCDBG_FACILITY	RPCDBG_AUTH


/*
 * AUTHUNIX and AUTHNULL credentials are both handled here.
 * AUTHNULL is treated just like AUTHUNIX except that the uid/gid
 * are always nobody (-2).  i.e. we do the same IP address checks for
 * AUTHNULL as for AUTHUNIX, and that is done here.
 */


char *strdup(char *s)
{
	char *rv = kmalloc(strlen(s)+1, GFP_KERNEL);
	if (rv)
		strcpy(rv, s);
	return rv;
}

struct unix_domain {
	struct auth_domain	h;
	int	addr_changes;
	/* other stuff later */
};

struct auth_domain *unix_domain_find(char *name)
{
	struct auth_domain *rv, ud;
	struct unix_domain *new;

	ud.name = name;
	
	rv = auth_domain_lookup(&ud, 0);

 foundit:
	if (rv && rv->flavour != RPC_AUTH_UNIX) {
		auth_domain_put(rv);
		return NULL;
	}
	if (rv)
		return rv;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	cache_init(&new->h.h);
	atomic_inc(&new->h.h.refcnt);
	new->h.name = strdup(name);
	new->h.flavour = RPC_AUTH_UNIX;
	new->addr_changes = 0;
	new->h.h.expiry_time = NEVER;
	new->h.h.flags = 0;

	rv = auth_domain_lookup(&new->h, 2);
	if (rv == &new->h) {
		if (atomic_dec_and_test(&new->h.h.refcnt)) BUG();
	} else {
		auth_domain_put(&new->h);
		goto foundit;
	}

	return rv;
}


/**************************************************
 * cache for IP address to unix_domain
 * as needed by AUTH_UNIX
 */
#define	IP_HASHBITS	8
#define	IP_HASHMAX	(1<<IP_HASHBITS)
#define	IP_HASHMASK	(IP_HASHMAX-1)

struct ip_map {
	struct cache_head	h;
	char			*m_class; /* e.g. "nfsd" */
	struct in_addr		m_addr;
	struct unix_domain	*m_client;
	int			m_add_change;
};
static struct cache_head	*ip_table[IP_HASHMAX];

void ip_map_put(struct cache_head *item, struct cache_detail *cd)
{
	struct ip_map *im = container_of(item, struct ip_map,h);
	if (cache_put(item, cd)) {
		if (test_bit(CACHE_VALID, &item->flags) &&
		    !test_bit(CACHE_NEGATIVE, &item->flags))
			auth_domain_put(&im->m_client->h);
		kfree(im);
	}
}

static inline int ip_map_hash(struct ip_map *item)
{
	return (name_hash(item->m_class, IP_HASHMAX) ^ item->m_addr.s_addr) & IP_HASHMASK;
}
static inline int ip_map_match(struct ip_map *item, struct ip_map *tmp)
{
	return strcmp(tmp->m_class, item->m_class) == 0
		&& tmp->m_addr.s_addr == item->m_addr.s_addr;
}
static inline void ip_map_init(struct ip_map *new, struct ip_map *item)
{
	new->m_class = strdup(item->m_class);
	new->m_addr.s_addr = item->m_addr.s_addr;
}
static inline void ip_map_update(struct ip_map *new, struct ip_map *item)
{
	cache_get(&item->m_client->h.h);
	new->m_client = item->m_client;
	new->m_add_change = item->m_add_change;
}

static void ip_map_request(struct cache_detail *cd,
				  struct cache_head *h,
				  char **bpp, int *blen)
{
	char text_addr[20];
	struct ip_map *im = container_of(h, struct ip_map, h);
	__u32 addr = im->m_addr.s_addr;
	
	snprintf(text_addr, 20, "%u.%u.%u.%u",
		 ntohl(addr) >> 24 & 0xff,
		 ntohl(addr) >> 16 & 0xff,
		 ntohl(addr) >>  8 & 0xff,
		 ntohl(addr) >>  0 & 0xff);

	add_word(bpp, blen, im->m_class);
	add_word(bpp, blen, text_addr);
	(*bpp)[-1] = '\n';
}

static struct ip_map *ip_map_lookup(struct ip_map *, int);
static int ip_map_parse(struct cache_detail *cd,
			  char *mesg, int mlen)
{
	/* class ipaddress [domainname] */
	char class[50], buf[50];
	int len;
	int b1,b2,b3,b4;
	char c;
	struct ip_map ipm, *ipmp;
	struct auth_domain *dom;
	time_t expiry;

	if (mesg[mlen-1] != '\n')
		return -EINVAL;
	mesg[mlen-1] = 0;

	/* class */
	len = get_word(&mesg, class, 50);
	if (len <= 0) return -EINVAL;

	/* ip address */
	len = get_word(&mesg, buf, 50);
	if (len <= 0) return -EINVAL;

	if (sscanf(buf, "%u.%u.%u.%u%c", &b1, &b2, &b3, &b4, &c) != 4)
		return -EINVAL;
	
	expiry = get_expiry(&mesg);
	if (expiry ==0)
		return -EINVAL;

	/* domainname, or empty for NEGATIVE */
	len = get_word(&mesg, buf, 50);
	if (len < 0) return -EINVAL;

	if (len) {
		dom = unix_domain_find(buf);
		if (dom == NULL)
			return -ENOENT;
	} else
		dom = NULL;

	ipm.m_class = class;
	ipm.m_addr.s_addr =
		htonl((((((b1<<8)|b2)<<8)|b3)<<8)|b4);
	ipm.h.flags = 0;
	if (dom)
		ipm.m_client = container_of(dom, struct unix_domain, h);
	else
		set_bit(CACHE_NEGATIVE, &ipm.h.flags);
	ipm.h.expiry_time = expiry;
	ipm.m_add_change = ipm.m_client->addr_changes;

	ipmp = ip_map_lookup(&ipm, 1);
	if (ipmp)
		ip_map_put(&ipmp->h, &ip_map_cache);
	if (dom)
		auth_domain_put(dom);
	if (!ipmp)
		return -ENOMEM;

	return 0;
}

struct cache_detail ip_map_cache = {
	.hash_size	= IP_HASHMAX,
	.hash_table	= ip_table,
	.name		= "auth.unix.ip",
	.cache_put	= ip_map_put,
	.cache_request	= ip_map_request,
	.cache_parse	= ip_map_parse,
};

static DefineSimpleCacheLookup(ip_map)


int auth_unix_add_addr(struct in_addr addr, struct auth_domain *dom)
{
	struct unix_domain *udom;
	struct ip_map ip, *ipmp;

	if (dom->flavour != RPC_AUTH_UNIX)
		return -EINVAL;
	udom = container_of(dom, struct unix_domain, h);
	ip.m_class = "nfsd";
	ip.m_addr = addr;
	ip.m_client = udom;
	ip.m_add_change = udom->addr_changes+1;
	ip.h.flags = 0;
	ip.h.expiry_time = NEVER;
	
	ipmp = ip_map_lookup(&ip, 1);
	if (ipmp) {
		ip_map_put(&ipmp->h, &ip_map_cache);
		return 0;
	} else
		return -ENOMEM;
}

int auth_unix_forget_old(struct auth_domain *dom)
{
	struct unix_domain *udom;
	
	if (dom->flavour != RPC_AUTH_UNIX)
		return -EINVAL;
	udom = container_of(dom, struct unix_domain, h);
	udom->addr_changes++;
	return 0;
}

struct auth_domain *auth_unix_lookup(struct in_addr addr)
{
	struct ip_map key, *ipm;
	struct auth_domain *rv;

	key.m_class = "nfsd";
	key.m_addr = addr;

	ipm = ip_map_lookup(&key, 0);

	if (!ipm)
		return NULL;

	if (test_bit(CACHE_VALID, &ipm->h.flags) &&
	    (ipm->m_client->addr_changes - ipm->m_add_change) >0)
		set_bit(CACHE_NEGATIVE, &ipm->h.flags);

	if (!test_bit(CACHE_VALID, &ipm->h.flags))
		rv = NULL;
	else if (test_bit(CACHE_NEGATIVE, &ipm->h.flags))
		rv = NULL;
	else {
		rv = &ipm->m_client->h;
		cache_get(&rv->h);
	}
	if (ipm) ip_map_put(&ipm->h, &ip_map_cache);
	return rv;
}

void svcauth_unix_purge(void)
{
	cache_purge(&ip_map_cache);
	cache_purge(&auth_domain_cache);
}


static int
svcauth_null_accept(struct svc_rqst *rqstp, u32 *authp, int proc)
{
	struct svc_buf	*argp = &rqstp->rq_argbuf;
	struct svc_buf	*resp = &rqstp->rq_resbuf;
	int		rv=0;
	struct ip_map key, *ipm;

	if ((argp->len -= 3) < 0) {
		return SVC_GARBAGE;
	}
	if (*(argp->buf)++ != 0) {	/* we already skipped the flavor */
		dprintk("svc: bad null cred\n");
		*authp = rpc_autherr_badcred;
		return SVC_DENIED;
	}
	if (*(argp->buf)++ != RPC_AUTH_NULL || *(argp->buf)++ != 0) {
		dprintk("svc: bad null verf\n");
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	/* Signal that mapping to nobody uid/gid is required */
	rqstp->rq_cred.cr_uid = (uid_t) -1;
	rqstp->rq_cred.cr_gid = (gid_t) -1;
	rqstp->rq_cred.cr_groups[0] = NOGROUP;

	/* Put NULL verifier */
	svc_putu32(resp, RPC_AUTH_NULL);
	svc_putu32(resp, 0);

	key.m_class = rqstp->rq_server->sv_program->pg_class;
	key.m_addr = rqstp->rq_addr.sin_addr;

	ipm = ip_map_lookup(&key, 0);

	rqstp->rq_client = NULL;

	if (ipm)
		switch (cache_check(&ip_map_cache, &ipm->h, &rqstp->rq_chandle)) {
		case -EAGAIN:
			rv = SVC_DROP;
			break;
		case -ENOENT:
			rv = SVC_OK; /* rq_client is NULL */
			break;
		case 0:
			rqstp->rq_client = &ipm->m_client->h;
			cache_get(&rqstp->rq_client->h);
			ip_map_put(&ipm->h, &ip_map_cache);
			rv = SVC_OK;
			break;
		default: BUG();
		}
	else rv = SVC_DROP;

	if (rqstp->rq_client == NULL && proc != 0)
		*authp = rpc_autherr_badcred;

	return rv;
}

static int
svcauth_null_release(struct svc_rqst *rqstp)
{
	if (rqstp->rq_client)
		auth_domain_put(rqstp->rq_client);
	rqstp->rq_client = NULL;

	return 0; /* don't drop */
}


struct auth_ops svcauth_null = {
	.name		= "null",
	.flavour	= RPC_AUTH_NULL,
	.accept 	= svcauth_null_accept,
	.release	= svcauth_null_release,
};


int
svcauth_unix_accept(struct svc_rqst *rqstp, u32 *authp, int proc)
{
	struct svc_buf	*argp = &rqstp->rq_argbuf;
	struct svc_buf	*resp = &rqstp->rq_resbuf;
	struct svc_cred	*cred = &rqstp->rq_cred;
	u32		*bufp = argp->buf, slen, i;
	int		len   = argp->len;
	int		rv=0;
	struct ip_map key, *ipm;

	if ((len -= 3) < 0)
		return SVC_GARBAGE;

	bufp++;					/* length */
	bufp++;					/* time stamp */
	slen = XDR_QUADLEN(ntohl(*bufp++));	/* machname length */
	if (slen > 64 || (len -= slen + 3) < 0)
		goto badcred;
	bufp += slen;				/* skip machname */

	cred->cr_uid = ntohl(*bufp++);		/* uid */
	cred->cr_gid = ntohl(*bufp++);		/* gid */

	slen = ntohl(*bufp++);			/* gids length */
	if (slen > 16 || (len -= slen + 2) < 0)
		goto badcred;
	for (i = 0; i < NGROUPS && i < slen; i++)
		cred->cr_groups[i] = ntohl(*bufp++);
	if (i < NGROUPS)
		cred->cr_groups[i] = NOGROUP;
	bufp += (slen - i);

	if (*bufp++ != RPC_AUTH_NULL || *bufp++ != 0) {
		*authp = rpc_autherr_badverf;
		return SVC_DENIED;
	}

	argp->buf = bufp;
	argp->len = len;

	/* Put NULL verifier */
	svc_putu32(resp, RPC_AUTH_NULL);
	svc_putu32(resp, 0);

	key.m_class = rqstp->rq_server->sv_program->pg_class;
	key.m_addr = rqstp->rq_addr.sin_addr;

	ipm = ip_map_lookup(&key, 0);

	rqstp->rq_client = NULL;

	if (ipm)
		switch (cache_check(&ip_map_cache, &ipm->h, &rqstp->rq_chandle)) {
		case -EAGAIN:
			rv = SVC_DROP;
			break;
		case -ENOENT:
			rv = SVC_OK; /* rq_client is NULL */
			break;
		case 0:
			rqstp->rq_client = &ipm->m_client->h;
			cache_get(&rqstp->rq_client->h);
			ip_map_put(&ipm->h, &ip_map_cache);
			rv = SVC_OK;
			break;
		default: BUG();
		}
	else rv = SVC_DROP;

	if (rqstp->rq_client == NULL && proc != 0)
		goto badcred;
	return rv;

badcred:
	*authp = rpc_autherr_badcred;
	return SVC_DENIED;
}

int
svcauth_unix_release(struct svc_rqst *rqstp)
{
	/* Verifier (such as it is) is already in place.
	 */
	if (rqstp->rq_client)
		auth_domain_put(rqstp->rq_client);
	rqstp->rq_client = NULL;

	return 0;
}


struct auth_ops svcauth_unix = {
	.name		= "unix",
	.flavour	= RPC_AUTH_UNIX,
	.accept 	= svcauth_unix_accept,
	.release	= svcauth_unix_release,
};

