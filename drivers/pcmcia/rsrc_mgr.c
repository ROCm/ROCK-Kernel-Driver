/*======================================================================

    Resource management routines

    rsrc_mgr.c 1.79 2000/08/30 20:23:58

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/ss.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include "cs_internal.h"

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0444)

INT_MODULE_PARM(probe_mem,	1);		/* memory probe? */
#ifdef CONFIG_PCMCIA_PROBE
INT_MODULE_PARM(probe_io,	1);		/* IO port probe? */
INT_MODULE_PARM(mem_limit,	0x10000);
#endif

/*======================================================================

    The resource_map_t structures are used to track what resources are
    available for allocation for PC Card devices.

======================================================================*/

typedef struct resource_map_t {
    u_long			base, num;
    struct resource_map_t	*next;
} resource_map_t;

/* Memory resource database */
static resource_map_t mem_db = {
	.next	= &mem_db,
};

/* IO port resource database */
static resource_map_t io_db = {
	.next	= &io_db,
};

static DECLARE_MUTEX(rsrc_sem);

#ifdef CONFIG_PCMCIA_PROBE

typedef struct irq_info_t {
    u_int			Attributes;
    int				time_share, dyn_share;
    struct pcmcia_socket	*Socket;
} irq_info_t;

/* Table of IRQ assignments */
static irq_info_t irq_table[NR_IRQS];

#endif

/*======================================================================

    Linux resource management extensions

======================================================================*/

static struct resource *
make_resource(unsigned long b, unsigned long n, int flags, char *name)
{
	struct resource *res = kmalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		memset(res, 0, sizeof(*res));
		res->name = name;
		res->start = b;
		res->end = b + n - 1;
		res->flags = flags | IORESOURCE_BUSY;
	}
	return res;
}

static struct resource *
claim_region(struct pcmcia_socket *s, unsigned long base, unsigned long size,
	     int type, char *name)
{
	struct resource *res, *parent;

	parent = type & IORESOURCE_MEM ? &iomem_resource : &ioport_resource;
	res = make_resource(base, size, type | IORESOURCE_BUSY, name);

	if (res) {
#ifdef CONFIG_PCI
		if (s && s->cb_dev)
			parent = pci_find_parent_resource(s->cb_dev, res);
#endif
		if (!parent || request_resource(parent, res)) {
			kfree(res);
			res = NULL;
		}
	}
	return res;
}

static void free_region(struct resource *res)
{
	if (res) {
		release_resource(res);
		kfree(res);
	}
}

/*======================================================================

    These manage the internal databases of available resources.
    
======================================================================*/

static int add_interval(resource_map_t *map, u_long base, u_long num)
{
    resource_map_t *p, *q;

    for (p = map; ; p = p->next) {
	if ((p != map) && (p->base+p->num-1 >= base))
	    return -1;
	if ((p->next == map) || (p->next->base > base+num-1))
	    break;
    }
    q = kmalloc(sizeof(resource_map_t), GFP_KERNEL);
    if (!q) return CS_OUT_OF_RESOURCE;
    q->base = base; q->num = num;
    q->next = p->next; p->next = q;
    return CS_SUCCESS;
}

/*====================================================================*/

static int sub_interval(resource_map_t *map, u_long base, u_long num)
{
    resource_map_t *p, *q;

    for (p = map; ; p = q) {
	q = p->next;
	if (q == map)
	    break;
	if ((q->base+q->num > base) && (base+num > q->base)) {
	    if (q->base >= base) {
		if (q->base+q->num <= base+num) {
		    /* Delete whole block */
		    p->next = q->next;
		    kfree(q);
		    /* don't advance the pointer yet */
		    q = p;
		} else {
		    /* Cut off bit from the front */
		    q->num = q->base + q->num - base - num;
		    q->base = base + num;
		}
	    } else if (q->base+q->num <= base+num) {
		/* Cut off bit from the end */
		q->num = base - q->base;
	    } else {
		/* Split the block into two pieces */
		p = kmalloc(sizeof(resource_map_t), GFP_KERNEL);
		if (!p) return CS_OUT_OF_RESOURCE;
		p->base = base+num;
		p->num = q->base+q->num - p->base;
		q->num = base - q->base;
		p->next = q->next ; q->next = p;
	    }
	}
    }
    return CS_SUCCESS;
}

/*======================================================================

    These routines examine a region of IO or memory addresses to
    determine what ranges might be genuinely available.
    
======================================================================*/

#ifdef CONFIG_PCMCIA_PROBE
static void do_io_probe(ioaddr_t base, ioaddr_t num)
{
    struct resource *res;
    ioaddr_t i, j, bad, any;
    u_char *b, hole, most;
    
    printk(KERN_INFO "cs: IO port probe 0x%04x-0x%04x:",
	   base, base+num-1);
    
    /* First, what does a floating port look like? */
    b = kmalloc(256, GFP_KERNEL);
    if (!b) {
            printk(KERN_ERR "do_io_probe: unable to kmalloc 256 bytes");
            return;
    }   
    memset(b, 0, 256);
    for (i = base, most = 0; i < base+num; i += 8) {
	res = claim_region(NULL, i, 8, IORESOURCE_IO, "PCMCIA IO probe");
	if (!res)
	    continue;
	hole = inb(i);
	for (j = 1; j < 8; j++)
	    if (inb(i+j) != hole) break;
	free_region(res);
	if ((j == 8) && (++b[hole] > b[most]))
	    most = hole;
	if (b[most] == 127) break;
    }
    kfree(b);

    bad = any = 0;
    for (i = base; i < base+num; i += 8) {
	res = claim_region(NULL, i, 8, IORESOURCE_IO, "PCMCIA IO probe");
	if (!res)
	    continue;
	for (j = 0; j < 8; j++)
	    if (inb(i+j) != most) break;
	free_region(res);
	if (j < 8) {
	    if (!any)
		printk(" excluding");
	    if (!bad)
		bad = any = i;
	} else {
	    if (bad) {
		sub_interval(&io_db, bad, i-bad);
		printk(" %#04x-%#04x", bad, i-1);
		bad = 0;
	    }
	}
    }
    if (bad) {
	if ((num > 16) && (bad == base) && (i == base+num)) {
	    printk(" nothing: probe failed.\n");
	    return;
	} else {
	    sub_interval(&io_db, bad, i-bad);
	    printk(" %#04x-%#04x", bad, i-1);
	}
    }
    
    printk(any ? "\n" : " clean.\n");
}
#endif

/*======================================================================

    This is tricky... when we set up CIS memory, we try to validate
    the memory window space allocations.
    
======================================================================*/

/* Validation function for cards with a valid CIS */
static int readable(struct pcmcia_socket *s, struct resource *res, cisinfo_t *info)
{
	int ret = -1;

	s->cis_mem.sys_start = res->start;
	s->cis_mem.sys_stop = res->end;
	s->cis_virt = ioremap(res->start, s->map_size);
	if (s->cis_virt) {
		ret = pcmcia_validate_cis(s->clients, info);
		/* invalidate mapping and CIS cache */
		iounmap(s->cis_virt);
		s->cis_virt = NULL;
		destroy_cis_cache(s);
	}
	s->cis_mem.sys_start = 0;
	s->cis_mem.sys_stop = 0;
	if ((ret != 0) || (info->Chains == 0))
		return 0;
	return 1;
}

/* Validation function for simple memory cards */
static int checksum(struct pcmcia_socket *s, struct resource *res)
{
	pccard_mem_map map;
	int i, a = 0, b = -1, d;
	void *virt;

	virt = ioremap(res->start, s->map_size);
	if (virt) {
		map.map = 0;
		map.flags = MAP_ACTIVE;
		map.speed = 0;
		map.sys_start = res->start;
		map.sys_stop = res->end;
		map.card_start = 0;
		s->ops->set_mem_map(s, &map);

		/* Don't bother checking every word... */
		for (i = 0; i < s->map_size; i += 44) {
			d = readl(virt+i);
			a += d;
			b &= d;
		}

		map.flags = 0;
		s->ops->set_mem_map(s, &map);

		iounmap(virt);
	}

	return (b == -1) ? -1 : (a>>1);
}

static int
cis_readable(struct pcmcia_socket *s, unsigned long base, unsigned long size)
{
	struct resource *res1, *res2;
	cisinfo_t info1, info2;
	int ret = 0;

	res1 = claim_region(s, base, size/2, IORESOURCE_MEM, "cs memory probe");
	res2 = claim_region(s, base + size/2, size/2, IORESOURCE_MEM, "cs memory probe");

	if (res1 && res2) {
		ret = readable(s, res1, &info1);
		ret += readable(s, res2, &info2);
	}

	free_region(res2);
	free_region(res1);

	return (ret == 2) && (info1.Chains == info2.Chains);
}

static int
checksum_match(struct pcmcia_socket *s, unsigned long base, unsigned long size)
{
	struct resource *res1, *res2;
	int a = -1, b = -1;

	res1 = claim_region(s, base, size/2, IORESOURCE_MEM, "cs memory probe");
	res2 = claim_region(s, base + size/2, size/2, IORESOURCE_MEM, "cs memory probe");

	if (res1 && res2) {
		a = checksum(s, res1);
		b = checksum(s, res2);
	}

	free_region(res2);
	free_region(res1);

	return (a == b) && (a >= 0);
}

/*======================================================================

    The memory probe.  If the memory list includes a 64K-aligned block
    below 1MB, we probe in 64K chunks, and as soon as we accumulate at
    least mem_limit free space, we quit.
    
======================================================================*/

static int do_mem_probe(u_long base, u_long num, struct pcmcia_socket *s)
{
    u_long i, j, bad, fail, step;

    printk(KERN_INFO "cs: memory probe 0x%06lx-0x%06lx:",
	   base, base+num-1);
    bad = fail = 0;
    step = (num < 0x20000) ? 0x2000 : ((num>>4) & ~0x1fff);
    /* cis_readable wants to map 2x map_size */
    if (step < 2 * s->map_size)
	step = 2 * s->map_size;
    for (i = j = base; i < base+num; i = j + step) {
	if (!fail) {	
	    for (j = i; j < base+num; j += step) {
		if (cis_readable(s, j, step))
		    break;
	    }
	    fail = ((i == base) && (j == base+num));
	}
	if (fail) {
	    for (j = i; j < base+num; j += 2*step)
		if (checksum_match(s, j, step) &&
		    checksum_match(s, j + step, step))
		    break;
	}
	if (i != j) {
	    if (!bad) printk(" excluding");
	    printk(" %#05lx-%#05lx", i, j-1);
	    sub_interval(&mem_db, i, j-i);
	    bad += j-i;
	}
    }
    printk(bad ? "\n" : " clean.\n");
    return (num - bad);
}

#ifdef CONFIG_PCMCIA_PROBE

static u_long inv_probe(resource_map_t *m, struct pcmcia_socket *s)
{
    u_long ok;
    if (m == &mem_db)
	return 0;
    ok = inv_probe(m->next, s);
    if (ok) {
	if (m->base >= 0x100000)
	    sub_interval(&mem_db, m->base, m->num);
	return ok;
    }
    if (m->base < 0x100000)
	return 0;
    return do_mem_probe(m->base, m->num, s);
}

void validate_mem(struct pcmcia_socket *s)
{
    resource_map_t *m, mm;
    static u_char order[] = { 0xd0, 0xe0, 0xc0, 0xf0 };
    static int hi = 0, lo = 0;
    u_long b, i, ok = 0;
    int force_low = !(s->features & SS_CAP_PAGE_REGS);

    if (!probe_mem)
	return;

    down(&rsrc_sem);
    /* We do up to four passes through the list */
    if (!force_low) {
	if (hi++ || (inv_probe(mem_db.next, s) > 0))
	    goto out;
	printk(KERN_NOTICE "cs: warning: no high memory space "
	       "available!\n");
    }
    if (lo++)
	goto out;
    for (m = mem_db.next; m != &mem_db; m = mm.next) {
	mm = *m;
	/* Only probe < 1 MB */
	if (mm.base >= 0x100000) continue;
	if ((mm.base | mm.num) & 0xffff) {
	    ok += do_mem_probe(mm.base, mm.num, s);
	    continue;
	}
	/* Special probe for 64K-aligned block */
	for (i = 0; i < 4; i++) {
	    b = order[i] << 12;
	    if ((b >= mm.base) && (b+0x10000 <= mm.base+mm.num)) {
		if (ok >= mem_limit)
		    sub_interval(&mem_db, b, 0x10000);
		else
		    ok += do_mem_probe(b, 0x10000, s);
	    }
	}
    }
 out:
    up(&rsrc_sem);
}

#else /* CONFIG_PCMCIA_PROBE */

void validate_mem(struct pcmcia_socket *s)
{
    resource_map_t *m, mm;
    static int done = 0;
    
    if (probe_mem && done++ == 0) {
	down(&rsrc_sem);
	for (m = mem_db.next; m != &mem_db; m = mm.next) {
	    mm = *m;
	    if (do_mem_probe(mm.base, mm.num, s))
		break;
	}
	up(&rsrc_sem);
    }
}

#endif /* CONFIG_PCMCIA_PROBE */

struct pcmcia_align_data {
	unsigned long	mask;
	unsigned long	offset;
	resource_map_t	*map;
};

static void
pcmcia_common_align(void *align_data, struct resource *res,
		    unsigned long size, unsigned long align)
{
	struct pcmcia_align_data *data = align_data;
	unsigned long start;
	/*
	 * Ensure that we have the correct start address
	 */
	start = (res->start & ~data->mask) + data->offset;
	if (start < res->start)
		start += data->mask + 1;
	res->start = start;
}

static void
pcmcia_align(void *align_data, struct resource *res,
	     unsigned long size, unsigned long align)
{
	struct pcmcia_align_data *data = align_data;
	resource_map_t *m;

	pcmcia_common_align(data, res, size, align);

	for (m = data->map->next; m != data->map; m = m->next) {
		unsigned long start = m->base;
		unsigned long end = m->base + m->num - 1;

		/*
		 * If the lower resources are not available, try aligning
		 * to this entry of the resource database to see if it'll
		 * fit here.
		 */
		if (res->start < start) {
			res->start = start;
			pcmcia_common_align(data, res, size, align);
		}

		/*
		 * If we're above the area which was passed in, there's
		 * no point proceeding.
		 */
		if (res->start >= res->end)
			break;

		if ((res->start + size - 1) <= end)
			break;
	}

	/*
	 * If we failed to find something suitable, ensure we fail.
	 */
	if (m == data->map)
		res->start = res->end;
}

/*
 * Adjust an existing IO region allocation, but making sure that we don't
 * encroach outside the resources which the user supplied.
 */
int adjust_io_region(struct resource *res, unsigned long r_start,
		     unsigned long r_end, struct pcmcia_socket *s)
{
	resource_map_t *m;
	int ret = -ENOMEM;

	down(&rsrc_sem);
	for (m = io_db.next; m != &io_db; m = m->next) {
		unsigned long start = m->base;
		unsigned long end = m->base + m->num - 1;

		if (start > r_start || r_end > end)
			continue;

		ret = adjust_resource(res, r_start, r_end - r_start + 1);
		break;
	}
	up(&rsrc_sem);

	return ret;
}

/*======================================================================

    These find ranges of I/O ports or memory addresses that are not
    currently allocated by other devices.

    The 'align' field should reflect the number of bits of address
    that need to be preserved from the initial value of *base.  It
    should be a power of two, greater than or equal to 'num'.  A value
    of 0 means that all bits of *base are significant.  *base should
    also be strictly less than 'align'.
    
======================================================================*/

struct resource *find_io_region(unsigned long base, int num,
		   unsigned long align, char *name, struct pcmcia_socket *s)
{
	struct resource *res = make_resource(0, num, IORESOURCE_IO, name);
	struct pcmcia_align_data data;
	unsigned long min = base;
	int ret;

	if (align == 0)
		align = 0x10000;

	data.mask = align - 1;
	data.offset = base & data.mask;
	data.map = &io_db;

	down(&rsrc_sem);
#ifdef CONFIG_PCI
	if (s->cb_dev) {
		ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num, 1,
					     min, 0, pcmcia_align, &data);
	} else
#endif
		ret = allocate_resource(&ioport_resource, res, num, min, ~0UL, 0,
					pcmcia_align, &data);
	up(&rsrc_sem);

	if (ret != 0) {
		kfree(res);
		res = NULL;
	}
	return res;
}

int find_mem_region(u_long *base, u_long num, u_long align,
		    int low, char *name, struct pcmcia_socket *s)
{
	struct resource *res = make_resource(0, num, IORESOURCE_MEM, name);
	struct pcmcia_align_data data;
	unsigned long min, max;
	int ret, i;

	low = low || !(s->features & SS_CAP_PAGE_REGS);

	data.mask = align - 1;
	data.offset = *base & data.mask;
	data.map = &mem_db;

	for (i = 0; i < 2; i++) {
		if (low) {
			max = 0x100000UL;
			min = *base < max ? *base : 0;
		} else {
			max = ~0UL;
			min = 0x100000UL + *base;
		}

		down(&rsrc_sem);
#ifdef CONFIG_PCI
		if (s->cb_dev) {
			ret = pci_bus_alloc_resource(s->cb_dev->bus, res, num,
						     1, min, 0,
						     pcmcia_align, &data);
		} else
#endif
			ret = allocate_resource(&iomem_resource, res, num, min,
						max, 0, pcmcia_align, &data);
		up(&rsrc_sem);
		if (ret == 0 || low)
			break;
		low = 1;
	}

	if (ret != 0) {
		kfree(res);
	} else {
		*base = res->start;
	}
	return ret;
}

/*======================================================================

    This checks to see if an interrupt is available, with support
    for interrupt sharing.  We don't support reserving interrupts
    yet.  If the interrupt is available, we allocate it.
    
======================================================================*/

#ifdef CONFIG_PCMCIA_PROBE

static irqreturn_t fake_irq(int i, void *d, struct pt_regs *r) { return IRQ_NONE; }
static inline int check_irq(int irq)
{
    if (request_irq(irq, fake_irq, 0, "bogus", NULL) != 0)
	return -1;
    free_irq(irq, NULL);
    return 0;
}

int try_irq(u_int Attributes, int irq, int specific)
{
    irq_info_t *info = &irq_table[irq];
    int ret = 0;

    down(&rsrc_sem);
    if (info->Attributes & RES_ALLOCATED) {
	switch (Attributes & IRQ_TYPE) {
	case IRQ_TYPE_EXCLUSIVE:
	    ret = CS_IN_USE;
	    break;
	case IRQ_TYPE_TIME:
	    if ((info->Attributes & RES_IRQ_TYPE)
		!= RES_IRQ_TYPE_TIME) {
		ret = CS_IN_USE;
		break;
	    }
	    if (Attributes & IRQ_FIRST_SHARED) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_TIME | RES_ALLOCATED;
	    info->time_share++;
	    break;
	case IRQ_TYPE_DYNAMIC_SHARING:
	    if ((info->Attributes & RES_IRQ_TYPE)
		!= RES_IRQ_TYPE_DYNAMIC) {
		ret = CS_IN_USE;
		break;
	    }
	    if (Attributes & IRQ_FIRST_SHARED) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_DYNAMIC | RES_ALLOCATED;
	    info->dyn_share++;
	    break;
	}
    } else {
	if ((info->Attributes & RES_RESERVED) && !specific) {
	    ret = CS_IN_USE;
	    goto out;
	}
	if (check_irq(irq) != 0) {
	    ret = CS_IN_USE;
	    goto out;
	}
	switch (Attributes & IRQ_TYPE) {
	case IRQ_TYPE_EXCLUSIVE:
	    info->Attributes |= RES_ALLOCATED;
	    break;
	case IRQ_TYPE_TIME:
	    if (!(Attributes & IRQ_FIRST_SHARED)) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_TIME | RES_ALLOCATED;
	    info->time_share = 1;
	    break;
	case IRQ_TYPE_DYNAMIC_SHARING:
	    if (!(Attributes & IRQ_FIRST_SHARED)) {
		ret = CS_BAD_ATTRIBUTE;
		break;
	    }
	    info->Attributes |= RES_IRQ_TYPE_DYNAMIC | RES_ALLOCATED;
	    info->dyn_share = 1;
	    break;
	}
    }
 out:
    up(&rsrc_sem);
    return ret;
}

#endif

/*====================================================================*/

#ifdef CONFIG_PCMCIA_PROBE

void undo_irq(u_int Attributes, int irq)
{
    irq_info_t *info;

    info = &irq_table[irq];
    down(&rsrc_sem);
    switch (Attributes & IRQ_TYPE) {
    case IRQ_TYPE_EXCLUSIVE:
	info->Attributes &= RES_RESERVED;
	break;
    case IRQ_TYPE_TIME:
	info->time_share--;
	if (info->time_share == 0)
	    info->Attributes &= RES_RESERVED;
	break;
    case IRQ_TYPE_DYNAMIC_SHARING:
	info->dyn_share--;
	if (info->dyn_share == 0)
	    info->Attributes &= RES_RESERVED;
	break;
    }
    up(&rsrc_sem);
}

#endif

/*======================================================================

    The various adjust_* calls form the external interface to the
    resource database.
    
======================================================================*/

static int adjust_memory(adjust_t *adj)
{
    u_long base, num;
    int ret;

    base = adj->resource.memory.Base;
    num = adj->resource.memory.Size;
    if ((num == 0) || (base+num-1 < base))
	return CS_BAD_SIZE;

    ret = CS_SUCCESS;

    down(&rsrc_sem);
    switch (adj->Action) {
    case ADD_MANAGED_RESOURCE:
	ret = add_interval(&mem_db, base, num);
	break;
    case REMOVE_MANAGED_RESOURCE:
	ret = sub_interval(&mem_db, base, num);
	if (ret == CS_SUCCESS) {
		struct pcmcia_socket *socket;
		down_read(&pcmcia_socket_list_rwsem);
		list_for_each_entry(socket, &pcmcia_socket_list, socket_list)
			release_cis_mem(socket);
		up_read(&pcmcia_socket_list_rwsem);
	}
	break;
    default:
	ret = CS_UNSUPPORTED_FUNCTION;
    }
    up(&rsrc_sem);
    
    return ret;
}

/*====================================================================*/

static int adjust_io(adjust_t *adj)
{
    int base, num, ret = CS_SUCCESS;
    
    base = adj->resource.io.BasePort;
    num = adj->resource.io.NumPorts;
    if ((base < 0) || (base > 0xffff))
	return CS_BAD_BASE;
    if ((num <= 0) || (base+num > 0x10000) || (base+num <= base))
	return CS_BAD_SIZE;

    down(&rsrc_sem);
    switch (adj->Action) {
    case ADD_MANAGED_RESOURCE:
	if (add_interval(&io_db, base, num) != 0) {
	    ret = CS_IN_USE;
	    break;
	}
#ifdef CONFIG_PCMCIA_PROBE
	if (probe_io)
	    do_io_probe(base, num);
#endif
	break;
    case REMOVE_MANAGED_RESOURCE:
	sub_interval(&io_db, base, num);
	break;
    default:
	ret = CS_UNSUPPORTED_FUNCTION;
	break;
    }
    up(&rsrc_sem);

    return ret;
}

/*====================================================================*/

static int adjust_irq(adjust_t *adj)
{
    int ret = CS_SUCCESS;
#ifdef CONFIG_PCMCIA_PROBE
    int irq;
    irq_info_t *info;
    
    irq = adj->resource.irq.IRQ;
    if ((irq < 0) || (irq > 15))
	return CS_BAD_IRQ;
    info = &irq_table[irq];

    down(&rsrc_sem);
    switch (adj->Action) {
    case ADD_MANAGED_RESOURCE:
	if (info->Attributes & RES_REMOVED)
	    info->Attributes &= ~(RES_REMOVED|RES_ALLOCATED);
	else
	    if (adj->Attributes & RES_ALLOCATED) {
		ret = CS_IN_USE;
		break;
	    }
	if (adj->Attributes & RES_RESERVED)
	    info->Attributes |= RES_RESERVED;
	else
	    info->Attributes &= ~RES_RESERVED;
	break;
    case REMOVE_MANAGED_RESOURCE:
	if (info->Attributes & RES_REMOVED) {
	    ret = 0;
	    break;
	}
	if (info->Attributes & RES_ALLOCATED) {
	    ret = CS_IN_USE;
	    break;
	}
	info->Attributes |= RES_ALLOCATED|RES_REMOVED;
	info->Attributes &= ~RES_RESERVED;
	break;
    default:
	ret = CS_UNSUPPORTED_FUNCTION;
	break;
    }
    up(&rsrc_sem);
#endif
    return ret;
}

/*====================================================================*/

int pcmcia_adjust_resource_info(client_handle_t handle, adjust_t *adj)
{
    if (CHECK_HANDLE(handle))
	return CS_BAD_HANDLE;
    
    switch (adj->Resource) {
    case RES_MEMORY_RANGE:
	return adjust_memory(adj);
	break;
    case RES_IO_RANGE:
	return adjust_io(adj);
	break;
    case RES_IRQ:
	return adjust_irq(adj);
	break;
    }
    return CS_UNSUPPORTED_FUNCTION;
}

/*====================================================================*/

void release_resource_db(void)
{
    resource_map_t *p, *q;
    
    for (p = mem_db.next; p != &mem_db; p = q) {
	q = p->next;
	kfree(p);
    }
    for (p = io_db.next; p != &io_db; p = q) {
	q = p->next;
	kfree(p);
    }
}
