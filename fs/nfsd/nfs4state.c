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
static u32 current_ownerid = 0;
static u32 current_fileid = 0;
static u32 nfs4_init = 0;
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

/* Locking:
 *
 * client_sema: 
 * 	protects clientid_hashtbl[], clientstr_hashtbl[],
 * 	unconfstr_hashtbl[], uncofid_hashtbl[].
 */
static struct semaphore client_sema;

void
nfsd4_lock_state(void)
{
	down(&client_sema);
}

void
nfsd4_unlock_state(void)
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
static void release_stateid(struct nfs4_stateid *stp);
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
copy_verf(struct nfs4_client *target, nfs4_verifier source) {
	memcpy(&target->cl_verifier, source, sizeof(nfs4_verifier));
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
cmp_verf(nfs4_verifier v1, nfs4_verifier v2) {
	return(!memcmp(v1,v2,sizeof(nfs4_verifier)));
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
	p = (u32 *)clp->cl_confirm;
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
	char *			clverifier = setclid->se_verf;
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
	down(&client_sema);
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
		copy_verf(new,clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		add_to_unconfirmed(new, strhashval);
	} else if (cmp_verf(conf->cl_verifier, clverifier)) {
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
		    cmp_verf(unconf->cl_verifier, conf->cl_verifier) &&
		     cmp_clid(&unconf->cl_clientid, &conf->cl_clientid)) {
				expire_client(unconf);
		}
		if (!(new = create_client(clname)))
			goto out;
		copy_verf(new,conf->cl_verifier);
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
		copy_verf(new,clverifier);
		new->cl_addr = ip_addr;
		copy_cred(&new->cl_cred,&rqstp->rq_cred);
		gen_clid(new);
		gen_confirm(new);
		add_to_unconfirmed(new, strhashval);
	} else if (!cmp_clid(&conf->cl_clientid, &unconf->cl_clientid) &&
	      !cmp_verf(conf->cl_confirm, unconf->cl_confirm)) {
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
		copy_verf(new,clverifier);
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
	memcpy(&setclid->se_confirm, new->cl_confirm, sizeof(nfs4_verifier));
	printk(KERN_INFO "NFSD: this client will not receive delegations\n");
	status = nfs_ok;
out:
	up(&client_sema);
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
	char * confirm = setclientid_confirm->sc_confirm; 
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
	down(&client_sema);
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
	    (cmp_verf(unconf->cl_confirm, confirm)) &&
	    (cmp_verf(conf->cl_verifier, unconf->cl_verifier)) &&
	    (cmp_name(&conf->cl_name,&unconf->cl_name))  &&
	    (!cmp_verf(conf->cl_confirm, unconf->cl_confirm))) {
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
	     (!cmp_verf(conf->cl_verifier, unconf->cl_verifier) ||
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
	if (!conf && unconf && cmp_verf(unconf->cl_confirm, confirm)) {
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
	if ((!conf || (conf && !cmp_verf(conf->cl_confirm, confirm))) &&
	    (!unconf || (unconf && !cmp_verf(unconf->cl_confirm, confirm)))) {
		status = nfserr_stale_clientid;
		goto out;
	}
	/* check that we have hit one of the cases...*/
	status = nfserr_inval;
	goto out;
out:
	/* XXX if status == nfs_ok, probe callback path */
	up(&client_sema);
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
#define OPENSTATEID_HASH_BITS              10
#define OPENSTATEID_HASH_SIZE              (1 << OPENSTATEID_HASH_BITS)
#define OPENSTATEID_HASH_MASK              (OPENSTATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        ((unsigned int)((x)->dev + (x)->ino + (x)->generation) & FILE_HASH_MASK)
#define openstateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & OPENSTATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head openstateid_hashtbl[OPENSTATEID_HASH_SIZE];

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(unsigned int hashval, nfs4_ino_desc_t *ino) {
	struct nfs4_file *fp;
	if ((fp = kmalloc(sizeof(struct nfs4_file),GFP_KERNEL))) {
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_perfile);
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		memcpy(&fp->fi_ino, ino, sizeof(nfs4_ino_desc_t));
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
alloc_init_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return (struct nfs4_stateowner *)NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_peropenstate);
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_perclient);
	add_perclient++;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
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
	while (!list_empty(&sop->so_peropenstate)) {
		stp = list_entry(sop->so_peropenstate.next, 
			struct nfs4_stateid, st_peropenstate);
		release_stateid(stp);
	}
	free_stateowner(sop);
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfs4_stateowner *sop, struct nfsd4_open *open) {
	unsigned int hashval = openstateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_peropenstate);
	INIT_LIST_HEAD(&stp->st_perfile);
	list_add(&stp->st_hash, &openstateid_hashtbl[hashval]);
	list_add(&stp->st_peropenstate, &sop->so_peropenstate);
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
release_stateid(struct nfs4_stateid *stp) {

	list_del_init(&stp->st_hash);
	list_del_perfile++;
	list_del_init(&stp->st_perfile);
	list_del_init(&stp->st_peropenstate);
	if(stp->st_vfs_set) {
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
	kfree(fp);
}	

void
release_open_state(struct nfs4_stateid *stp)
{
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_file *fp = stp->st_file;

	dprintk("NFSD: release_open_state\n");
	release_stateid(stp);
	/*
	 * release unused nfs4_stateowners.
	 * XXX will need to be placed  on an  open_stateid_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (sop->so_confirmed && list_empty(&sop->so_peropenstate)) {
		release_stateowner(sop);
	}
	/* unused nfs4_file's are releseed. XXX slab cache? */
	if (list_empty(&fp->fi_perfile)) {
		release_file(fp);
	}
}

static int
cmp_owner_str(struct nfs4_stateowner *sop, struct nfsd4_open *open) {
	return ((sop->so_owner.len == open->op_owner.len) && 
	 !memcmp(sop->so_owner.data, open->op_owner.data, sop->so_owner.len) && 
	  (sop->so_client->cl_clientid.cl_id == open->op_clientid.cl_id));
}

/* search ownerstr_hashtbl[] for owner */
static int
find_stateowner_str(unsigned int hashval, struct nfsd4_open *open, struct nfs4_stateowner **op) {
	struct list_head *pos, *next;
	struct nfs4_stateowner *local = NULL;

	list_for_each_safe(pos, next, &ownerstr_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_stateowner, so_strhash);
		if(!cmp_owner_str(local, open)) 
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
find_file(unsigned int hashval, nfs4_ino_desc_t *ino, struct nfs4_file **fp) {
	struct list_head *pos, *next;
	struct nfs4_file *local = NULL;

	list_for_each_safe(pos, next, &file_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_file, fi_hash);
		if(!memcmp(&local->fi_ino, ino, sizeof(nfs4_ino_desc_t))) {
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

static inline void
nfs4_init_ino(nfs4_ino_desc_t *ino, struct svc_fh *fhp)
{
	struct inode *inode;
	if (!fhp->fh_dentry)
		BUG();
	inode = fhp->fh_dentry->d_inode;
	if (!inode)
		BUG();
	ino->dev = inode->i_sb->s_dev;
	ino->ino = inode->i_ino;
	ino->generation = inode->i_generation;
}

int
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	nfs4_ino_desc_t ino;
	unsigned int fi_hashval;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;
	struct list_head *pos, *next;

	dprintk("NFSD: nfs4_share_conflict\n");

	nfs4_init_ino(&ino, current_fh);
	fi_hashval = file_hashval(&ino);
	if (find_file(fi_hashval, &ino, &fp)) {
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

	down(&client_sema); /* XXX need finer grained locking */
	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	if (find_stateowner_str(strhashval, open, &sop)) {
		open->op_stateowner = sop;
		if (open->op_seqid == sop->so_seqid){
			/* XXX retplay: for now, return bad seqid */
			status = nfserr_bad_seqid;
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
		 * nfs4_stateowner. If seqid's are the same then this 
		 * is a replay.
		 * If the sequid's are different, then purge the 
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
	if (!(sop = alloc_init_stateowner(strhashval, clp, open))) 
		goto out;
	open->op_stateowner = sop;
	status = nfs_ok;
renew:
	renew_client(sop->so_client);
out:
	up(&client_sema); /*XXX need finer grained locking */
	return status;
}

int
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct iattr iattr;
	struct nfs4_stateowner *sop = open->op_stateowner;
	struct nfs4_file *fp;
	nfs4_ino_desc_t ino;
	unsigned int fi_hashval;
	struct list_head *pos, *next;
	struct nfs4_stateid *stq, *stp = NULL;
	int status;

	status = nfserr_resource;
	if (!sop)
		goto out;

	nfs4_init_ino(&ino, current_fh);

	down(&client_sema); /*XXX need finer grained locking */
	fi_hashval = file_hashval(&ino);
	if (find_file(fi_hashval, &ino, &fp)) {
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
		if ((fp = alloc_init_file(fi_hashval, &ino)) == NULL)
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
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = 0;
	if (!open->op_stateowner->so_confirmed)
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	up(&client_sema); /*XXX need finer grained locking */
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

	down(&client_sema);
	printk("process_renew(%08x/%08x): starting\n", 
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
	printk("nfsd4_renew: clientid not found!\n");
	status = nfserr_expired;
out:
	up(&client_sema);
	return status;
}

time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct list_head *pos, *next;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, return_val = NFSD_LEASE_TIME;

	down(&client_sema);

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
	up(&client_sema); 
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

/* search openstateid_hashtbl[] for stateid */
struct nfs4_stateid *
find_stateid(stateid_t *stid)
{
	struct list_head *pos, *next;
	struct nfs4_stateid *local = NULL;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval = openstateid_hashval(st_id, f_id);

	list_for_each_safe(pos, next, &openstateid_hashtbl[hashval]) {
		local = list_entry(pos, struct nfs4_stateid, st_hash);
		if((local->st_stateid.si_stateownerid == st_id) &&
		   (local->st_stateid.si_fileid == f_id))
			return local;
	}
	return NULL;
}

/* search ownerid_hashtbl[] for stateid owner (stateid->si_stateownerid) */
struct nfs4_stateowner *
find_stateowner_id(u32 st_id) {
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

	dprintk("NFSD: preprocess_stateid_op:stateid = (%08x/%08x/%08x/%08x)\n",
		stateid->si_boot, stateid->si_stateownerid, 
		stateid->si_fileid, stateid->si_generation); 

	*stpp = NULL;

	/* STALE STATEID */
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	/* BAD STATEID */
	status = nfserr_bad_stateid;
	if (!(stp = find_stateid(stateid))) {
		dprintk("NFSD: process stateid: no open stateid!\n");
		goto out;
	}
	if ((flags & CHECK_FH) && nfs4_check_fh(current_fh, stp)) {
		dprintk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		goto out;
	}
	if (!stp->st_stateowner->so_confirmed) {
		dprintk("process_stateid: lockowner not confirmed yet!\n");
		goto out;
	}
	if (stateid->si_generation > stp->st_stateid.si_generation) {
		dprintk("process_stateid: future stateid?!\n");
		goto out;
	}

	/* OLD STATEID */
	status = nfserr_old_stateid;
	if (stateid->si_generation < stp->st_stateid.si_generation) {
		dprintk("process_stateid: old stateid!\n");
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
 *
 * XXX need to code replay cache logic
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
	if (!(stp = find_stateid(stateid)))
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
	if (!(sop = find_stateowner_id(stateid->si_stateownerid))) {
		printk("NFSD: preprocess_seqid_op: no stateowner or nfs4_stateid!\n");
		status = nfserr_bad_stateid;
		goto out;
	}

check_replay:
	status = nfserr_bad_seqid;
	if (seqid == sop->so_seqid) {
		printk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* XXX will need to indicate replay to calling function here */
	} else 
		printk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d\n", sop->so_seqid +1, seqid);

	goto out;
}

int
nfsd4_open_confirm(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_confirm *oc)
{
	int status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			current_fh->fh_dentry->d_name.len,
			current_fh->fh_dentry->d_name.name);
	oc->oc_stateowner = NULL;
	down(&client_sema); /* XXX need finer grained locking */

	if ((status = nfs4_preprocess_seqid_op(current_fh, oc->oc_seqid,
					&oc->oc_req_stateid,
					CHECK_FH | CONFIRM,
					&oc->oc_stateowner, &stp)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	/* XXX renew the client lease here */
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);
	status = nfs_ok;
out:
	up(&client_sema);
	return status;
}
int
nfsd4_open_downgrade(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open_downgrade *od)
{
	int status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			current_fh->fh_dentry->d_name.len, 
			current_fh->fh_dentry->d_name.name);

	down(&client_sema); /* XXX need finer grained locking */
	if ((status = nfs4_preprocess_seqid_op(current_fh, od->od_seqid, 
					&od->od_stateid, 
					CHECK_FH, &od->od_stateowner, &stp)))
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
	up(&client_sema);
	return status;
}

int
nfsd4_close(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_close *close)
{
	int status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			current_fh->fh_dentry->d_name.len, 
			current_fh->fh_dentry->d_name.name);

	down(&client_sema); /* XXX need finer grained locking */
	if ((status = nfs4_preprocess_seqid_op(current_fh, close->cl_seqid, 
					&close->cl_stateid, 
					CHECK_FH, 
					&close->cl_stateowner, &stp)))
		goto out; 
	/*
	*  Return success, but first update the stateid.
	*/
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_open_state() calls nfsd_close() if needed */
	release_open_state(stp);
out:
	up(&client_sema);
	return status;
}

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
	for (i = 0; i < OPENSTATEID_HASH_SIZE; i++) {
		INIT_LIST_HEAD(&openstateid_hashtbl[i]);
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
	dprintk("NFSD: alloc_sowner %d free_sowner %d\n",
			alloc_sowner, free_sowner);
	dprintk("NFSD: vfsopen %d vfsclose %d\n",
			vfsopen, vfsclose);
}

void
nfs4_state_shutdown(void)
{
	down(&client_sema);
	__nfs4_state_shutdown();
	up(&client_sema);
}
