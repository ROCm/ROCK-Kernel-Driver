
/*
 * map between user/group name and id for a given 'client' 
 */

struct name_ent {
	char name[20];
};
static inline int name_get_user(int uid, struct name_ent **namep)
{
	struct name_ent *n = kmalloc(sizeof(*n),GFP_KERNEL);
	if (n) sprintf(n->name, "%d",uid);
	*namep = n;
	return n ? 0 : -ENOMEM;
}
static inline int name_get_group(int uid, struct name_ent **namep)
{
	struct name_ent *n = kmalloc(sizeof(*n),GFP_KERNEL);
	if (n) sprintf(n->name, "%d",uid);
	*namep = n;
	return n ? 0 : -ENOMEM;
}
static inline int name_get_uid(char *name, int name_len, int *uidp)
{
	*uidp = simple_strtoul(name, NULL, 0);
	return 0;
}

static inline int name_get_gid(char *name, int name_len, int *gidp)
{
	*gidp = simple_strtoul(name, NULL, 0);
	return 0;
}

static inline void name_put(struct name_ent *ent) 
{
	kfree(ent);
}
