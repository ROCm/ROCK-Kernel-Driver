#include <net/xfrm.h>
#include <net/ip.h>

static struct xfrm_type *xfrm6_type_map[256];
static rwlock_t xfrm6_type_lock = RW_LOCK_UNLOCKED;

int xfrm6_register_type(struct xfrm_type *type)
{
	int err = 0;

	write_lock(&xfrm6_type_lock);
	if (xfrm6_type_map[type->proto] == NULL)
		xfrm6_type_map[type->proto] = type;
	else
		err = -EEXIST;
	write_unlock(&xfrm6_type_lock);
	return err;
}

int xfrm6_unregister_type(struct xfrm_type *type)
{
	int err = 0;

	write_lock(&xfrm6_type_lock);
	if (xfrm6_type_map[type->proto] != type)
		err = -ENOENT;
	else
		xfrm6_type_map[type->proto] = NULL;
	write_unlock(&xfrm6_type_lock);
	return err;
}

struct xfrm_type *xfrm6_get_type(u8 proto)
{
	struct xfrm_type *type;

	read_lock(&xfrm6_type_lock);
	type = xfrm6_type_map[proto];
	if (type && !try_module_get(type->owner))
		type = NULL;
	read_unlock(&xfrm6_type_lock);
	return type;
}
