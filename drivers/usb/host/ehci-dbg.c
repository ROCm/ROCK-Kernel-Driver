/*
 * Copyright (c) 2001-2002 by David Brownell
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* this file is part of ehci-hcd.c */

#ifdef EHCI_VERBOSE_DEBUG
#	define vdbg dbg
#else
	static inline void vdbg (char *fmt, ...) { }
#endif

#ifdef	DEBUG

/* check the values in the HCSPARAMS register - host controller structural parameters */
/* see EHCI 0.95 Spec, Table 2-4 for each value */
static void dbg_hcs_params (struct ehci_hcd *ehci, char *label)
{
	u32	params = readl (&ehci->caps->hcs_params);

	dbg ("%s hcs_params 0x%x dbg=%d%s cc=%d pcc=%d%s%s ports=%d",
		label, params,
		HCS_DEBUG_PORT (params),
		HCS_INDICATOR (params) ? " ind" : "",
		HCS_N_CC (params),
		HCS_N_PCC (params),
	        HCS_PORTROUTED (params) ? "" : " ordered",
		HCS_PPC (params) ? "" : " !ppc",
		HCS_N_PORTS (params)
		);
	/* Port routing, per EHCI 0.95 Spec, Section 2.2.5 */
	if (HCS_PORTROUTED (params)) {
		int i;
		char buf [46], tmp [7], byte;

		buf[0] = 0;
		for (i = 0; i < HCS_N_PORTS (params); i++) {
			byte = readb (&ehci->caps->portroute[(i>>1)]);
			sprintf(tmp, "%d ", 
				((i & 0x1) ? ((byte)&0xf) : ((byte>>4)&0xf)));
			strcat(buf, tmp);
		}
		dbg ("%s: %s portroute %s", 
			ehci->hcd.self.bus_name, label,
			buf);
	}
}
#else

static inline void dbg_hcs_params (struct ehci_hcd *ehci, char *label) {}

#endif

#ifdef	DEBUG

/* check the values in the HCCPARAMS register - host controller capability parameters */
/* see EHCI 0.95 Spec, Table 2-5 for each value */
static void dbg_hcc_params (struct ehci_hcd *ehci, char *label)
{
	u32	params = readl (&ehci->caps->hcc_params);

	if (HCC_EXT_CAPS (params)) {
		// EHCI 0.96 ... could interpret these (legacy?)
		dbg ("%s extended capabilities at pci %d",
			label, HCC_EXT_CAPS (params));
	}
	if (HCC_ISOC_CACHE (params)) {
		dbg ("%s hcc_params 0x%04x caching frame %s%s%s",
		     label, params,
		     HCC_PGM_FRAMELISTLEN (params) ? "256/512/1024" : "1024",
		     HCC_CANPARK (params) ? " park" : "",
		     HCC_64BIT_ADDR (params) ? " 64 bit addr" : "");
	} else {
		dbg ("%s hcc_params 0x%04x caching %d uframes %s%s%s",
		     label,
		     params,
		     HCC_ISOC_THRES (params),
		     HCC_PGM_FRAMELISTLEN (params) ? "256/512/1024" : "1024",
		     HCC_CANPARK (params) ? " park" : "",
		     HCC_64BIT_ADDR (params) ? " 64 bit addr" : "");
	}
}
#else

static inline void dbg_hcc_params (struct ehci_hcd *ehci, char *label) {}

#endif

#ifdef	DEBUG

static void __attribute__((__unused__))
dbg_qh (char *label, struct ehci_hcd *ehci, struct ehci_qh *qh)
{
	dbg ("%s %p info1 %x info2 %x hw_curr %x qtd_next %x", label,
		qh, qh->hw_info1, qh->hw_info2,
		qh->hw_current, qh->hw_qtd_next);
	dbg ("  alt+errs= %x, token= %x, page0= %x, page1= %x",
		qh->hw_alt_next, qh->hw_token,
		qh->hw_buf [0], qh->hw_buf [1]);
	if (qh->hw_buf [2]) {
		dbg ("  page2= %x, page3= %x, page4= %x",
			qh->hw_buf [2], qh->hw_buf [3],
			qh->hw_buf [4]);
	}
}

static const char *const fls_strings [] =
    { "1024", "512", "256", "??" };

#else
static inline void __attribute__((__unused__))
dbg_qh (char *label, struct ehci_hcd *ehci, struct ehci_qh *qh) {}
#endif	/* DEBUG */

/* functions have the "wrong" filename when they're output... */

#define dbg_status(ehci, label, status) \
	dbg ("%s status 0x%x%s%s%s%s%s%s%s%s%s%s", \
		label, status, \
		(status & STS_ASS) ? " Async" : "", \
		(status & STS_PSS) ? " Periodic" : "", \
		(status & STS_RECL) ? " Recl" : "", \
		(status & STS_HALT) ? " Halt" : "", \
		(status & STS_IAA) ? " IAA" : "", \
		(status & STS_FATAL) ? " FATAL" : "", \
		(status & STS_FLR) ? " FLR" : "", \
		(status & STS_PCD) ? " PCD" : "", \
		(status & STS_ERR) ? " ERR" : "", \
		(status & STS_INT) ? " INT" : "" \
		)

#define dbg_cmd(ehci, label, command) \
	dbg ("%s %x cmd %s=%d ithresh=%d%s%s%s%s period=%s%s %s", \
		label, command, \
		(command & CMD_PARK) ? "park" : "(park)", \
		CMD_PARK_CNT (command), \
		(command >> 16) & 0x3f, \
		(command & CMD_LRESET) ? " LReset" : "", \
		(command & CMD_IAAD) ? " IAAD" : "", \
		(command & CMD_ASE) ? " Async" : "", \
		(command & CMD_PSE) ? " Periodic" : "", \
		fls_strings [(command >> 2) & 0x3], \
		(command & CMD_RESET) ? " Reset" : "", \
		(command & CMD_RUN) ? "RUN" : "HALT" \
		)

#define dbg_port(hcd, label, port, status) \
	dbg ("%s port %d status 0x%x%s%s speed=%d%s%s%s%s%s%s%s%s%s", \
		label, port, status, \
		(status & PORT_OWNER) ? " OWNER" : "", \
		(status & PORT_POWER) ? " POWER" : "", \
		(status >> 10) & 3, \
		(status & PORT_RESET) ? " RESET" : "", \
		(status & PORT_SUSPEND) ? " SUSPEND" : "", \
		(status & PORT_RESUME) ? " RESUME" : "", \
		(status & PORT_OCC) ? " OCC" : "", \
		(status & PORT_OC) ? " OC" : "", \
		(status & PORT_PEC) ? " PEC" : "", \
		(status & PORT_PE) ? " PE" : "", \
		(status & PORT_CSC) ? " CSC" : "", \
		(status & PORT_CONNECT) ? " CONNECT" : "" \
	    )

#ifdef DEBUG

#define speed_char(info1) ({ char tmp; \
		switch (info1 & (3 << 12)) { \
		case 0 << 12: tmp = 'f'; break; \
		case 1 << 12: tmp = 'l'; break; \
		case 2 << 12: tmp = 'h'; break; \
		default: tmp = '?'; break; \
		}; tmp; })

static ssize_t
show_async (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct pci_dev		*pdev;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	unsigned		temp, size;
	char			*next;
	struct ehci_qh		*qh;

	if (off != 0)
		return 0;

	pdev = container_of (dev, struct pci_dev, dev);
	ehci = container_of (pci_get_drvdata (pdev), struct ehci_hcd, hcd);
	next = buf;
	size = count;

	/* dumps a snapshot of the async schedule.
	 * usually empty except for long-term bulk reads, or head.
	 * one QH per line, and TDs we know about
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	if (ehci->async) {
		qh = ehci->async;
		do {
			u32			scratch;
			struct list_head	*entry;
			struct ehci_qtd		*td;

			scratch = cpu_to_le32p (&qh->hw_info1);
			temp = snprintf (next, size, "qh %p dev%d %cs ep%d",
					qh, scratch & 0x007f,
					speed_char (scratch),
					(scratch >> 8) & 0x000f);
			size -= temp;
			next += temp;

			list_for_each (entry, &qh->qtd_list) {
				td = list_entry (entry, struct ehci_qtd,
						qtd_list);
				scratch = cpu_to_le32p (&td->hw_token);
				temp = snprintf (next, size,
						", td %p len=%d %s",
						td, scratch >> 16,
						({ char *tmp;
						 switch ((scratch>>8)&0x03) {
						 case 0: tmp = "out"; break;
						 case 1: tmp = "in"; break;
						 case 2: tmp = "setup"; break;
						 default: tmp = "?"; break;
						 } tmp;})
						);
				size -= temp;
				next += temp;
			}

			temp = snprintf (next, size, "\n");
			size -= temp;
			next += temp;

		} while ((qh = qh->qh_next.qh) != ehci->async);
	}
	spin_unlock_irqrestore (&ehci->lock, flags);

	return count - size;
}
static DEVICE_ATTR (async, S_IRUSR, show_async, NULL);

#define DBG_SCHED_LIMIT 64

static ssize_t
show_periodic (struct device *dev, char *buf, size_t count, loff_t off)
{
	struct pci_dev		*pdev;
	struct ehci_hcd		*ehci;
	unsigned long		flags;
	union ehci_shadow	p, *seen;
	unsigned		temp, size, seen_count;
	char			*next;
	unsigned		i, tag;

	if (off != 0)
		return 0;
	if (!(seen = kmalloc (DBG_SCHED_LIMIT * sizeof *seen, SLAB_ATOMIC)))
		return 0;
	seen_count = 0;

	pdev = container_of (dev, struct pci_dev, dev);
	ehci = container_of (pci_get_drvdata (pdev), struct ehci_hcd, hcd);
	next = buf;
	size = count;

	temp = snprintf (next, size, "size = %d\n", ehci->periodic_size);
	size -= temp;
	next += temp;

	/* dump a snapshot of the periodic schedule.
	 * iso changes, interrupt usually doesn't.
	 */
	spin_lock_irqsave (&ehci->lock, flags);
	for (i = 0; i < ehci->periodic_size; i++) {
		p = ehci->pshadow [i];
		if (!p.ptr)
			continue;
		tag = Q_NEXT_TYPE (ehci->periodic [i]);

		temp = snprintf (next, size, "%4d: ", i);
		size -= temp;
		next += temp;

		do {
			switch (tag) {
			case Q_TYPE_QH:
				temp = snprintf (next, size, " intr-%d %p",
						p.qh->period, p.qh);
				size -= temp;
				next += temp;
				for (temp = 0; temp < seen_count; temp++) {
					if (seen [temp].ptr == p.ptr)
						break;
				}
				/* show more info the first time around */
				if (temp == seen_count) {
					u32	scratch = cpu_to_le32p (
							&p.qh->hw_info1);

					temp = snprintf (next, size,
						" (%cs dev%d ep%d)",
						speed_char (scratch),
						scratch & 0x007f,
						(scratch >> 8) & 0x000f);

					/* FIXME TDs too */

					if (seen_count < DBG_SCHED_LIMIT)
						seen [seen_count++].qh = p.qh;
				} else
					temp = 0;
				tag = Q_NEXT_TYPE (p.qh->hw_next);
				p = p.qh->qh_next;
				break;
			case Q_TYPE_FSTN:
				temp = snprintf (next, size,
					" fstn-%8x/%p", p.fstn->hw_prev,
					p.fstn);
				tag = Q_NEXT_TYPE (p.fstn->hw_next);
				p = p.fstn->fstn_next;
				break;
			case Q_TYPE_ITD:
				temp = snprintf (next, size,
					" itd/%p", p.itd);
				tag = Q_NEXT_TYPE (p.itd->hw_next);
				p = p.itd->itd_next;
				break;
			case Q_TYPE_SITD:
				temp = snprintf (next, size,
					" sitd/%p", p.sitd);
				tag = Q_NEXT_TYPE (p.sitd->hw_next);
				p = p.sitd->sitd_next;
				break;
			}
			size -= temp;
			next += temp;
		} while (p.ptr);

		temp = snprintf (next, size, "\n");
		size -= temp;
		next += temp;
	}
	spin_unlock_irqrestore (&ehci->lock, flags);
	kfree (seen);

	return count - size;
}
static DEVICE_ATTR (periodic, S_IRUSR, show_periodic, NULL);

#undef DBG_SCHED_LIMIT

static inline void create_debug_files (struct ehci_hcd *bus)
{
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_async);
	device_create_file (&bus->hcd.pdev->dev, &dev_attr_periodic);
}

static inline void remove_debug_files (struct ehci_hcd *bus)
{
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_async);
	device_remove_file (&bus->hcd.pdev->dev, &dev_attr_periodic);
}

#else /* DEBUG */

static inline void create_debug_files (struct ehci_hcd *bus)
{
}

static inline void remove_debug_files (struct ehci_hcd *bus)
{
}

#endif /* DEBUG */
