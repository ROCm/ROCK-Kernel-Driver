/*
*  linux/fs/nfsd/nfs4state.c
*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
*  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
*  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
*  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
time_t boot_time;
static u32 current_clientid = 1;
static u32 current_ownerid;
static u32 current_fileid;
static u32 nfs4_init;
stateid_t zerostateid;             /* bits all 0 */
stateid_t onestateid;              /* bits all 1 */

/* debug counters */
u32 list_add_perfile = 0; 
u32 list_del_perfile = 0;
u32 add_perclient = 0;
u32 del_perclient = 0;
u32 alloc_file = 0;
u32 free_file = 0;
u32 alloc_sowner = 0;
u32 free_sowner = 0;
u32 vfsopen = 0;
u32 vfsclose = 0;
u32 alloc_lsowner= 0;

/* forward declarations */
struct nfs4_stateid * find_stateid(stateid_t *stid, int flags);

/* Locking:
 *
 * client_sema: 
 * 	protects clientid_hashtbl[], clientstr_hashtbl[],
 * 	unconfstr_hashtbl[], uncofid_hashtbl[].
 */
static struct semaphore client_sema;

void
nfs4_lock_state(void)
{
	down(&client_sema);
}

void
nfs4_unlock_state(void)
{
	up(&client_sema);
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

/* forward declarations */
static void release_stateowner(struct nfs4_stateowner *sop);
static void release_stateid(struct nfs4_stateid *stp, int flags);
static void release_file(struct nfs4_file *fp);


/* 
 * SETCLIENTID state 
 */

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
	((id) & CLIENT_HASH_MASK)
#define clientstr_hashval(name, namelen) \
	(opaque_hashval((name), (namelen)) & CLIENT_HASH_MASK)

/* conf_id_hashtbl[], and conf_str_hashtbl[] hold confirmed
 * setclientid_confirmed info. 
 *
 * unconf_str_hastbl[] and unconf_id_hashtbl[] hold unconfirmed 
 * setclientid info.
 *
 * client_lru holds client queue ordered by nfs4_client.cl_time
 * for lease renewal.
 */
static struct list_head	conf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	conf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head client_lru;

static inline void
renew_client(struct nfs4_client *clp)
{
	/*
	* Move client to the end to the LRU list.
	*/
	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid)
{
	if (clid->cl_boot == boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x)\n", 
			clid->cl_boot, clid->cl_id);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static inline struct nfs4_client *
alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	if ((clp = kmalloc(sizeof(struct nfs4_client), GFP_KERNEL))!= NULL) {
		memset(clp, 0, sizeof(*clp));
		if ((clp->cl_name.data = kmalloc(name.len, GFP_KERNEL)) != NULL) {
			memcpy(clp->cl_name.data, name.data, name.len);
			clp->cl_name.len = name.len;
		}
		else {
			kfree(clp);
			clp = NULL;
		}
	}
	return clp;
}

static inline void
free_client(struct nfs4_client *clp)
{
	kfree(clp->cl_name.data);
	kfree(clp);
}

static void
expire_client(struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;

	dprintk("NFSD: expire_client\n");
	list_del(&clp->cl_idhash);
	list_del(&clp->cl_strhash);
	list_del(&clp->cl_lru);
	while (!list_empty(&clp->cl_perclient)) {
		sop = list_entry(clp->cl_perclient.next, struct nfs4_stateowner, so_perclient);
		release_stateowner(sop);
	}
	free_client(clp);
}

static struct nfs4_client *
create_client(struct xdr_netobj name) {
	struct nfs4_client *clp;

	if(!(clp = alloc_client(name)))
		goto out;
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_strhash);
	INIT_LIST_HEAD(&clp->cl_perclient);
	INIT_LIST_HEAD(&clp->cl_lru);
out:
	return clp;
}

static void
copy_verf(struct nfs4_client *target, nfs4_verifier *source) {
	memcpy(target->cl_verifier.data, source->data, sizeof(target->cl_verifier.data));
}

static void
copy_clid(struct nfs4_client *target, struct nfs4_client *source) {
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static void
copy_cred(struct svc_cred *target, struct svc_cred *source) {
	int i;

	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	for(i = 0; i < NGROUPS; i++)
		target->cr_groups[i] = source->cr_groups[i];
}

static int
cmp_name(struct xdr_netobj *n1, struct xdr_netobj *n2) {
	if(!n1 || !n2)
		return 0;
	return((n1->len == n2->len) && !memcmp(n1->data, n2->data, n2->len));
}

static int
cmp_verf(nfs4_verifier *v1, nfs4_verifier *v2) {
	return(!memcmp(v1->data,v2->data,sizeof(v1->data)));
}

static int
cmp_clid(clientid_t * cl1, clientid_t * cl2) {
	return((cl1->cl_boot == cl2->cl_boot) &&
	   	(cl1->cl_id == cl2->cl_id));
}

/* XXX what about NGROUP */
static int
cmp_creds(struct svc_cred *cr1, struct svc_cred *cr2){
	return((cr1->cr_uid == cr2->cr_uid) &&
	   	(cr1->cr_gid == cr2->cr_gid));

}

static void
gen_clid(struct nfs4_client *clp) {
	clp->cl_clientid.cl_boot = boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void
gen_confirm(struct nfs4_client *clp) {
	struct timespec 	tv;
	u32 *			p;

	tv = CURRENT_TIME;
	p = (u32 *)clp->cl_confirm.data;
	*p++ = tv.tv_sec;
	*p++ = tv.tv_nsec;
}

static int
check_name(struct xdr_netobj name) {

	if (name.len == 0) 
		return 0;
	if (name.len > NFS4_OPAQUE_LIMIT) {
		printk("NFSD: check_name: name too long(%d)!\n", name.len);
		return 0;
	}
	return 1;
}

void
add_to_unconfirmed(struct nfs4_client *clp, unsigned int strhashval)
{
	unsigned int idhashval;

	list_add(&clp->cl_strhash, &unconf_str_hashtbl[strhashval]);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &unconf_id_hashtbl[idhashval]);
	list_add_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

void
move_to_confirmed(struct nfs4_client *clp, unsigned int idhashval)
{
	unsigned int strhashval;

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_del_init(&clp->cl_strhash);
	list_del_init(&clp->cl_idhash);
	list_add(&clp->cl_idhash, &conf_id_hashtbl[idhashval]);
	strhashval = clientstr_hashval(clp->cl_name.data, 
			clp->cl_name.len);
	list_add(&clp->cl_strhash, &conf_str_hashtbl[strhashval]);
	renew_client(clp);
}

/*
 * RFC 3010 has a complex implmentation description of processing a 
 * SETCLIENTID request consisting of 5 bullets, labeled as 
 * CASE0 - CASE4 below.
 *
 * NOTES:
 * 	callback information will be processed in a future patch
 *
 *	an unconfirmed record is added when:
 *      NORMAL (part of CASE 4): there is no confirmed nor unconfirmed record.
 *	CASE 1: confirmed record found with matching name, principal,
 *		verifier, and clientid.
 *	CASE 2: confirmed record found with matching name, principal,
 *		and there is no unconfirmed record with matching
 *		name and principal
 *
 *      an unconfirmed record is replaced when:
 *	CASE 3: confirmed record found with matching name, principal,
 *		and an unconfirmed record is found with matching 
 *		name, principal, and with clientid and
 *		confirm that does not match the confirmed record.
 *	CASE 4: there is no confirmed record with matching name and 
 *		principal. there is an unconfirmed record with 
 *		matching name, principal.
 *
 *	an unconfirmed record is deleted when:
 *	CASE 1: an unconfirmed record that matches input name, verifier,
 *		and confirmed clientid.
 *	CASE 4: any unconfirmed records with matching name and principal
 *		that exist after an unconfirmed record has been replaced
 *		as described above.
 *
 */
int
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_setclientid *setclid)
{
	u32 			ip_addr = rqstp->rq_addr.sin_addr.s_addr;
	struct xdr_netobj 	clname = { 
		.len = setclid->se_namelen,
		.data = setclid->se_name,
	};
	nfs4_verifier		clverifier = setclid->se_verf;
	unsigned int 		strhashval;
	struct nfs4_client *	conf, * unconf, * new, * clp;
	int 			status;
	struct list_head *pos, *next;
	
	status = nfserr_inval;
	if (!check_name(clname))
		goto out;

	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	strhashval = clientstr_hashval(clname.data, clname.len);

	conf = NULL;
	nfs4_lock_state();
	list_for_each_safe(pos, next, &conf_str_hashtbl[strhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_strhash);
		if (!cmp_name(&clp->cl_name, &clname))
			continue;
		/* 
		 * CASE 0:
		 * clname match, confirmed, different principal
		 * or different ip_address
		 */
		status = nfserr_clid_inuse;
		if (!cmp_creds(&clp->cl_cred,&rqstp->rq_cred)) {
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}
		if (clp->cl_addr != ip_addr) { 
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}

		/* 
	 	 * cl_name match from a previous SETCLIENTID operation
	 	 * XXX check for additional matches?
		 */
		conf = clp;
		break;
	}
	unconf = NULL;
	list_for_each_safe(pos, next, &unconf_str_hashtbl[strhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_strhash);
		if (!cmp_name(&clp->cl_name, &clname))
			continue;
		/* cl_name match from a previous SETCLIENTID operation */
		unconf = clp;
		break;
	}
	status = nfserr_resource;
	if (!conf) {
		/* 
		 * CASE 4:
		 * placed first, because it is the normal case.
		 */
		if (unconf)
			expire_client(unconf);
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new, &clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		add_to_unconfirmed(new, strhashval);
	} else if (cmp_verf(&conf->cl_verifier, &clverifier)) {
		/*
		 * CASE 1:
		 * cl_name match, confirmed, principal match
		 * verifier match: probable callback update
		 *
		 * remove any unconfirmed nfs4_client with 
		 * matching cl_name, cl_verifier, and cl_clientid
		 *
		 * create and insert an unconfirmed nfs4_client with same 
		 * cl_name, cl_verifier, and cl_clientid as existing 
		 * nfs4_client,  but with the new callback info and a 
		 * new cl_confirm
		 */
		if ((unconf) && 
		    cmp_verf(&unconf->cl_verifier, &conf->cl_verifier) &&
		     cmp_clid(&unconf->cl_clientid, &conf->cl_clientid)) {
				expire_client(unconf);
		}
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,&conf->cl_verifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		copy_clid(new, conf);
		gen_confirm(new);
		add_to_unconfirmed(new,strhashval);
	} else if (!unconf) {
		/*
		 * CASE 2:
		 * clname match, confirmed, principal match
		 * verfier does not match
		 * no unconfirmed. create a new unconfirmed nfs4_client
		 * using input clverifier, clname, and callback info
		 * and generate a new cl_clientid and cl_confirm.
		 */
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,&clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		add_to_unconfirmed(new, strhashval);
	} else if (!cmp_clid(&conf->cl_clientid, &unconf->cl_clientid) &&
	      !cmp_verf(&conf->cl_confirm, &unconf->cl_confirm)) {
		/*	
		 * CASE3:
		 * confirmed found (name, principal match)
		 * confirmed verifier does not match input clverifier
		 *
		 * unconfirmed found (name match)
		 * confirmed->cl_clientid != unconfirmed->cl_clientid and
		 * confirmed->cl_confirm != unconfirmed->cl_confirm
		 *
		 * remove unconfirmed.
		 *
		 * create an unconfirmed nfs4_client 
		 * with same cl_name as existing confirmed nfs4_client, 
		 * but with new callback info, new cl_clientid,
		 * new cl_verifier and a new cl_confirm
		 */
		expire_client(unconf);
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,&clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		add_to_unconfirmed(new, strhashval);
	} else {
		/* No cases hit !!! */
		status = nfserr_inval;
		goto out;

	}
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	printk(KERN_INFO "NFSD: this client will not receive delegations\n");
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}


/*
 * RFC 3010 has a complex implmentation description of processing a 
 * SETCLIENTID_CONFIRM request consisting of 4 bullets describing
 * processing on a DRC miss, labeled as CASE1 - CASE4 below.
 *
 * NOTE: callback information will be processed here in a future patch
 */
int
nfsd4_setclientid_confirm(struct svc_rqst *rqstp, struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	u32 ip_addr = rqstp->rq_addr.sin_addr.s_addr;
	unsigned int idhashval;
	struct nfs4_client *clp, *conf = NULL, *unconf = NULL;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	struct list_head *pos, *next;
	int status;

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	idhashval = clientid_hashval(clid->cl_id);
	nfs4_lock_state();
	list_for_each_safe(pos, next, &conf_id_hashtbl[idhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_idhash);
		if (!cmp_clid(&clp->cl_clientid, clid))
			continue;

		status = nfserr_inval;
		/* 
		 * Found a record for this clientid. If the IP addresses
		 * don't match, return ERR_INVAL just as if the record had
		 * not been found.
		 */
		if (clp->cl_addr != ip_addr) { 
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}
		conf = clp;
		break;
	}
	list_for_each_safe(pos, next, &unconf_id_hashtbl[idhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_idhash);
		if (!cmp_clid(&clp->cl_clientid, clid))
			continue;
		status = nfserr_inval;
		if (clp->cl_addr != ip_addr) { 
			printk("NFSD: setclientid: string in use by client"
			"(clientid %08x/%08x)\n",
			clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);
			goto out;
		}
		unconf = clp;
		break;
	}
	/* CASE 1: 
	* unconf record that matches input clientid and input confirm.
	* conf record that matches input clientid.
	* conf  and unconf records match names, verifiers 
	*/
	if ((conf && unconf) && 
	    (cmp_verf(&unconf->cl_confirm, &confirm)) &&
	    (cmp_verf(&conf->cl_verifier, &unconf->cl_verifier)) &&
	    (cmp_name(&conf->cl_name,&unconf->cl_name))  &&
	    (!cmp_verf(&conf->cl_confirm, &unconf->cl_confirm))) {
		if (!cmp_creds(&conf->cl_cred, &unconf->cl_cred)) 
			status = nfserr_clid_inuse;
		else {
			expire_client(conf);
			move_to_confirmed(unconf, idhashval);
			status = nfs_ok;
		}
		goto out;
	} 
	/* CASE 2:
	 * conf record that matches input clientid.
	 * if unconf record that matches input clientid, then unconf->cl_name
	 * or unconf->cl_verifier don't match the conf record.
	 */
	if ((conf && !unconf) || 
	    ((conf && unconf) && 
	     (!cmp_verf(&conf->cl_verifier, &unconf->cl_verifier) ||
	      !cmp_name(&conf->cl_name, &unconf->cl_name)))) {
		if (!cmp_creds(&conf->cl_cred,&rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			status = nfs_ok;
		}
		goto out;
	}
	/* CASE 3:
	 * conf record not found.
	 * unconf record found. 
	 * unconf->cl_confirm matches input confirm
	 */ 
	if (!conf && unconf && cmp_verf(&unconf->cl_confirm, &confirm)) {
		if (!cmp_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			status = nfs_ok;
			move_to_confirmed(unconf, idhashval);
		}
		goto out;
	}
	/* CASE 4:
	 * conf record not found, or if conf, then conf->cl_confirm does not
	 * match input confirm.
	 * unconf record not found, or if unconf, then unconf->cl_confirm 
	 * does not match input confirm.
	 */
	if ((!conf || (conf && !cmp_verf(&conf->cl_confirm, &confirm))) &&
	    (!unconf || (unconf && !cmp_verf(&unconf->cl_confirm, &confirm)))) {
		status = nfserr_stale_clientid;
		goto out;
	}
	/* check that we have hit one of the cases...*/
	status = nfserr_inval;
	goto out;
out:
	/* XXX if status == nfs_ok, probe callback path */
	nfs4_unlock_state();
	return status;
}

/* 
 * Open owner state (share locks)
 */

/* hash tables for nfs4_stateowner */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

#define ownerid_hashval(id) \
        ((id) & OWNER_HASH_MASK)
#define ownerstr_hashval(clientid, ownername) \
        (((clientid) + opaque_hashval((ownername.data), (ownername.len))) & OWNER_HASH_MASK)

static struct list_head	ownerid_hashtbl[OWNER_HASH_SIZE];
static struct list_head	ownerstr_hashtbl[OWNER_HASH_SIZE];

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)
#define FILE_HASH_MASK                  (FILE_HASH_SIZE - 1)
/* hash table for (open)nfs4_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        hash_ptr(x, FILE_HASH_BITS)
#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head stateid_hashtbl[STATEID_HASH_SIZE];

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(unsigned int hashval, struct inode *ino) {
	struct nfs4_file *fp;
	if ((fp = kmalloc(sizeof(struct nfs4_file),GFP_KERNEL))) {
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_perfile);
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		fp->fi_inode = igrab(ino);
		fp->fi_id = current_fileid++;
		alloc_file++;
		return fp;
	}
	return (struct nfs4_file *)NULL;
}

static void
release_all_files(void)
{
	int i;
	struct nfs4_file *fp;

	for (i=0;i<FILE_HASH_SIZE;i++) {
		while (!list_empty(&file_hashtbl[i])) {
			fp = list_entry(file_hashtbl[i].next, struct nfs4_file, fi_hash);
			/* this should never be more than once... */
			if(!list_empty(&fp->fi_perfile)) {
				printk("ERROR: release_all_files: file %p is open, creating dangling state !!!\n",fp);
			}
			release_file(fp);
		}
	}
}

static inline struct nfs4_stateowner *
alloc_stateowner(struct xdr_netobj *owner)
{
	struct nfs4_stateowner *sop;

	if ((sop = kmalloc(sizeof(struct nfs4_stateowner),GFP_KERNEL))) {
		if((sop->so_owner.data = kmalloc(owner->len, GFP_KERNEL))) {
			memcpy(sop->so_owner.data, owner->data, owner->len);
			sop->so_owner.len = owner->len;
			return sop;
		} 
		kfree(sop);
	}
	return (struct nfs4_stateowner *)NULL;
}

/* should use a slab cache */
static void
free_stateowner(struct nfs4_stateowner *sop) {
	if(sop) {
		kfree(sop->so_owner.data);
		kfree(sop);
		sop = NULL;
		free_sowner++;
	}
}

static struct nfs4_stateowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return (struct nfs4_stateowner *)NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_perfilestate);
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_perclient);
	add_perclient++;
	sop->so_is_open_owner = 1;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
	rp = &sop->so_replay;
	rp->rp_status = NFSERR_SERVERFAULT;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	alloc_sowner++;
	return sop;
}

static void
release_stateowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del_init(&sop->so_idhash);
	list_del_init(&sop->so_strhash);
	list_del_init(&sop->so_perclient);
	del_perclient++;
	while (!list_empty(&sop->so_perfilestate)) {
		stp = list_entry(sop->so_perfilestate.next, 
			struct nfs4_stateid, st_perfilestate);
		if(sop->so_is_open_owner)
			release_stateid(stp, OPEN_STATE);
		else
			release_stateid(stp, LOCK_STATE);
	}
	free_stateowner(sop);
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfs4_stateowner *sop, struct nfsd4_open *open) {
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfilestate);
	INIT_LIST_HEAD(&stp->st_perfile);
	list_add(&stp->st_hash, &stateid_hashtbl[hashval]);
	list_add(&stp->st_perfilestate, &sop->so_perfilestate);
	list_add_perfile++;
	list_add(&stp->st_perfile, &fp->fi_perfile);
	stp->st_stateowner = sop;
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_share_access = open->op_share_access;
	stp->st_share_deny = open->op_share_deny;
}

static void
release_stateid(struct nfs4_stateid *stp, int flags) {

	list_del_init(&stp->st_hash);
	list_del_perfile++;
	list_del_init(&stp->st_perfile);
	list_del_init(&stp->st_perfilestate);
	if((stp->st_vfs_set) && (flags & OPEN_STATE)) {
		nfsd_close(&stp->st_vfs_file);
		vfsclose++;
		dput(stp->st_vfs_file.f_dentry);
		mntput(stp->st_vfs_file.f_vfsmnt);
	}
	/* should use a slab cache */
	kfree(stp);
	stp = NULL;
}

static void
release_file(struct nfs4_file *fp)
{
	free_file++;
	list_del_init(&fp->fi_hash);
	iput(fp->fi_inode);
	kfree(fp);
}	

void
release_state_owner(struct nfs4_stateid *stp, struct nfs4_stateowner **sopp,
		int flag)
{
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_file *fp = stp->st_file;

	dprintk("NFSD: release_state_owner\n");
	release_stateid(stp, flag);
	/*
	 * release unused nfs4_stateowners.
	 * XXX will need to be placed  on an  open_stateid_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (sop->so_confirmed && list_empty(&sop->so_perfilestate)) {
		release_stateowner(sop);
		*sopp = NULL;
	}
	/* unused nfs4_file's are releseed. XXX slab cache? */
	if (list_empty(&fp->fi_perfile)) {
		release_file(fp);
	}
}

static int
cmp_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner, clientid_t *clid) {
	return ((sop->so_owner.len == owner->len) && 
	 !memcmp(sop->so_owner.data, owner->data, owner->len) && 
	  (sop->so_client->cl_clientid.cl_id == clid->cl_id));
}

/* search ownerstr_hashtbl[] for owner */
static int
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open, struct nfs4_stateowner **op) {
	struct list_head *pos, *next;
	struct nfs4_stateowner *local = NULL;

	list_for_each_safe(pos, next, &ownerstr_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_stateowner, so_strhash);
		if(!cmp_owner_str(local, &open->op_owner, &open->op_clientid)) 
			continue;
		*op = local;
		return(1);
	}
	return 0;
}

/* see if clientid is in confirmed hash table */
static int
verify_clientid(struct nfs4_client **client, clientid_t *clid) {

	struct list_head *pos, *next;
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_safe(pos, next, &conf_id_hashtbl[idhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_idhash);
		if (!cmp_clid(&clp->cl_clientid, clid))
			continue;
		*client = clp;
		return 1;
	}
	*client = NULL;
	return 0;
}

/* search file_hashtbl[] for file */
static int
find_file(unsigned int hashval, struct inode *ino, struct nfs4_file **fp) {
	struct list_head *pos, *next;
	struct nfs4_file *local = NULL;

	list_for_each_safe(pos, next, &file_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_file, fi_hash);
		if (local->fi_inode == ino) {
			*fp = local;
			return(1);
		}
	}
	return 0;
}

static int
test_share(struct nfs4_stateid *stp, struct nfsd4_open *open) {
	if ((stp->st_share_access & open->op_share_deny) ||
	    (stp->st_share_deny & open->op_share_access)) {
		return 0;
	}
	return 1;
}

int
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	unsigned int fi_hashval;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;
	struct list_head *pos, *next;

	dprintk("NFSD: nfs4_share_conflict\n");

	fi_hashval = file_hashval(ino);
	if (find_file(fi_hashval, ino, &fp)) {
	/* Search for conflicting share reservations */
		list_for_each_safe(pos, next, &fp->fi_perfile) {
			stp = list_entry(pos, struct nfs4_stateid, st_perfile);
			if (stp->st_share_deny & deny_type)
				return nfserr_share_denied;
		}
	}
	return nfs_ok;
}

static inline int
nfs4_file_upgrade(struct file *filp, unsigned int share_access)
{
int status;

	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		status = get_write_access(filp->f_dentry->d_inode);
		if (!status)
			filp->f_mode = FMODE_WRITE;
		else
			return nfserrno(status);
	}
	return nfs_ok;
}

static inline void
nfs4_file_downgrade(struct file *filp, unsigned int share_access)
{
	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		put_write_access(filp->f_dentry->d_inode);
		filp->f_mode = FMODE_READ;
	}
}


/*
 * nfsd4_process_open1()
 * 	lookup stateowner.
 * 		found:
 * 			check confirmed 
 * 				confirmed:
 * 					check seqid
 * 				not confirmed:
 * 					delete owner
 * 					create new owner
 * 		notfound:
 * 			verify clientid
 * 			create new owner
 */
int
nfsd4_process_open1(struct nfsd4_open *open)
{
	int status;
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_stateowner *sop = NULL;

	status = nfserr_inval;
	if (!check_name(open->op_owner))
		goto out;

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(&open->op_clientid))
		goto out;

	nfs4_lock_state();
	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	if (find_openstateowner_str(strhashval, open, &sop)) {
		open->op_stateowner = sop;
		/* check for replay */
		if (open->op_seqid == sop->so_seqid){
			if (!sop->so_replay.rp_buflen) {
			/*
			* The original OPEN failed in so spectacularly that we
			* don't even have replay data saved!  Therefore, we
			* have no choice but to continue processing
			* this OPEN; presumably, we'll fail again for the same
			* reason.
			*/
				dprintk("nfsd4_process_open1: replay with no replay cache\n");
				status = NFS_OK;
				goto renew;
			}
			/* replay: indicate to calling function */
			status = NFSERR_REPLAY_ME;
			goto out;
		}
		if (sop->so_confirmed) {
			if (open->op_seqid == sop->so_seqid + 1) { 
				status = nfs_ok;
				goto renew;
			} 
			status = nfserr_bad_seqid;
			goto out;
		}
		/* If we get here, we received and OPEN for an unconfirmed
		 * nfs4_stateowner. 
		 * Since the sequid's are different, purge the 
		 * existing nfs4_stateowner, and instantiate a new one.
		 */
		clp = sop->so_client;
		release_stateowner(sop);
		goto instantiate_new_owner;
	} 
	/* nfs4_stateowner not found. 
	* verify clientid and instantiate new nfs4_stateowner
	* if verify fails this is presumably the result of the 
	* client's lease expiring.
	*
	* XXX compare clp->cl_addr with rqstp addr? 
	*/
	status = nfserr_expired;
	if (!verify_clientid(&clp, clientid))
		goto out;
instantiate_new_owner:
	status = nfserr_resource;
	if (!(sop = alloc_init_open_stateowner(strhashval, clp, open))) 
		goto out;
	open->op_stateowner = sop;
	status = nfs_ok;
renew:
	renew_client(sop->so_client);
out:
	nfs4_unlock_state();
	return status;
}

int
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct iattr iattr;
	struct nfs4_stateowner *sop = open->op_stateowner;
	struct nfs4_file *fp;
	struct inode *ino;
	unsigned int fi_hashval;
	struct list_head *pos, *next;
	struct nfs4_stateid *stq, *stp = NULL;
	int status;

	status = nfserr_resource;
	if (!sop)
		goto out;

	ino = current_fh->fh_dentry->d_inode;

	nfs4_lock_state();
	fi_hashval = file_hashval(ino);
	if (find_file(fi_hashval, ino, &fp)) {
		/* Search for conflicting share reservations */
		status = nfserr_share_denied;
		list_for_each_safe(pos, next, &fp->fi_perfile) {
		stq = list_entry(pos, struct nfs4_stateid, st_perfile);
			if(stq->st_stateowner == sop) {
				stp = stq;
				continue;
			}
			if (!test_share(stq,open))	
				goto out;
		}
	} else {
	/* No nfs4_file found; allocate and init a new one */
		status = nfserr_resource;
		if ((fp = alloc_init_file(fi_hashval, ino)) == NULL)
			goto out;
	}

	if (!stp) {
		int flags = 0;

		status = nfserr_resource;
		if ((stp = kmalloc(sizeof(struct nfs4_stateid),
						GFP_KERNEL)) == NULL)
			goto out;

		if (open->op_share_access && NFS4_SHARE_ACCESS_WRITE)
			flags = MAY_WRITE;
		else
			flags = MAY_READ;
		if ((status = nfsd_open(rqstp, current_fh,  S_IFREG,
			                      flags,
			                      &stp->st_vfs_file)) != 0)
			goto out_free;

		vfsopen++;
		dget(stp->st_vfs_file.f_dentry);
		mntget(stp->st_vfs_file.f_vfsmnt);

		init_stateid(stp, fp, sop, open);
		stp->st_vfs_set = 1;
	} else {
		/* This is an upgrade of an existing OPEN. 
		 * OR the incoming share with the existing 
		 * nfs4_stateid share */
		int share_access = open->op_share_access;

		share_access &= ~(stp->st_share_access);

		/* update the struct file */
		if ((status = nfs4_file_upgrade(&stp->st_vfs_file, share_access)))
			goto out;
		stp->st_share_access |= share_access;
		stp->st_share_deny |= open->op_share_deny;
		/* bump the stateid */
		update_stateid(&stp->st_stateid);
	}
	dprintk("nfs4_process_open2: stateid=(%08x/%08x/%08x/%08x)\n\n",
	            stp->st_stateid.si_boot, stp->st_stateid.si_stateownerid,
	            stp->st_stateid.si_fileid, stp->st_stateid.si_generation);

	if (open->op_truncate) {
		iattr.ia_valid = ATTR_SIZE;
		iattr.ia_size = 0;
		status = nfsd_setattr(rqstp, current_fh, &iattr, 0, (time_t)0);
		if (status)
			goto out;
	}
	memcpy(&open->op_stateid, &stp->st_stateid, sizeof(stateid_t));

	open->op_delegate_type = NFS4_OPEN_DELEGATE_NONE;
	status = nfs_ok;
out:
	if (fp && list_empty(&fp->fi_perfile))
		release_file(fp);

	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = 0;
	if (!open->op_stateowner->so_confirmed)
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	nfs4_unlock_state();
	return status;
out_free:
	kfree(stp);
	goto out;
}
static struct work_struct laundromat_work;
static void laundromat_main(void *);
static DECLARE_WORK(laundromat_work, laundromat_main, NULL);

int 
nfsd4_renew(clientid_t *clid)
{
	struct nfs4_client *clp;
	struct list_head *pos, *next;
	unsigned int idhashval;
	int status;

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	status = nfs_ok;
	idhashval = clientid_hashval(clid->cl_id);
	list_for_each_safe(pos, next, &conf_id_hashtbl[idhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_idhash);
		if (!cmp_clid(&clp->cl_clientid, clid))
			continue;
		renew_client(clp);
		goto out;
	}
	list_for_each_safe(pos, next, &unconf_id_hashtbl[idhashval]) {
		clp = list_entry(pos, struct nfs4_client, cl_idhash);
		if (!cmp_clid(&clp->cl_clientid, clid))
			continue;
		renew_client(clp);
	goto out;
	}
	/*
	* Couldn't find an nfs4_client for this clientid.  
	* Presumably this is because the client took too long to 
	* RENEW, so return NFS4ERR_EXPIRED.
	*/
	dprintk("nfsd4_renew: clientid not found!\n");
	status = nfserr_expired;
out:
	nfs4_unlock_state();
	return status;
}

time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct list_head *pos, *next;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, return_val = NFSD_LEASE_TIME;

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting, examining clients\n");
	list_for_each_safe(pos, next, &client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (return_val > t)
				return_val = t;
			break;
		}
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		expire_client(clp);
	}
	if (return_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		return_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return return_val;
}

void
laundromat_main(void *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	schedule_delayed_work(&laundromat_work, t*HZ);
}

/* search ownerid_hashtbl[] for stateid owner (stateid->si_stateownerid) */
struct nfs4_stateowner *
find_openstateowner_id(u32 st_id) {
	struct list_head *pos, *next;
	struct nfs4_stateowner *local = NULL;
	unsigned int hashval = ownerid_hashval(st_id);

	list_for_each_safe(pos, next, &ownerid_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_stateowner, so_idhash);
		if(local->so_id == st_id)
			return local;
	}
	return NULL;
}

static inline int
nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stateid *stp)
{
	return (fhp->fh_dentry != stp->st_vfs_file.f_dentry);
}

static int
STALE_STATEID(stateid_t *stateid)
{
	if (stateid->si_boot == boot_time)
		return 0;
	printk("NFSD: stale stateid (%08x/%08x/%08x/%08x)!\n",
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
	return 1;
}


/*
* Checks for stateid operations
*/
int
nfs4_preprocess_stateid_op(struct svc_fh *current_fh, stateid_t *stateid, int flags, struct nfs4_stateid **stpp)
{
	struct nfs4_stateid *stp;
	int status;

	dprintk("NFSD: preprocess_stateid_op: stateid = (%08x/%08x/%08x/%08x)\n",
		stateid->si_boot, stateid->si_stateownerid, 
		stateid->si_fileid, stateid->si_generation); 

	*stpp = NULL;

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (!(stp = find_stateid(stateid, flags))) {
		dprintk("NFSD: preprocess_stateid_op: no open stateid!\n");
		goto out;
	}
	if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp)) {
		dprintk("NFSD: preprocess_stateid_op: fh-stateid mismatch!\n");
		goto out;
	}
	if (!stp->st_stateowner->so_confirmed) {
		dprintk("preprocess_stateid_op: lockowner not confirmed yet!\n");
		goto out;
	}
	if (stateid->si_generation > stp->st_stateid.si_generation) {
		dprintk("preprocess_stateid_op: future stateid?!\n");
		goto out;
	}

	/* OLD STATEID */
	status = nfserr_old_stateid;
	if (stateid->si_generation < stp->st_stateid.si_generation) {
		dprintk("preprocess_stateid_op: old stateid!\n");
		goto out;
	}
	*stpp = stp;
	status = nfs_ok;
	renew_client(stp->st_stateowner->so_client);
out:
	return status;
}


/* 
 * Checks for sequence id mutating operations. 
 */
int
nfs4_preprocess_seqid_op(struct svc_fh *current_fh, u32 seqid, stateid_t *stateid, int flags, struct nfs4_stateowner **sopp, struct nfs4_stateid **stpp)
{
	int status;
	struct nfs4_stateid *stp;
	struct nfs4_stateowner *sop;

	dprintk("NFSD: preprocess_seqid_op: seqid=%d " 
			"stateid = (%08x/%08x/%08x/%08x)\n", seqid,
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);
			        
	*stpp = NULL;
	*sopp = NULL;

	status = nfserr_bad_stateid;
	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		printk("NFSD: preprocess_seqid_op: magic stateid!\n");
		goto out;
	}

	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid))
		goto out;
	/*
	* We return BAD_STATEID if filehandle doesn't match stateid, 
	* the confirmed flag is incorrecly set, or the generation 
	* number is incorrect.  
	* If there is no entry in the openfile table for this id, 
	* we can't always return BAD_STATEID;
	* this might be a retransmitted CLOSE which has arrived after 
	* the openfile has been released.
	*/
	if (!(stp = find_stateid(stateid, flags)))
		goto no_nfs4_stateid;

	status = nfserr_bad_stateid;

	if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp)) {
		printk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		goto out;
	}

	*stpp = stp;
	*sopp = sop = stp->st_stateowner;

	/*
	*  We now validate the seqid and stateid generation numbers.
	*  For the moment, we ignore the possibility of 
	*  generation number wraparound.
	*/
	if (seqid != sop->so_seqid + 1)
		goto check_replay;

	if (sop->so_confirmed) {
		if (flags & CONFIRM) {
			printk("NFSD: preprocess_seqid_op: expected unconfirmed stateowner!\n");
			goto out;
		}
	}
	else {
		if (!(flags & CONFIRM)) {
			printk("NFSD: preprocess_seqid_op: stateowner not confirmed yet!\n");
			goto out;
		}
	}
	if (stateid->si_generation > stp->st_stateid.si_generation) {
		printk("NFSD: preprocess_seqid_op: future stateid?!\n");
		goto out;
	}

	status = nfserr_old_stateid;
	if (stateid->si_generation < stp->st_stateid.si_generation) {
		printk("NFSD: preprocess_seqid_op: old stateid!\n");
		goto out;
	}
	/* XXX renew the client lease here */
	status = nfs_ok;

out:
	return status;

no_nfs4_stateid:

	/*
	* We determine whether this is a bad stateid or a replay, 
	* starting by trying to look up the stateowner.
	* If stateowner is not found - stateid is bad.
	*/
	if (!(sop = find_openstateowner_id(stateid->si_stateownerid))) {
		printk("NFSD: preprocess_seqid_op: no stateowner or nfs4_stateid!\n");
		status = nfserr_bad_stateid;
		goto out;
	}

check_replay:
	if (seqid == sop->so_seqid) {
		printk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* indicate replay to calling function */
		status = NFSERR_REPLAY_ME;
	} else 
		printk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d\n", sop->so_seqid +1, seqid);

		status = nfserr_bad_seqid;
	goto out;
}

int
nfsd4_open_confirm(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_confirm *oc)
{
	int status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);
	oc->oc_stateowner = NULL;
	nfs4_lock_state();

	if ((status = nfs4_preprocess_seqid_op(current_fh, oc->oc_seqid,
					&oc->oc_req_stateid,
					CHECK_FH | CONFIRM | OPEN_STATE,
					&oc->oc_stateowner, &stp)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}
int
nfsd4_open_downgrade(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_downgrade *od)
{
	int status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(current_fh, od->od_seqid, 
					&od->od_stateid, 
					CHECK_FH | OPEN_STATE, 
					&od->od_stateowner, &stp)))
		goto out; 

	status = nfserr_inval;
	if (od->od_share_access & ~stp->st_share_access) {
		dprintk("NFSD:access not a subset current=%08x, desired=%08x\n", 
			stp->st_share_access, od->od_share_access); 
		goto out;
	}
	if (od->od_share_deny & ~stp->st_share_deny) {
		dprintk("NFSD:deny not a subset current=%08x, desired=%08x\n", 
			stp->st_share_deny, od->od_share_deny);
		goto out;
	}
	nfs4_file_downgrade(&stp->st_vfs_file, 
	stp->st_share_access & ~od->od_share_access);
	stp->st_share_access = od->od_share_access;
	stp->st_share_deny = od->od_share_deny;
	update_stateid(&stp->st_stateid);
	memcpy(&od->od_stateid, &stp->st_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

int
nfsd4_close(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_close *close)
{
	int status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(current_fh, close->cl_seqid, 
					&close->cl_stateid, 
					CHECK_FH | OPEN_STATE, 
					&close->cl_stateowner, &stp)))
		goto out; 
	/*
	*  Return success, but first update the stateid.
	*/
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_state_owner() calls nfsd_close() if needed */
	release_state_owner(stp, &close->cl_stateowner, OPEN_STATE);
out:
	nfs4_unlock_state();
	return status;
}

/* 
 * Lock owner state (byte-range locks)
 */
#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))
#define LOCK_HASH_BITS              8
#define LOCK_HASH_SIZE             (1 << LOCK_HASH_BITS)
#define LOCK_HASH_MASK             (LOCK_HASH_SIZE - 1)

#define lockownerid_hashval(id) \
        ((id) & LOCK_HASH_MASK)
#define lock_ownerstr_hashval(x, clientid, ownername) \
        ((file_hashval(x) + (clientid) + opaque_hashval((ownername.data), (ownername.len))) & LOCK_HASH_MASK)

static struct list_head lock_ownerid_hashtbl[LOCK_HASH_SIZE];
static struct list_head	lock_ownerstr_hashtbl[LOCK_HASH_SIZE];
static struct list_head lockstateid_hashtbl[STATEID_HASH_SIZE];

struct nfs4_stateid *
find_stateid(stateid_t *stid, int flags)
{
	struct list_head *pos, *next;
	struct nfs4_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("NFSD: find_stateid flags 0x%x\n",flags);
	if ((flags & LOCK_STATE) || (flags & RDWR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_safe(pos, next, &lockstateid_hashtbl[hashval]) {
			local = list_entry(pos, struct nfs4_stateid, st_hash);
			if((local->st_stateid.si_stateownerid == st_id) &&
			   (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} 
	if ((flags & OPEN_STATE) || (flags & RDWR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_safe(pos, next, &stateid_hashtbl[hashval]) {
			local = list_entry(pos, struct nfs4_stateid, st_hash);
			if((local->st_stateid.si_stateownerid == st_id) &&
			   (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} else
		printk("NFSD: find_stateid: ERROR: no state flag\n");
	return NULL;
}


/*
 * TODO: Linux file offsets are _signed_ 64-bit quantities, which means that
 * we can't properly handle lock requests that go beyond the (2^63 - 1)-th
 * byte, because of sign extension problems.  Since NFSv4 calls for 64-bit
 * locking, this prevents us from being completely protocol-compliant.  The
 * real solution to this problem is to start using unsigned file offsets in
 * the VFS, but this is a very deep change!
 */
static inline void
nfs4_transform_lock_offset(struct file_lock *lock)
{
	if (lock->fl_start < 0)
		lock->fl_start = OFFSET_MAX;
	if (lock->fl_end < 0)
		lock->fl_end = OFFSET_MAX;
}

int
nfs4_verify_lock_stateowner(struct nfs4_stateowner *sop, unsigned int hashval)
{
	struct list_head *pos, *next;
	struct nfs4_stateowner *local = NULL;
	int status = 0;
			        
	if (hashval >= LOCK_HASH_SIZE)
		goto out;
	list_for_each_safe(pos, next, &lock_ownerid_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_stateowner, so_idhash);
		if (local == sop) {
			status = 1;
			goto out;
		}
	}
out:
	return status;
}


static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_stateowner *sop = (struct nfs4_stateowner *) fl->fl_owner;

	deny->ld_sop = NULL;
	if (nfs4_verify_lock_stateowner(sop, fl->fl_pid))
		deny->ld_sop = sop;
	deny->ld_start = fl->fl_start;
	deny->ld_length = ~(u64)0;
	if (fl->fl_end != ~(u64)0)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}


static int
find_lockstateowner_str(unsigned int hashval, struct xdr_netobj *owner, clientid_t *clid, struct nfs4_stateowner **op) {
	struct list_head *pos, *next;
	struct nfs4_stateowner *local = NULL;

	list_for_each_safe(pos, next, &lock_ownerstr_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_stateowner, so_strhash);
		if(!cmp_owner_str(local, owner, clid)) 
			continue;
		*op = local;
		return(1);
	}
	*op = NULL;
	return 0;
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occured. 
 *
 * strhashval = lock_ownerstr_hashval 
 * so_seqid = lock->lk_new_lock_seqid - 1: it gets bumped in encode 
 */

static struct nfs4_stateowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_lock *lock) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&lock->lk_new_owner)))
		return (struct nfs4_stateowner *)NULL;
	idhashval = lockownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_perfilestate);
	list_add(&sop->so_idhash, &lock_ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &lock_ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_perclient);
	add_perclient++;
	sop->so_is_open_owner = 0;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = lock->lk_new_lock_seqid - 1;
	sop->so_confirmed = 1;
	rp = &sop->so_replay;
	rp->rp_status = NFSERR_SERVERFAULT;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	alloc_lsowner++;
	return sop;
}

struct nfs4_stateid *
alloc_init_lock_stateid(struct nfs4_stateowner *sop, struct nfs4_file *fp, struct nfs4_stateid *open_stp)
{
	struct nfs4_stateid *stp;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	if ((stp = kmalloc(sizeof(struct nfs4_stateid), 
					GFP_KERNEL)) == NULL)
		goto out;

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfile);
	INIT_LIST_HEAD(&stp->st_perfilestate);
	list_add(&stp->st_hash, &lockstateid_hashtbl[hashval]);
	list_add(&stp->st_perfile, &fp->fi_perfile);
	list_add_perfile++;
	list_add(&stp->st_perfilestate, &sop->so_perfilestate);
	stp->st_stateowner = sop;
	stp->st_file = fp;
	stp->st_stateid.si_boot = boot_time;
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_vfs_file = open_stp->st_vfs_file;
	stp->st_vfs_set = open_stp->st_vfs_set;
	stp->st_share_access = -1; 
	stp->st_share_deny = -1;

out:
	return stp;
}

/*
 *  LOCK operation 
 */
int
nfsd4_lock(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lock *lock)
{
	struct nfs4_stateowner *lock_sop = NULL, *open_sop = NULL;
	struct nfs4_stateid *lock_stp;
	struct file *filp;
	struct file_lock file_lock;
	struct file_lock *conflock;
	int status = 0;
	unsigned int strhashval;

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		lock->lk_offset, lock->lk_length);

	lock->lk_stateowner = NULL;
	nfs4_lock_state();

	if (lock->lk_is_new) {
	/*
	 * Client indicates that this is a new lockowner.
	 * Use open owner and open stateid to create lock owner and lock 
	 * stateid.
	 */
		struct nfs4_stateid *open_stp = NULL;
		struct nfs4_file *fp;
		
		status = nfserr_stale_clientid;
		if (STALE_CLIENTID(&lock->lk_new_clientid)) {
			printk("NFSD: nfsd4_lock: clientid is stale!\n");
			goto out;
		}
		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_seqid_op(current_fh, 
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
		                        CHECK_FH | OPEN_STATE,
		                        &open_sop, &open_stp);
		if (status)
			goto out;
		/* create lockowner and lock stateid */
		fp = open_stp->st_file;
		strhashval = lock_ownerstr_hashval(fp->fi_inode, 
				open_sop->so_client->cl_clientid.cl_id, 
				lock->v.new.owner);

		/* 
		 * If we already have this lock owner, the client is in 
		 * error (or our bookeeping is wrong!) 
		 * for asking for a 'new lock'.
		 */
		status = nfserr_bad_stateid;
		if (find_lockstateowner_str(strhashval, &lock->v.new.owner,
					&lock->v.new.clientid, &lock_sop))
			goto out;
		status = nfserr_resource;
		if (!(lock->lk_stateowner = alloc_init_lock_stateowner(strhashval, 
						open_sop->so_client, lock)))
			goto out;
		if ((lock_stp = alloc_init_lock_stateid(lock->lk_stateowner, 
						fp, open_stp)) == NULL)
			goto out;
		/* bump the open seqid used to create the lock */
		open_sop->so_seqid++;
	} else {
		/* lock (lock owner + lock stateid) already exists */
		status = nfs4_preprocess_seqid_op(current_fh,
				       lock->lk_old_lock_seqid, 
				       &lock->lk_old_lock_stateid, 
				       CHECK_FH | LOCK_STATE, 
				       &lock->lk_stateowner, &lock_stp);
		if (status)
			goto out;
	}
	/* lock->lk_stateowner and lock_stp have been created or found */
	filp = &lock_stp->st_vfs_file;

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, MAY_LOCK))) {
		printk("NFSD: nfsd4_lock: permission denied!\n");
		goto out;
	}

	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			status = nfserr_inval;
		goto out;
	}
	file_lock.fl_owner = (fl_owner_t) lock->lk_stateowner;
	file_lock.fl_pid = lockownerid_hashval(lock->lk_stateowner->so_id);
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX;
	file_lock.fl_notify = NULL;
	file_lock.fl_insert = NULL;
	file_lock.fl_remove = NULL;

	file_lock.fl_start = lock->lk_offset;
	if ((lock->lk_length == ~(u64)0) || 
			LOFF_OVERFLOW(lock->lk_offset, lock->lk_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lock->lk_offset + lock->lk_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	* Try to lock the file in the VFS.
	* Note: locks.c uses the BKL to protect the inode's lock list.
	*/

	status = posix_lock_file(filp, &file_lock);
	dprintk("NFSD: nfsd4_lock: posix_test_lock passed. posix_lock_file status %d\n",status);
	switch (-status) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stateid, 
				sizeof(stateid_t));
		goto out;
	case (EAGAIN):
		goto conflicting_lock;
	case (EDEADLK):
		status = nfserr_deadlock;
	default:        
		dprintk("NFSD: nfsd4_lock: posix_lock_file() failed! status %d\n",status);
		goto out_destroy_new_stateid;
	}

conflicting_lock:
	dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
	status = nfserr_denied;
	/* XXX There is a race here. Future patch needed to provide 
	 * an atomic posix_lock_and_test_file
	 */
	if (!(conflock = posix_test_lock(filp, &file_lock))) {
		status = nfserr_serverfault;
		goto out;
	}
	nfs4_set_lock_denied(conflock, &lock->lk_denied);

out_destroy_new_stateid:
	if (lock->lk_is_new) {
		dprintk("NFSD: nfsd4_lock: destroy new stateid!\n");
	/*
	* An error encountered after instantiation of the new
	* stateid has forced us to destroy it.
	*/
		if (!seqid_mutating_err(status))
			open_sop->so_seqid--;

		release_state_owner(lock_stp, &lock->lk_stateowner, LOCK_STATE);
	}
out:
	nfs4_unlock_state();
	return status;
}

/*
 * LOCKT operation
 */
int
nfsd4_lockt(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct nfs4_stateowner *sop;
	struct file file;
	struct file_lock file_lock;
	struct file_lock *conflicting_lock;
	unsigned int strhashval;
	int status;

	lockt->lt_stateowner = NULL;
	nfs4_lock_state();

	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(&lockt->lt_clientid)) {
		printk("NFSD: nfsd4_lockt: clientid is stale!\n");
		goto out;
	}

	if ((status = fh_verify(rqstp, current_fh, S_IFREG, 0))) {
		printk("NFSD: nfsd4_lockt: fh_verify() failed!\n");
		goto out;
	}

	inode = current_fh->fh_dentry->d_inode;
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			printk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	strhashval = lock_ownerstr_hashval(inode, 
			lockt->lt_clientid.cl_id, lockt->lt_owner);

	if (find_lockstateowner_str(strhashval, &lockt->lt_owner, 
					&lockt->lt_clientid, 
					&lockt->lt_stateowner)) {
		printk("NFSD: nsfd4_lockt: lookup_lockowner() failed!\n");
		goto out;
	}

	sop = lockt->lt_stateowner;
	if (sop) {
		file_lock.fl_owner = (fl_owner_t) sop;
		file_lock.fl_pid = lockownerid_hashval(sop->so_id);
	} else {
		file_lock.fl_owner = NULL;
		file_lock.fl_pid = 0;
	}
	file_lock.fl_flags = FL_POSIX;

	file_lock.fl_start = lockt->lt_offset;
	if ((lockt->lt_length == ~(u64)0) || LOFF_OVERFLOW(lockt->lt_offset, lockt->lt_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = lockt->lt_offset + lockt->lt_length - 1;

	nfs4_transform_lock_offset(&file_lock);

	/* posix_test_lock uses the struct file _only_ to resolve the inode.
	 * since LOCKT doesn't require an OPEN, and therefore a struct
	 * file may not exist, pass posix_test_lock a struct file with
	 * only the dentry:inode set.
	 */
	memset(&file, 0, sizeof (struct file));
	file.f_dentry = current_fh->fh_dentry;

	status = nfs_ok;
	conflicting_lock = posix_test_lock(&file, &file_lock);
	if (conflicting_lock) {
		status = nfserr_denied;
		nfs4_set_lock_denied(conflicting_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	return status;
}

int
nfsd4_locku(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_locku *locku)
{
	struct nfs4_stateid *stp;
	struct file *filp = NULL;
	struct file_lock file_lock;
	int status;
						        
	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		locku->lu_offset, locku->lu_length);
	nfs4_lock_state();
									        
	if ((status = nfs4_preprocess_seqid_op(current_fh, 
					locku->lu_seqid, 
					&locku->lu_stateid, 
					CHECK_FH | LOCK_STATE, 
					&locku->lu_stateowner, &stp)))
		goto out;

	filp = &stp->st_vfs_file;
	BUG_ON(!filp);
	file_lock.fl_type = F_UNLCK;
	file_lock.fl_owner = (fl_owner_t) locku->lu_stateowner;
	file_lock.fl_pid = lockownerid_hashval(locku->lu_stateowner->so_id);
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX; 
	file_lock.fl_notify = NULL;
	file_lock.fl_insert = NULL;
	file_lock.fl_remove = NULL;
	file_lock.fl_start = locku->lu_offset;

	if ((locku->lu_length == ~(u64)0) || LOFF_OVERFLOW(locku->lu_offset, locku->lu_length))
		file_lock.fl_end = ~(u64)0;
	else
		file_lock.fl_end = locku->lu_offset + locku->lu_length - 1;
	nfs4_transform_lock_offset(&file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	status = posix_lock_file(filp, &file_lock); 
	if (status) {
		printk("NFSD: nfs4_locku: posix_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stateid, sizeof(stateid_t));

out:
	nfs4_unlock_state();
	return status;

out_nfserr:
	status = nfserrno(status);
	goto out;
}

/* 
 * Start and stop routines
 */

void 
nfs4_state_init(void)
{
	int i;

	if (nfs4_init)
		return;
	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&conf_id_hashtbl[i]);
		INIT_LIST_HEAD(&conf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_str_hashtbl[i]);
		INIT_LIST_HEAD(&unconf_id_hashtbl[i]);
	}
	for (i = 0; i < FILE_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&file_hashtbl[i]);
	}
	for (i = 0; i < OWNER_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&ownerstr_hashtbl[i]);
		INIT_LIST_HEAD(&ownerid_hashtbl[i]);
	}
	for (i = 0; i < STATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&stateid_hashtbl[i]);
		INIT_LIST_HEAD(&lockstateid_hashtbl[i]);
	}
	for (i = 0; i < LOCK_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&lock_ownerid_hashtbl[i]);
		INIT_LIST_HEAD(&lock_ownerstr_hashtbl[i]);
	}
	memset(&zerostateid, 0, sizeof(stateid_t));
	memset(&onestateid, ~0, sizeof(stateid_t));

	INIT_LIST_HEAD(&client_lru);
	init_MUTEX(&client_sema);
	boot_time = get_seconds();
	INIT_WORK(&laundromat_work,laundromat_main, NULL);
	schedule_delayed_work(&laundromat_work, NFSD_LEASE_TIME*HZ);
	nfs4_init = 1;

}

static void
__nfs4_state_shutdown(void)
{
	int i;
	struct nfs4_client *clp = NULL;

	for (i = 0; i < CLIENT_HASH_SIZE; i++) {
		while (!list_empty(&conf_id_hashtbl[i])) {
			clp = list_entry(conf_id_hashtbl[i].next, struct nfs4_client, cl_idhash);
			expire_client(clp);
		}
		while (!list_empty(&unconf_str_hashtbl[i])) {
			clp = list_entry(unconf_str_hashtbl[i].next, struct nfs4_client, cl_strhash);
			expire_client(clp);
		}
	}
	release_all_files();
	cancel_delayed_work(&laundromat_work);
	flush_scheduled_work();
	nfs4_init = 0;
	dprintk("NFSD: list_add_perfile %d list_del_perfile %d\n",
			list_add_perfile, list_del_perfile);
	dprintk("NFSD: add_perclient %d del_perclient %d\n",
			add_perclient, del_perclient);
	dprintk("NFSD: alloc_file %d free_file %d\n",
			alloc_file, free_file);
	dprintk("NFSD: alloc_sowner %d alloc_lsowner %d free_sowner %d\n",
			alloc_sowner, alloc_lsowner, free_sowner);
	dprintk("NFSD: vfsopen %d vfsclose %d\n",
			vfsopen, vfsclose);
}

void
nfs4_state_shutdown(void)
{
	nfs4_lock_state();
	__nfs4_state_shutdown();
	nfs4_unlock_state();
}
