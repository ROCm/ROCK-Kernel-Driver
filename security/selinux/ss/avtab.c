/*
 * Implementation of the access vector table type.
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#include "avtab.h"
#include "policydb.h"

#define AVTAB_HASH(keyp) \
((keyp->target_class + \
 (keyp->target_type << 2) + \
 (keyp->source_type << 9)) & \
 AVTAB_HASH_MASK)

int avtab_insert(struct avtab *h, struct avtab_key *key, struct avtab_datum *datum)
{
	int hvalue;
	struct avtab_node *prev, *cur, *newnode;

	if (!h)
		return -EINVAL;

	hvalue = AVTAB_HASH(key);
	for (prev = NULL, cur = h->htable[hvalue];
	     cur;
	     prev = cur, cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (datum->specified & cur->datum.specified))
			return -EEXIST;
		if (key->source_type < cur->key.source_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type < cur->key.target_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class < cur->key.target_class)
			break;
	}

	newnode = kmalloc(sizeof(*newnode), GFP_KERNEL);
	if (newnode == NULL)
		return -ENOMEM;
	memset(newnode, 0, sizeof(*newnode));
	newnode->key = *key;
	newnode->datum = *datum;
	if (prev) {
		newnode->next = prev->next;
		prev->next = newnode;
	} else {
		newnode->next = h->htable[hvalue];
		h->htable[hvalue] = newnode;
	}

	h->nel++;
	return 0;
}


struct avtab_datum *avtab_search(struct avtab *h, struct avtab_key *key, int specified)
{
	int hvalue;
	struct avtab_node *cur;

	if (!h)
		return NULL;

	hvalue = AVTAB_HASH(key);
	for (cur = h->htable[hvalue]; cur; cur = cur->next) {
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class == cur->key.target_class &&
		    (specified & cur->datum.specified))
			return &cur->datum;

		if (key->source_type < cur->key.source_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type < cur->key.target_type)
			break;
		if (key->source_type == cur->key.source_type &&
		    key->target_type == cur->key.target_type &&
		    key->target_class < cur->key.target_class)
			break;
	}

	return NULL;
}

void avtab_destroy(struct avtab *h)
{
	int i;
	struct avtab_node *cur, *temp;

	if (!h)
		return;

	for (i = 0; i < AVTAB_SIZE; i++) {
		cur = h->htable[i];
		while (cur != NULL) {
			temp = cur;
			cur = cur->next;
			kfree(temp);
		}
		h->htable[i] = NULL;
	}
	vfree(h->htable);
}


int avtab_map(struct avtab *h,
	      int (*apply) (struct avtab_key *k,
			    struct avtab_datum *d,
			    void *args),
	      void *args)
{
	int i, ret;
	struct avtab_node *cur;

	if (!h)
		return 0;

	for (i = 0; i < AVTAB_SIZE; i++) {
		cur = h->htable[i];
		while (cur != NULL) {
			ret = apply(&cur->key, &cur->datum, args);
			if (ret)
				return ret;
			cur = cur->next;
		}
	}
	return 0;
}

int avtab_init(struct avtab *h)
{
	int i;

	h->htable = vmalloc(sizeof(*(h->htable)) * AVTAB_SIZE);
	if (!h->htable)
		return -ENOMEM;
	for (i = 0; i < AVTAB_SIZE; i++)
		h->htable[i] = NULL;
	h->nel = 0;
	return 0;
}

void avtab_hash_eval(struct avtab *h, char *tag)
{
	int i, chain_len, slots_used, max_chain_len;
	struct avtab_node *cur;

	slots_used = 0;
	max_chain_len = 0;
	for (i = 0; i < AVTAB_SIZE; i++) {
		cur = h->htable[i];
		if (cur) {
			slots_used++;
			chain_len = 0;
			while (cur) {
				chain_len++;
				cur = cur->next;
			}

			if (chain_len > max_chain_len)
				max_chain_len = chain_len;
		}
	}

	printk(KERN_INFO "%s:  %d entries and %d/%d buckets used, longest "
	       "chain length %d\n", tag, h->nel, slots_used, AVTAB_SIZE,
	       max_chain_len);
}

int avtab_read(struct avtab *a, void *fp, u32 config)
{
	int i, rc = -EINVAL;
	struct avtab_key avkey;
	struct avtab_datum avdatum;
	u32 *buf;
	u32 nel, items, items2;


	buf = next_entry(fp, sizeof(u32));
	if (!buf) {
		printk(KERN_ERR "security: avtab: truncated table\n");
		goto bad;
	}
	nel = le32_to_cpu(buf[0]);
	if (!nel) {
		printk(KERN_ERR "security: avtab: table is empty\n");
		goto bad;
	}
	for (i = 0; i < nel; i++) {
		memset(&avkey, 0, sizeof(avkey));
		memset(&avdatum, 0, sizeof(avdatum));

		buf = next_entry(fp, sizeof(u32));
		if (!buf) {
			printk(KERN_ERR "security: avtab: truncated entry\n");
			goto bad;
		}
		items2 = le32_to_cpu(buf[0]);
		buf = next_entry(fp, sizeof(u32)*items2);
		if (!buf) {
			printk(KERN_ERR "security: avtab: truncated entry\n");
			goto bad;
		}
		items = 0;
		avkey.source_type = le32_to_cpu(buf[items++]);
		avkey.target_type = le32_to_cpu(buf[items++]);
		avkey.target_class = le32_to_cpu(buf[items++]);
		avdatum.specified = le32_to_cpu(buf[items++]);
		if (!(avdatum.specified & (AVTAB_AV | AVTAB_TYPE))) {
			printk(KERN_ERR "security: avtab: null entry\n");
			goto bad;
		}
		if ((avdatum.specified & AVTAB_AV) &&
		    (avdatum.specified & AVTAB_TYPE)) {
			printk(KERN_ERR "security: avtab: entry has both "
			       "access vectors and types\n");
			goto bad;
		}
		if (avdatum.specified & AVTAB_AV) {
			if (avdatum.specified & AVTAB_ALLOWED)
				avtab_allowed(&avdatum) = le32_to_cpu(buf[items++]);
			if (avdatum.specified & AVTAB_AUDITDENY)
				avtab_auditdeny(&avdatum) = le32_to_cpu(buf[items++]);
			if (avdatum.specified & AVTAB_AUDITALLOW)
				avtab_auditallow(&avdatum) = le32_to_cpu(buf[items++]);
		} else {
			if (avdatum.specified & AVTAB_TRANSITION)
				avtab_transition(&avdatum) = le32_to_cpu(buf[items++]);
			if (avdatum.specified & AVTAB_CHANGE)
				avtab_change(&avdatum) = le32_to_cpu(buf[items++]);
			if (avdatum.specified & AVTAB_MEMBER)
				avtab_member(&avdatum) = le32_to_cpu(buf[items++]);
		}
		if (items != items2) {
			printk(KERN_ERR "security: avtab: entry only had %d "
			       "items, expected %d\n", items2, items);
			goto bad;
		}
		rc = avtab_insert(a, &avkey, &avdatum);
		if (rc) {
			if (rc == -ENOMEM)
				printk(KERN_ERR "security: avtab: out of memory\n");
			if (rc == -EEXIST)
				printk(KERN_ERR "security: avtab: duplicate entry\n");
			goto bad;
		}
	}

	rc = 0;
out:
	return rc;

bad:
	avtab_destroy(a);
	goto out;
}

