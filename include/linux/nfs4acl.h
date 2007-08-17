#ifndef __NFS4ACL_H
#define __NFS4ACL_H

struct nfs4ace {
	unsigned short	e_type;
	unsigned short	e_flags;
	unsigned int	e_mask;
	union {
		unsigned int	e_id;
		const char	*e_who;
	} u;
};

struct nfs4acl {
	atomic_t	a_refcount;
	unsigned int	a_owner_mask;
	unsigned int	a_group_mask;
	unsigned int	a_other_mask;
	unsigned int	a_count;
	struct nfs4ace	a_entries[0];
};

#define nfs4acl_for_each_entry(_ace, _acl) \
	for (_ace = _acl->a_entries; \
	     _ace != _acl->a_entries + _acl->a_count; \
	     _ace++)

#define nfs4acl_for_each_entry_reverse(_ace, _acl) \
	for (_ace = _acl->a_entries + _acl->a_count - 1; \
	     _ace != _acl->a_entries - 1; \
	     _ace--)

/* e_type values */
#define ACE4_ACCESS_ALLOWED_ACE_TYPE	0x0000
#define ACE4_ACCESS_DENIED_ACE_TYPE	0x0001
/*#define ACE4_SYSTEM_AUDIT_ACE_TYPE	0x0002*/
/*#define ACE4_SYSTEM_ALARM_ACE_TYPE	0x0003*/

/* e_flags bitflags */
#define ACE4_FILE_INHERIT_ACE		0x0001
#define ACE4_DIRECTORY_INHERIT_ACE	0x0002
#define ACE4_NO_PROPAGATE_INHERIT_ACE	0x0004
#define ACE4_INHERIT_ONLY_ACE		0x0008
/*#define ACE4_SUCCESSFUL_ACCESS_ACE_FLAG	0x0010*/
/*#define ACE4_FAILED_ACCESS_ACE_FLAG	0x0020*/
#define ACE4_IDENTIFIER_GROUP		0x0040
#define ACE4_SPECIAL_WHO		0x4000  /* in-kernel only */

#define ACE4_VALID_FLAGS ( \
	ACE4_FILE_INHERIT_ACE | \
	ACE4_DIRECTORY_INHERIT_ACE | \
	ACE4_NO_PROPAGATE_INHERIT_ACE | \
	ACE4_INHERIT_ONLY_ACE | \
	ACE4_IDENTIFIER_GROUP )

/* e_mask bitflags */
#define ACE4_READ_DATA			0x00000001
#define ACE4_LIST_DIRECTORY		0x00000001
#define ACE4_WRITE_DATA			0x00000002
#define ACE4_ADD_FILE			0x00000002
#define ACE4_APPEND_DATA		0x00000004
#define ACE4_ADD_SUBDIRECTORY		0x00000004
#define ACE4_READ_NAMED_ATTRS		0x00000008
#define ACE4_WRITE_NAMED_ATTRS		0x00000010
#define ACE4_EXECUTE			0x00000020
#define ACE4_DELETE_CHILD		0x00000040
#define ACE4_READ_ATTRIBUTES		0x00000080
#define ACE4_WRITE_ATTRIBUTES		0x00000100
#define ACE4_DELETE			0x00010000
#define ACE4_READ_ACL			0x00020000
#define ACE4_WRITE_ACL			0x00040000
#define ACE4_WRITE_OWNER		0x00080000
#define ACE4_SYNCHRONIZE		0x00100000

#define ACE4_VALID_MASK ( \
	ACE4_READ_DATA | \
	ACE4_LIST_DIRECTORY | \
	ACE4_WRITE_DATA | \
	ACE4_ADD_FILE | \
	ACE4_APPEND_DATA | \
	ACE4_ADD_SUBDIRECTORY | \
	ACE4_READ_NAMED_ATTRS | \
	ACE4_WRITE_NAMED_ATTRS | \
	ACE4_EXECUTE | \
	ACE4_DELETE_CHILD | \
	ACE4_READ_ATTRIBUTES | \
	ACE4_WRITE_ATTRIBUTES | \
	ACE4_DELETE | \
	ACE4_READ_ACL | \
	ACE4_WRITE_ACL | \
	ACE4_WRITE_OWNER | \
	ACE4_SYNCHRONIZE )

/*
 * Duplicate an NFS4ACL handle.
 */
static inline struct nfs4acl *
nfs4acl_dup(struct nfs4acl *acl)
{
	if (acl)
		atomic_inc(&acl->a_refcount);
	return acl;
}

/*
 * Free an NFS4ACL handle
 */
static inline void
nfs4acl_release(struct nfs4acl *acl)
{
	if (acl && atomic_dec_and_test(&acl->a_refcount))
		kfree(acl);
}

/* Special e_who identifiers: we use these pointer values in comparisons
   instead of strcmp for efficiency. */

extern const char *nfs4ace_owner_who;
extern const char *nfs4ace_group_who;
extern const char *nfs4ace_everyone_who;

static inline int
nfs4ace_is_owner(const struct nfs4ace *ace)
{
	return (ace->e_flags & ACE4_SPECIAL_WHO) &&
	       ace->u.e_who == nfs4ace_owner_who;
}

static inline int
nfs4ace_is_group(const struct nfs4ace *ace)
{
	return (ace->e_flags & ACE4_SPECIAL_WHO) &&
	       ace->u.e_who == nfs4ace_group_who;
}

static inline int
nfs4ace_is_everyone(const struct nfs4ace *ace)
{
	return (ace->e_flags & ACE4_SPECIAL_WHO) &&
	       ace->u.e_who == nfs4ace_everyone_who;
}

static inline int
nfs4ace_is_unix_id(const struct nfs4ace *ace)
{
	return !(ace->e_flags & ACE4_SPECIAL_WHO);
}

static inline int
nfs4ace_is_inherit_only(const struct nfs4ace *ace)
{
	return ace->e_flags & ACE4_INHERIT_ONLY_ACE;
}

static inline int
nfs4ace_is_inheritable(const struct nfs4ace *ace)
{
	return ace->e_flags & (ACE4_FILE_INHERIT_ACE |
			       ACE4_DIRECTORY_INHERIT_ACE);
}

static inline void
nfs4ace_clear_inheritance_flags(struct nfs4ace *ace)
{
	ace->e_flags &= ~(ACE4_FILE_INHERIT_ACE |
			  ACE4_DIRECTORY_INHERIT_ACE |
			  ACE4_NO_PROPAGATE_INHERIT_ACE |
			  ACE4_INHERIT_ONLY_ACE);
}

static inline int
nfs4ace_is_allow(const struct nfs4ace *ace)
{
	return ace->e_type == ACE4_ACCESS_ALLOWED_ACE_TYPE;
}

static inline int
nfs4ace_is_deny(const struct nfs4ace *ace)
{
	return ace->e_type == ACE4_ACCESS_DENIED_ACE_TYPE;
}

extern struct nfs4acl *nfs4acl_alloc(int count);
extern struct nfs4acl *nfs4acl_clone(const struct nfs4acl *acl);

extern int nfs4acl_permission(struct inode *, const struct nfs4acl *, int, int);
extern int nfs4ace_is_same_who(const struct nfs4ace *, const struct nfs4ace *);
extern struct nfs4acl *nfs4acl_inherit(const struct nfs4acl *, mode_t, int);
extern int nfs4acl_masks_to_mode(const struct nfs4acl *);
extern struct nfs4acl *nfs4acl_chmod(struct nfs4acl *, mode_t);
extern int nfs4acl_apply_masks(struct nfs4acl **acl, int);

#endif /* __NFS4ACL_H */
