/*
 *  arch/s390/kernel/s390_ext.c
 *
 *  S390 version
 *    Copyright (C) 1999,2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Holger Smolinski (Holger.Smolinski@de.ibm.com),
 *               Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/lowcore.h>
#include <asm/s390_ext.h>

/*
 * Simple hash strategy: index = code & 0xff;
 * ext_int_hash[index] is the start of the list for all external interrupts
 * that hash to this index. With the current set of external interrupts 
 * (0x1202 external call, 0x1004 cpu timer, 0x2401 hwc console, 0x4000
 * iucv and 0x2603 pfault) this is always the first element. 
 */
ext_int_info_t *ext_int_hash[256] = { 0, };
ext_int_info_t ext_int_info_timer;
ext_int_info_t ext_int_info_hwc;
ext_int_info_t ext_int_pfault;

int register_external_interrupt(__u16 code, ext_int_handler_t handler) {
        ext_int_info_t *p;
        int index;

        index = code & 0xff;
        p = ext_int_hash[index];
        while (p != NULL) {
                if (p->code == code)
                        return -EBUSY;
                p = p->next;
        }
        if (code == 0x1004) /* time_init is done before kmalloc works :-/ */
                p = &ext_int_info_timer;
        else if (code == 0x2401) /* hwc_init is done too early too */
                p = &ext_int_info_hwc;
        else if (code == 0x2603) /* pfault_init is done too early too */
		p = &ext_int_pfault;
	else
                p = (ext_int_info_t *)
                          kmalloc(sizeof(ext_int_info_t), GFP_ATOMIC);
        if (p == NULL)
                return -ENOMEM;
        p->code = code;
        p->handler = handler;
        p->next = ext_int_hash[index];
        ext_int_hash[index] = p;
        return 0;
}

int unregister_external_interrupt(__u16 code, ext_int_handler_t handler) {
        ext_int_info_t *p, *q;
        int index;

        index = code & 0xff;
        q = NULL;
        p = ext_int_hash[index];
        while (p != NULL) {
                if (p->code == code && p->handler == handler)
                        break;
                q = p;
                p = p->next;
        }
        if (p == NULL)
                return -ENOENT;
        if (q != NULL)
                q->next = p->next;
        else
                ext_int_hash[index] = p->next;
        if (code != 0x1004 && code != 0x2401 && code != 0x2603)
                kfree(p);
        return 0;
}

EXPORT_SYMBOL(register_external_interrupt);
EXPORT_SYMBOL(unregister_external_interrupt);

