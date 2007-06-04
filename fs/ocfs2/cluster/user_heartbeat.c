
/* The userspace cluster heartbeat directory will be populated with symbolic
 * links to the nodes who are heartbeating in the given group */

#include <linux/configfs.h>
#include "heartbeat.h"
#include "tcp.h"
#include "nodemanager.h"

struct o2hb_user_region {
	struct o2hb_heartbeat_resource hr_res;
	unsigned long hr_live_bitmap[BITS_TO_LONGS(O2NM_MAX_NODES)];
};

static inline struct o2hb_user_region *to_o2hb_user_region(struct o2hb_heartbeat_resource *hbres)
{
	return container_of(hbres, struct o2hb_user_region, hr_res);
}

static inline struct o2hb_user_region *item_to_o2hb_user_region(struct config_item *item)
{
	return to_o2hb_user_region(to_o2hb_heartbeat_resource(item));
}

static inline void o2hb_user_region_get(struct o2hb_user_region *region)
{
	o2hb_heartbeat_resource_get(&region->hr_res);
}

static inline void o2hb_user_region_put(struct o2hb_user_region *region)
{
	o2hb_heartbeat_resource_put(&region->hr_res);
}
static int o2hb_user_group_allow_link(struct config_item *src,
                                      struct config_item *target)
{
	struct o2nm_node *node = to_o2nm_node(target);
	struct o2hb_user_region *hbr = item_to_o2hb_user_region(src);

	struct o2hb_node_event event = {
		.hn_item = LIST_HEAD_INIT(event.hn_item),
		.hn_res = &hbr->hr_res,
	};

	/* Make sure the target is within the same cluster */
	if (src->ci_parent->ci_parent != target->ci_parent->ci_parent)
		return -EPERM;

	printk ("Node %s is up in group %s\n", target->ci_name, src->ci_name);

	spin_lock(&o2hb_live_lock);
	set_bit(node->nd_num, hbr->hr_live_bitmap);

	/* also add a link back to the node */

	/* Notify listeners that this node is up */
	o2hb_queue_node_event(&event, O2HB_NODE_UP_CB, node, node->nd_num);
	spin_unlock(&o2hb_live_lock);

	o2hb_run_event_list (&event);

	return 0;
}

static int o2hb_user_group_drop_link(struct config_item *src,
                                     struct config_item *target)
{
	struct o2nm_node *node = to_o2nm_node(target);
	struct o2hb_user_region *hbr = item_to_o2hb_user_region(src);

	struct o2hb_node_event event = {
		.hn_item = LIST_HEAD_INIT(event.hn_item),
		.hn_res = &hbr->hr_res,
	};

	/* Somehow notify listeners that this node is down */
	printk ("Node %s is down in group %s\n", target->ci_name,
	                                         src->ci_name);

	spin_lock(&o2hb_live_lock);
	clear_bit(node->nd_num, hbr->hr_live_bitmap);

	/* Notify listeners that this node is down */
	o2hb_queue_node_event (&event, O2HB_NODE_DOWN_CB, node, node->nd_num);
	spin_unlock(&o2hb_live_lock);

	o2hb_run_event_list (&event);

	return 0;
}

static struct configfs_item_operations o2hb_user_region_ops = {
	.allow_link	= o2hb_user_group_allow_link,
	.drop_link	= o2hb_user_group_drop_link,
};

struct config_item_type o2hb_user_region_type = {
	.ct_item_ops	= &o2hb_user_region_ops,
	.ct_owner	= THIS_MODULE,
};

/* How to create a heartbeat region */
static struct config_item *o2hb_heartbeat_group_make_item(struct config_group *group,
                                                          const char *name)
{
	struct o2hb_user_region *region;
	struct config_item *ret = NULL;

	region = kzalloc(sizeof (*region), GFP_KERNEL);
	if (region == NULL)
		goto out;

	/* mkdir <fs uuid> */
	config_item_init_type_name(&region->hr_res.hr_item, name,
	                            &o2hb_user_region_type);

	ret = &region->hr_res.hr_item;

out:
	if (ret == NULL)
		kfree(region);
	return ret;
}

/* How to remove a heartbeat region */
static void o2hb_heartbeat_group_drop_item(struct config_group *group,
                                           struct config_item *item)
{
	config_item_put (item);
}

static struct configfs_group_operations o2hb_user_heartbeat_root_ops = {
	.make_item	= o2hb_heartbeat_group_make_item,
	.drop_item	= o2hb_heartbeat_group_drop_item,
};

static inline struct o2hb_user_region *o2hb_user_region_get_by_name(const char *resource)
{
	struct o2hb_heartbeat_resource *hbr;
	struct o2hb_user_region *region = NULL;

	hbr = o2hb_heartbeat_resource_get_by_name(resource);
	if (hbr != NULL)
		region = to_o2hb_user_region(hbr);

	return region;
}

static int o2hb_user_fill_node_map(const char *resource, unsigned long *map,
                                   size_t len)
{
	struct o2hb_user_region *region;
	if (resource == NULL)
		return -EINVAL;

	if (len > BITS_TO_LONGS(O2NM_MAX_NODES) * sizeof (*map))
		return -EFAULT;

	region = o2hb_user_region_get_by_name(resource);
	if (region == NULL)
		return -ENOENT;

	memcpy (map, region->hr_live_bitmap, len);

	o2hb_user_region_put(region);

	return 0;
}

static int o2hb_user_check_node_status(const char *resource, u8 node_num)
{
	int ret = 0;

	if (resource) {
		struct o2hb_user_region *region;
		region = o2hb_user_region_get_by_name(resource);
		if (region == NULL)
			goto out;

		ret = test_bit(node_num, region->hr_live_bitmap);
		o2hb_user_region_put(region);
	} else {
		struct o2nm_node *node = o2nm_get_node_by_num(node_num);
		ret = (node && atomic_read(&node->nd_count));
	}
out:
	return ret;
}

static struct o2hb_heartbeat_group user_heartbeat_group = {
	.hs_type = {
		.ct_group_ops	= &o2hb_user_heartbeat_root_ops,
		.ct_owner	= THIS_MODULE,
	},
	.hs_name		= "user",
	.fill_node_map		= o2hb_user_fill_node_map,
	.check_node_status	= o2hb_user_check_node_status,
};

static int __init o2hb_user_heartbeat_init(void)
{
	return o2hb_register_heartbeat_group(&user_heartbeat_group);
}

static void __exit o2hb_user_heartbeat_exit(void)
{
	o2hb_unregister_heartbeat_group(&user_heartbeat_group);
}

MODULE_AUTHOR("Novell");
MODULE_LICENSE("GPL");

module_init(o2hb_user_heartbeat_init);
module_exit(o2hb_user_heartbeat_exit);
