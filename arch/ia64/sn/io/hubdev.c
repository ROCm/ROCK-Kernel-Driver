/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/sn/sgi.h>
#include <asm/sn/iograph.h>
#include <asm/sn/sn1/hubdev.h>
#include <asm/sn/sn_private.h>
#include <asm/sn/invent.h>
#include <asm/sn/hcl.h>
#include <asm/sn/labelcl.h>

struct hubdev_callout {
        int (*attach_method)(devfs_handle_t);
        struct hubdev_callout *fp;
};

typedef struct hubdev_callout hubdev_callout_t;

mutex_t hubdev_callout_mutex;
hubdev_callout_t *hubdev_callout_list = NULL;

void
hubdev_init(void)
{
	mutex_init(&hubdev_callout_mutex, MUTEX_DEFAULT, "hubdev");
        hubdev_callout_list = NULL;
}
        
void
hubdev_register(int (*attach_method)(devfs_handle_t))
{
        hubdev_callout_t *callout;
        
        ASSERT(attach_method);

        callout =  (hubdev_callout_t *)kmem_zalloc(sizeof(hubdev_callout_t), KM_SLEEP);
        ASSERT(callout);
        
	mutex_lock(&hubdev_callout_mutex, PZERO);
        /*
         * Insert at the front of the list
         */
        callout->fp = hubdev_callout_list;
        hubdev_callout_list = callout;
        callout->attach_method = attach_method;
	mutex_unlock(&hubdev_callout_mutex);
}

int
hubdev_unregister(int (*attach_method)(devfs_handle_t))
{
        hubdev_callout_t **p;
        
        ASSERT(attach_method);
   
	mutex_lock(&hubdev_callout_mutex, PZERO);
        /*
         * Remove registry element containing attach_method
         */
        for (p = &hubdev_callout_list; *p != NULL; p = &(*p)->fp) {
                if ((*p)->attach_method == attach_method) {
                        hubdev_callout_t* victim = *p;
                        *p = (*p)->fp;
                        kfree(victim);
                        mutex_unlock(&hubdev_callout_mutex);
                        return (0);
                }
        }
        mutex_unlock(&hubdev_callout_mutex);
        return (ENOENT);
}


int
hubdev_docallouts(devfs_handle_t hub)
{
        hubdev_callout_t *p;
        int errcode;

	mutex_lock(&hubdev_callout_mutex, PZERO);
        
        for (p = hubdev_callout_list; p != NULL; p = p->fp) {
                ASSERT(p->attach_method);
                errcode = (*p->attach_method)(hub);
                if (errcode != 0) {
			mutex_unlock(&hubdev_callout_mutex);
                        return (errcode);
                }
        }
        mutex_unlock(&hubdev_callout_mutex);
        return (0);
}

/*
 * Given a hub vertex, return the base address of the Hspec space
 * for that hub.
 */
caddr_t
hubdev_prombase_get(devfs_handle_t hub)
{
	hubinfo_t	hinfo = NULL;

	hubinfo_get(hub, &hinfo);
	ASSERT(hinfo);

	return ((caddr_t)NODE_RBOOT_BASE(hinfo->h_nasid));
}

cnodeid_t
hubdev_cnodeid_get(devfs_handle_t hub)
{
	hubinfo_t	hinfo = NULL;
	hubinfo_get(hub, &hinfo);
	ASSERT(hinfo);

	return hinfo->h_cnodeid;
}
