/*
 * linux/net/sunrpc/svcauth.c
 *
 * The generic interface for RPC authentication on the server side.
 * 
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 *
 * CHANGES
 * 19-Apr-2000 Chris Evans      - Security fix
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/sunrpc/types.h>
#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/svcsock.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/err.h>
#include <linux/hash.h>

#define RPCDBG_FACILITY	RPCDBG_AUTH


/*
 * Table of authenticators
 */
extern struct auth_ops svcauth_null;
extern struct auth_ops svcauth_unix;

static struct auth_ops	*authtab[RPC_AUTH_MAXFLAVOR] = {
	[0] = &svcauth_null,
	[1] = &svcauth_unix,
};

int
svc_authenticate(struct svc_rqst *rqstp, u32 *authp)
{
	rpc_authflavor_t	flavor;
	struct auth_ops		*aops;

	*authp = rpc_auth_ok;

	flavor = ntohl(svc_getu32(&rqstp->rq_arg.head[0]));

	dprintk("svc: svc_authenticate (%d)\n", flavor);
	if (flavor >= RPC_AUTH_MAXFLAVOR || !(aops = authtab[flavor])) {
		*authp = rpc_autherr_badcred;
		return SVC_DENIED;
	}

	rqstp->rq_authop = aops;
	return aops->accept(rqstp, authp);
}

/* A request, which was authenticated, has now executed.
 * Time to finalise the the credentials and verifier
 * and release and resources
 */
int svc_authorise(struct svc_rqst *rqstp)
{
	struct auth_ops *aops = rqstp->rq_authop;
	int rv = 0;

	rqstp->rq_authop = NULL;
	
	if (aops) 
		rv = aops->release(rqstp);

	/* FIXME should I count and release authops */
	return rv;
}

int
svc_auth_register(rpc_authflavor_t flavor, struct auth_ops *aops)
{
	if (flavor >= RPC_AUTH_MAXFLAVOR || authtab[flavor])
		return -EINVAL;
	authtab[flavor] = aops;
	return 0;
}

void
svc_auth_unregister(rpc_authflavor_t flavor)
{
	if (flavor < RPC_AUTH_MAXFLAVOR)
		authtab[flavor] = NULL;
}

/**************************************************
 * cache for domain name to auth_domain
 * Entries are only added by flavours which will normally
 * have a structure that 'inherits' from auth_domain.
 * e.g. when an IP -> domainname is given to  auth_unix,
 * and the domain name doesn't exist, it will create a
 * auth_unix_domain and add it to this hash table.
 * If it finds the name does exist, but isn't AUTH_UNIX,
 * it will complain.
 */

/*
 * Auth auth_domain cache is somewhat different to other caches,
 * largely because the entries are possibly of different types:
 * each auth flavour has it's own type.
 * One consequence of this that DefineCacheLookup cannot
 * allocate a new structure as it cannot know the size.
 * Notice that the "INIT" code fragment is quite different
 * from other caches.  When auth_domain_lookup might be
 * creating a new domain, the new domain is passed in
 * complete and it is used as-is rather than being copied into
 * another structure.
 */
#define	DN_HASHBITS	6
#define	DN_HASHMAX	(1<<DN_HASHBITS)
#define	DN_HASHMASK	(DN_HASHMAX-1)

static struct cache_head	*auth_domain_table[DN_HASHMAX];
void auth_domain_drop(struct cache_head *item, struct cache_detail *cd)
{
	struct auth_domain *dom = container_of(item, struct auth_domain, h);
	if (cache_put(item,cd))
		authtab[dom->flavour]->domain_release(dom);
}


struct cache_detail auth_domain_cache = {
	.hash_size	= DN_HASHMAX,
	.hash_table	= auth_domain_table,
	.name		= "auth.domain",
	.cache_put	= auth_domain_drop,
};

void auth_domain_put(struct auth_domain *dom)
{
	auth_domain_drop(&dom->h, &auth_domain_cache);
}

static inline int auth_domain_hash(struct auth_domain *item)
{
	return hash_str(item->name, DN_HASHBITS);
}
static inline int auth_domain_match(struct auth_domain *tmp, struct auth_domain *item)
{
	return strcmp(tmp->name, item->name) == 0;
}
DefineCacheLookup(struct auth_domain,
		  h,
		  auth_domain_lookup,
		  (struct auth_domain *item, int set),
		  /* no setup */,
		  &auth_domain_cache,
		  auth_domain_hash(item),
		  auth_domain_match(tmp, item),
		  kfree(new); if(!set) return NULL;
		  new=item; atomic_inc(&new->h.refcnt),
		  /* no update */,
		  0 /* no inplace updates */
		  )

struct auth_domain *auth_domain_find(char *name)
{
	struct auth_domain *rv, ad;

	ad.name = name;
	rv = auth_domain_lookup(&ad, 0);
	return rv;
}
