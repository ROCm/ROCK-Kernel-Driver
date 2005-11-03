/*
 * Manager for various I/O types.
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iotype.h"
#include "iscsi_dbg.h"

static LIST_HEAD(iotypes);
static rwlock_t iotypes_lock = RW_LOCK_UNLOCKED;

static struct iotype *find_iotype(const char *name)
{
	struct iotype *iot = NULL;

	list_for_each_entry(iot, &iotypes, iot_list) {
		if (strcmp(iot->name, name) == 0)
			return iot;
	}
	return NULL;
}

struct iotype *get_iotype(const char *name)
{
	struct iotype *iot;

	read_lock(&iotypes_lock);
	iot = find_iotype(name);
	read_unlock(&iotypes_lock);

	return iot;
}

void put_iotype(struct iotype *iot)
{
	if (!iot)
		return;
	return;
}

static int register_iotype(struct iotype *iot)
{
	int err = 0;
	struct iotype *p;

	write_lock(&iotypes_lock);

	if ((p = find_iotype(iot->name)))
		err = -EBUSY;
	else
		list_add_tail(&iot->iot_list, &iotypes);

	write_unlock(&iotypes_lock);

	return err;
}

static int unregister_iotype(struct iotype *iot)
{
	int err = 0;
	struct iotype *p;

	write_lock(&iotypes_lock);

	p = find_iotype(iot->name);
	if (p && p == iot)
		list_del_init(&iot->iot_list);
	else
		err = -EINVAL;

	write_unlock(&iotypes_lock);


	return err;
}

struct iotype *iotype_array[] = {
	&fileio,
	&nullio,
};

int iotype_init(void)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(iotype_array); i++) {
		if (!(err = register_iotype(iotype_array[i])))
			eprintk("register %s\n", iotype_array[i]->name);
		else {
			eprintk("failed to register %s\n", iotype_array[i]->name);
			break;
		}
	}

	return err;
}

void iotype_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(iotype_array); i++)
		unregister_iotype(iotype_array[i]);
}
