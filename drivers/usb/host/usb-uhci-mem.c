/*  
    UHCI HCD (Host Controller Driver) for USB
    UHCI memory allocation and basic descriptor handling
   
    (c) 1999-2002 
    Georg Acher      +    Deti Fliegl    +    Thomas Sailer
    georg@acher.org      deti@fliegl.de   sailer@ife.ee.ethz.ch
   
    with the help of
    David Brownell, david-b@pacbell.net
    Adam Richter, adam@yggdrasil.com
    Roman Weissgaerber, weissg@vienna.at    
    
    HW-initalization based on material of
    Randy Dunlap + Johannes Erdfelt + Gregory P. Smith + Linus Torvalds 

    $Id: usb-uhci-mem.c,v 1.3 2002/05/25 16:42:41 acher Exp $
 */

/*###########################################################################*/
//                        UHCI STRUCTURE
/*###########################################################################*/
static struct usb_hcd *uhci_hcd_alloc (void)
{
	struct uhci_hcd *uhci;
	int len;

	len=sizeof (struct uhci_hcd);
	uhci = (struct uhci_hcd *) kmalloc (len, GFP_KERNEL);
	if (uhci == 0) 
		return NULL;
	
	memset (uhci, 0, len);
	init_dbg("uhci @ %p, hcd @ %p",uhci, &(uhci->hcd));
	INIT_LIST_HEAD (&uhci->free_desc_qh);
	INIT_LIST_HEAD (&uhci->free_desc_td);
	INIT_LIST_HEAD (&uhci->urb_list);
	INIT_LIST_HEAD (&uhci->urb_unlinked);
	spin_lock_init (&uhci->urb_list_lock);
	spin_lock_init (&uhci->qh_lock);
	spin_lock_init (&uhci->td_lock);
	atomic_set(&uhci->avoid_bulk, 0);		
	return &(uhci->hcd);
}
/*-------------------------------------------------------------------*/
static void uhci_hcd_free (struct usb_hcd *hcd)
{
	kfree (hcd_to_uhci (hcd));
}
/*###########################################################################*/
//                   DMA/PCI CONSISTENCY
/*###########################################################################*/

static void uhci_urb_dma_sync(struct uhci_hcd *uhci, struct urb *urb, urb_priv_t *urb_priv)
{
	if (urb_priv->setup_packet_dma)
		pci_dma_sync_single(uhci->uhci_pci, urb_priv->setup_packet_dma,
				    sizeof(struct usb_ctrlrequest), PCI_DMA_TODEVICE);

	if (urb_priv->transfer_buffer_dma)
		pci_dma_sync_single(uhci->uhci_pci, urb_priv->transfer_buffer_dma,
				    urb->transfer_buffer_length,
				    usb_pipein(urb->pipe) ?
				    PCI_DMA_FROMDEVICE :
				    PCI_DMA_TODEVICE);
}
/*-------------------------------------------------------------------*/
static void uhci_urb_dma_unmap(struct uhci_hcd *uhci, struct urb *urb, urb_priv_t *urb_priv)
{
	if (urb_priv->setup_packet_dma) {
		pci_unmap_single(uhci->uhci_pci, urb_priv->setup_packet_dma,
				 sizeof(struct usb_ctrlrequest), PCI_DMA_TODEVICE);
		urb_priv->setup_packet_dma = 0;
	}
	if (urb_priv->transfer_buffer_dma) {
		pci_unmap_single(uhci->uhci_pci, urb_priv->transfer_buffer_dma,
				 urb->transfer_buffer_length,
				 usb_pipein(urb->pipe) ?
				 PCI_DMA_FROMDEVICE :
				 PCI_DMA_TODEVICE);
		urb_priv->transfer_buffer_dma = 0;
	}
}
/*###########################################################################*/
//                       TRANSFER DESCRIPTORS (TD)
/*###########################################################################*/

static void fill_td (uhci_desc_t *td, int status, int info, __u32 buffer)
{
	td->hw.td.status = cpu_to_le32(status);
	td->hw.td.info = cpu_to_le32(info);
	td->hw.td.buffer = cpu_to_le32(buffer);
}
/*-------------------------------------------------------------------*/
static int alloc_td (struct uhci_hcd *uhci, uhci_desc_t ** new, int flags)
{
	dma_addr_t dma_handle;

	*new = pci_pool_alloc(uhci->desc_pool, GFP_DMA | GFP_ATOMIC, &dma_handle);
	if (!*new)
		return -ENOMEM;
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->dma_addr = dma_handle;
	set_td_link((*new), UHCI_PTR_TERM | (flags & UHCI_PTR_BITS));	// last by default
	(*new)->type = TD_TYPE;
	mb();
	INIT_LIST_HEAD (&(*new)->vertical);
	INIT_LIST_HEAD (&(*new)->horizontal);
	
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert td at last position in td-list of qh (vertical) */
static int insert_td (struct uhci_hcd *uhci, uhci_desc_t *qh, uhci_desc_t* new, int flags)
{
	uhci_desc_t *prev;
	unsigned long cpuflags;
	
	spin_lock_irqsave (&uhci->td_lock, cpuflags);

	list_add_tail (&new->vertical, &qh->vertical);

	prev = list_entry (new->vertical.prev, uhci_desc_t, vertical);

	if (qh == prev ) {
		// virgin qh without any tds
		set_qh_element(qh, new->dma_addr | UHCI_PTR_TERM);
	}
	else {
		// already tds inserted, implicitely remove TERM bit of prev
		set_td_link(prev, new->dma_addr | (flags & UHCI_PTR_DEPTH));
	}
	mb();
	spin_unlock_irqrestore (&uhci->td_lock, cpuflags);
	
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert new_td after td (horizontal) */
static int insert_td_horizontal (struct uhci_hcd *uhci, uhci_desc_t *td, uhci_desc_t* new)
{
	uhci_desc_t *next;
	unsigned long flags;
	
	spin_lock_irqsave (&uhci->td_lock, flags);

	next = list_entry (td->horizontal.next, uhci_desc_t, horizontal);
	list_add (&new->horizontal, &td->horizontal);
	new->hw.td.link = td->hw.td.link;
	mb();
	set_td_link(td, new->dma_addr);
	mb();
	spin_unlock_irqrestore (&uhci->td_lock, flags);	
	
	return 0;
}
/*-------------------------------------------------------------------*/
static int unlink_td (struct uhci_hcd *uhci, uhci_desc_t *element, int phys_unlink)
{
	uhci_desc_t *next, *prev;
	int dir = 0;
	unsigned long flags;
	
	spin_lock_irqsave (&uhci->td_lock, flags);
	
	next = list_entry (element->vertical.next, uhci_desc_t, vertical);
	
	if (next == element) {
		dir = 1;
		prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	}
	else 
		prev = list_entry (element->vertical.prev, uhci_desc_t, vertical);
	
	if (phys_unlink) {
		// really remove HW linking
		if (prev->type == TD_TYPE) {
			prev->hw.td.link = element->hw.td.link;
		}
		else
			prev->hw.qh.element = element->hw.td.link;
	}
 	mb ();

	if (dir == 0)
		list_del (&element->vertical);
	else
		list_del (&element->horizontal);
	
	spin_unlock_irqrestore (&uhci->td_lock, flags);	
	
	return 0;
}
/*###########################################################################*/
//               QUEUE HEADS (QH)
/*###########################################################################*/
// Allocates qh element
static int alloc_qh (struct uhci_hcd *uhci, uhci_desc_t ** new)
{
	dma_addr_t dma_handle;

	*new = pci_pool_alloc(uhci->desc_pool, GFP_DMA | GFP_ATOMIC, &dma_handle);
	if (!*new)
		return -ENOMEM;
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->dma_addr = dma_handle;
	set_qh_head(*new, UHCI_PTR_TERM);
	set_qh_element(*new, UHCI_PTR_TERM);
	(*new)->type = QH_TYPE;
	
	mb();
	INIT_LIST_HEAD (&(*new)->horizontal);
	INIT_LIST_HEAD (&(*new)->vertical);
	
	dbg("Allocated qh @ %p", *new);
	return 0;
}
/*-------------------------------------------------------------------*/
// inserts new qh before/after the qh at pos
// flags: 0: insert before pos, 1: insert after pos (for low speed transfers)
static int insert_qh (struct uhci_hcd *uhci, uhci_desc_t *pos, uhci_desc_t *new, int order)
{
	uhci_desc_t *old;
	unsigned long flags;

	spin_lock_irqsave (&uhci->qh_lock, flags);

	if (!order) {
		// (OLD) (POS) -> (OLD) (NEW) (POS)
		old = list_entry (pos->horizontal.prev, uhci_desc_t, horizontal);
		list_add_tail (&new->horizontal, &pos->horizontal);
		set_qh_head(new, MAKE_QH_ADDR (pos)) ;
		mb();
		if (!(old->hw.qh.head & cpu_to_le32(UHCI_PTR_TERM)))
			set_qh_head(old, MAKE_QH_ADDR (new)) ;
	}
	else {
		// (POS) (OLD) -> (POS) (NEW) (OLD)
		old = list_entry (pos->horizontal.next, uhci_desc_t, horizontal);
		list_add (&new->horizontal, &pos->horizontal);
		set_qh_head(new, MAKE_QH_ADDR (old));
		mb();
		set_qh_head(pos, MAKE_QH_ADDR (new)) ;
	}
 	mb ();
	
	spin_unlock_irqrestore (&uhci->qh_lock, flags);

	return 0;
}
/*-------------------------------------------------------------------*/
// append a qh to td.link physically, the SW linkage is not affected
static void append_qh(struct uhci_hcd *uhci, uhci_desc_t *td, uhci_desc_t* qh, int  flags)
{
	unsigned long cpuflags;
	
	spin_lock_irqsave (&uhci->td_lock, cpuflags);

	set_td_link(td, qh->dma_addr | (flags & UHCI_PTR_DEPTH) | UHCI_PTR_QH);
       
	mb();
	spin_unlock_irqrestore (&uhci->td_lock, cpuflags);
}
/*-------------------------------------------------------------------*/
static int unlink_qh (struct uhci_hcd *uhci, uhci_desc_t *element)
{
	uhci_desc_t  *prev;
	unsigned long flags;
	__u32 old_head;
	spin_lock_irqsave (&uhci->qh_lock, flags);
	
	prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);

	old_head = element->hw.qh.head;
	element->hw.qh.head = UHCI_PTR_TERM;
	mb();
	prev->hw.qh.head = old_head;

	dbg("unlink qh %p, pqh %p, nxqh %p, to %08x", element, prev, 
	    list_entry (element->horizontal.next, uhci_desc_t, horizontal),
	    le32_to_cpu(element->hw.qh.head) &~15);
	
	list_del(&element->horizontal);

	mb ();
	spin_unlock_irqrestore (&uhci->qh_lock, flags);
	
	return 0;
}
/*-------------------------------------------------------------------*/
static int delete_desc (struct uhci_hcd *uhci, uhci_desc_t *element)
{
	pci_pool_free(uhci->desc_pool, element, element->dma_addr);
	return 0;
}
/*-------------------------------------------------------------------*/
static int delete_qh (struct uhci_hcd *uhci, uhci_desc_t *qh)
{
	uhci_desc_t *td;
	struct list_head *p;
	int n=0;

	list_del (&qh->horizontal);

	while ((p = qh->vertical.next) != &qh->vertical && n<10000) {
		td = list_entry (p, uhci_desc_t, vertical);
		dbg("unlink td @ %p",td);
		unlink_td (uhci, td, 0); // no physical unlink
		delete_desc (uhci, td);
		n++;
	}
	// never trust any software, not even your own...
	if (n>=10000)
		err("delete_qh: Garbage in QH list, giving up");

	delete_desc (uhci, qh);
	
	return 0;
}
/*###########################################################################*/
//             DESCRIPTOR CHAINING HELPERS
/*###########################################################################*/

static void clean_td_chain (struct uhci_hcd *uhci, uhci_desc_t *td)
{
	struct list_head *p;
	uhci_desc_t *td1;

	if (!td)
		return;
	
	while ((p = td->horizontal.next) != &td->horizontal) {
		td1 = list_entry (p, uhci_desc_t, horizontal);
		delete_desc (uhci, td1);
	}
	
	delete_desc (uhci, td);
}
/*-------------------------------------------------------------------*/
// Cleans up collected QHs/TDs, but not more than 100 in one go
void clean_descs(struct uhci_hcd *uhci, int force)
{
	struct list_head *q;
	uhci_desc_t *qh,*td;
	int now=UHCI_GET_CURRENT_FRAME(uhci), n=0;

	q=uhci->free_desc_qh.prev;
	
	while (q != &uhci->free_desc_qh && (force || n<100)) {
		qh = list_entry (q, uhci_desc_t, horizontal);		
		q=qh->horizontal.prev;

		if ((qh->last_used!=now) || force) {
			delete_qh(uhci,qh);
		}
		n++;
	}

	q=uhci->free_desc_td.prev;
	n=0;

	while (q != &uhci->free_desc_td && (force || n<100)) {

		td = list_entry (q, uhci_desc_t, horizontal);		
		q=td->horizontal.prev;

		if (((td->last_used!=now)&&(td->last_used+1!=now)) || force) {
			list_del (&td->horizontal);
			delete_desc(uhci,td);
		}
		n++;
	}
}
/*-------------------------------------------------------------------*/
static void uhci_switch_timer_int(struct uhci_hcd *uhci)
{

	if (!list_empty(&uhci->urb_unlinked))
		set_td_ioc(uhci->td1ms);
	else
		clr_td_ioc(uhci->td1ms);

	if (uhci->timeout_urbs)
		set_td_ioc(uhci->td32ms);
	else
		clr_td_ioc(uhci->td32ms);
	wmb();
}
/*-------------------------------------------------------------------*/
static void enable_desc_loop(struct uhci_hcd *uhci, struct urb *urb)
{
	unsigned long flags;

	if (urb->transfer_flags & USB_NO_FSBR)
		return;

	spin_lock_irqsave (&uhci->qh_lock, flags);
	uhci->chain_end->hw.qh.head&=cpu_to_le32(~UHCI_PTR_TERM);
	mb();
	uhci->loop_usage++;
	((urb_priv_t*)urb->hcpriv)->use_loop=1;
	spin_unlock_irqrestore (&uhci->qh_lock, flags);
}
/*-------------------------------------------------------------------*/
static void disable_desc_loop(struct uhci_hcd *uhci, struct urb *urb)
{
	unsigned long flags;

	if (urb->transfer_flags & USB_NO_FSBR)
		return;

	spin_lock_irqsave (&uhci->qh_lock, flags);
	if (((urb_priv_t*)urb->hcpriv)->use_loop) {
		uhci->loop_usage--;

		if (!uhci->loop_usage) {
			uhci->chain_end->hw.qh.head|=cpu_to_le32(UHCI_PTR_TERM);
			mb();
		}
		((urb_priv_t*)urb->hcpriv)->use_loop=0;
	}
	spin_unlock_irqrestore (&uhci->qh_lock, flags);
}
/*-------------------------------------------------------------------*/
static void queue_urb_unlocked (struct uhci_hcd *uhci, struct urb *urb)
{
	urb_priv_t *priv=(urb_priv_t*)urb->hcpriv;
	int type;
	type=usb_pipetype (urb->pipe);
	
	if (high_bw && ((type == PIPE_BULK) || (type == PIPE_CONTROL)))
			enable_desc_loop(uhci, urb);

	urb->status = -EINPROGRESS;
	priv->started=jiffies;

	list_add (&priv->urb_list, &uhci->urb_list);
	if (urb->timeout)
		uhci->timeout_urbs++;
	uhci_switch_timer_int(uhci);
}
/*-------------------------------------------------------------------*/
static void queue_urb (struct uhci_hcd *uhci, struct urb *urb)
{
	unsigned long flags=0;

	spin_lock_irqsave (&uhci->urb_list_lock, flags);
	queue_urb_unlocked(uhci,urb);
	spin_unlock_irqrestore (&uhci->urb_list_lock, flags);
}
/*-------------------------------------------------------------------*/
static void dequeue_urb (struct uhci_hcd *uhci, struct urb *urb)
{
	urb_priv_t *priv=(urb_priv_t*)urb->hcpriv;
	int type;
	dbg("dequeue URB %p",urb);
	type=usb_pipetype (urb->pipe);

	if (high_bw && ((type == PIPE_BULK) || (type == PIPE_CONTROL)))
		disable_desc_loop(uhci, urb);

	list_del (&priv->urb_list);
	if (urb->timeout && uhci->timeout_urbs)
		uhci->timeout_urbs--;

}
/*###########################################################################*/
//                  INIT/FREE FRAME LAYOUT IN MEMORY
/*###########################################################################*/

// Removes ALL qhs in chain (paranoia!)
static void cleanup_skel (struct uhci_hcd *uhci)
{
	unsigned int n;
	uhci_desc_t *td;

	dbg("cleanup_skel");

	clean_descs(uhci,1);
	dbg("clean_descs done");

	if (uhci->td32ms) {	
		unlink_td(uhci,uhci->td32ms,1);
		delete_desc(uhci, uhci->td32ms);
	}

	if (uhci->td128ms) {	
		unlink_td(uhci,uhci->td128ms,1);
		delete_desc(uhci, uhci->td128ms);
	}

	for (n = 0; n < 8; n++) {
		td = uhci->int_chain[n];
		clean_td_chain (uhci, td);
	}

	if (uhci->iso_td) {
		for (n = 0; n < 1024; n++) {
			td = uhci->iso_td[n];
			clean_td_chain (uhci, td);
		}
		kfree (uhci->iso_td);
	}

	if (uhci->framelist)
		pci_free_consistent(uhci->uhci_pci, PAGE_SIZE,
				    uhci->framelist, uhci->framelist_dma);

	if (uhci->control_chain) {
		// completed init_skel?
		struct list_head *p;
		uhci_desc_t *qh, *qh1;

		qh = uhci->control_chain;
		while ((p = qh->horizontal.next) != &qh->horizontal) {
			qh1 = list_entry (p, uhci_desc_t, horizontal);
			delete_qh (uhci, qh1);
		}

		delete_qh (uhci, qh);
	}
	else {
		if (uhci->ls_control_chain)
			delete_desc (uhci, uhci->ls_control_chain);
		if (uhci->control_chain)
			delete_desc (uhci, uhci->control_chain);
		if (uhci->bulk_chain)
			delete_desc (uhci, uhci->bulk_chain);
		if (uhci->chain_end)
			delete_desc (uhci, uhci->chain_end);
	}

	if (uhci->desc_pool) {
		pci_pool_destroy(uhci->desc_pool);
		uhci->desc_pool = NULL;
	}

	uhci->ls_control_chain = NULL;
	uhci->control_chain = NULL;
	uhci->bulk_chain = NULL;
	uhci->chain_end = NULL;
	for (n = 0; n < 8; n++)
		uhci->int_chain[n] = NULL;
	dbg("cleanup_skel finished");	
}
/*-------------------------------------------------------------------*/
// allocates framelist and qh-skeletons
// only HW-links provide continous linking, SW-links stay in their domain (ISO/INT)
static int init_skel (struct uhci_hcd *uhci)
{
	int n, ret;
	uhci_desc_t *qh, *td;
	
	init_dbg("init_skel");
	
	uhci->framelist = pci_alloc_consistent(uhci->uhci_pci, PAGE_SIZE,
					    &uhci->framelist_dma);

	if (!uhci->framelist)
		return -ENOMEM;

	memset (uhci->framelist, 0, 4096);

	init_dbg("creating descriptor pci_pool");
	uhci->desc_pool = pci_pool_create("uhci_desc", uhci->uhci_pci,
				       sizeof(uhci_desc_t), 16, 0,
				       GFP_DMA | GFP_ATOMIC);	
	if (!uhci->desc_pool)
		goto init_skel_cleanup;

	init_dbg("allocating iso desc pointer list");
	uhci->iso_td = (uhci_desc_t **) kmalloc (1024 * sizeof (uhci_desc_t*), GFP_KERNEL);
	
	if (!uhci->iso_td)
		goto init_skel_cleanup;

	uhci->ls_control_chain = NULL;
	uhci->control_chain = NULL;
	uhci->bulk_chain = NULL;
	uhci->chain_end = NULL;
	for (n = 0; n < 8; n++)
		uhci->int_chain[n] = NULL;

	init_dbg("allocating iso descs");
	for (n = 0; n < 1024; n++) {
	 	// allocate skeleton iso/irq-tds
		if (alloc_td (uhci, &td, 0))
			goto init_skel_cleanup;

		uhci->iso_td[n] = td;
		uhci->framelist[n] = cpu_to_le32((__u32) td->dma_addr);
	}

	init_dbg("allocating qh: chain_end");
	if (alloc_qh (uhci, &qh))	
		goto init_skel_cleanup;
				
	uhci->chain_end = qh;

	if (alloc_td (uhci, &td, 0))
		goto init_skel_cleanup;
	
	fill_td (td, 0 * TD_CTRL_IOC, 0, 0); // generate 1ms interrupt (enabled on demand)
	insert_td (uhci, qh, td, 0);
	qh->hw.qh.element &= cpu_to_le32(~UHCI_PTR_TERM); // remove TERM bit
	uhci->td1ms=td;

	dbg("allocating qh: bulk_chain");
	if (alloc_qh (uhci, &qh))
		goto init_skel_cleanup;
	
	insert_qh (uhci, uhci->chain_end, qh, 0);
	uhci->bulk_chain = qh;

	dbg("allocating qh: control_chain");
	if ((ret = alloc_qh (uhci, &qh)))
		goto init_skel_cleanup;
	
	insert_qh (uhci, uhci->bulk_chain, qh, 0);
	uhci->control_chain = qh;

	// disabled reclamation loop
	if (high_bw)
		set_qh_head(uhci->chain_end, uhci->control_chain->dma_addr | UHCI_PTR_QH | UHCI_PTR_TERM);


	init_dbg("allocating qh: ls_control_chain");
	if (alloc_qh (uhci, &qh))
		goto init_skel_cleanup;
	
	insert_qh (uhci, uhci->control_chain, qh, 0);
	uhci->ls_control_chain = qh;

	init_dbg("allocating skeleton INT-TDs");
	
	for (n = 0; n < 8; n++) {
		uhci_desc_t *td;

		if (alloc_td (uhci, &td, 0))
			goto init_skel_cleanup;

		uhci->int_chain[n] = td;
		if (n == 0) {
			set_td_link(uhci->int_chain[0], uhci->ls_control_chain->dma_addr | UHCI_PTR_QH);
		}
		else {
			set_td_link(uhci->int_chain[n], uhci->int_chain[0]->dma_addr);
		}
	}

	init_dbg("Linking skeleton INT-TDs");
	
	for (n = 0; n < 1024; n++) {
		// link all iso-tds to the interrupt chains
		int m, o;
		dbg("framelist[%i]=%x",n,le32_to_cpu(uhci->framelist[n]));
		if ((n&127)==127) 
			((uhci_desc_t*) uhci->iso_td[n])->hw.td.link = cpu_to_le32(uhci->int_chain[0]->dma_addr);
		else 
			for (o = 1, m = 2; m <= 128; o++, m += m)
				if ((n & (m - 1)) == ((m - 1) / 2))
					set_td_link(((uhci_desc_t*) uhci->iso_td[n]), uhci->int_chain[o]->dma_addr);
	}

	if (alloc_td (uhci, &td, 0))
		goto init_skel_cleanup;
	
	fill_td (td, 0 * TD_CTRL_IOC, 0, 0); // generate 32ms interrupt (activated later)
	uhci->td32ms=td;
	insert_td_horizontal (uhci, uhci->int_chain[5], td);

	if (alloc_td (uhci, &td, 0))
		goto init_skel_cleanup;
	
	fill_td (td, 0 * TD_CTRL_IOC, 0, 0); // generate 128ms interrupt (activated later)
	uhci->td128ms=td;
	insert_td_horizontal (uhci, uhci->int_chain[7], td);

	mb();
	init_dbg("init_skel exit");
	return 0;

  init_skel_cleanup:
	cleanup_skel (uhci);
	return -ENOMEM;
}
/*###########################################################################*/
//                 UHCI PRIVATE DATA
/*###########################################################################*/

urb_priv_t *uhci_alloc_priv(int mem_flags)
{
	urb_priv_t *p;
#ifdef DEBUG_SLAB
	p = kmem_cache_alloc(urb_priv_kmem, SLAB_FLAG);
#else
	p = kmalloc (sizeof (urb_priv_t), mem_flags);
#endif
	if (p) {
		memset(p, 0, sizeof(urb_priv_t));
		INIT_LIST_HEAD (&p->urb_list);
	}
	return p;
}
/*-------------------------------------------------------------------*/
void uhci_free_priv(struct uhci_hcd *uhci, struct urb *urb, urb_priv_t* p)
{
		uhci_urb_dma_unmap(uhci, urb, p);
#ifdef DEBUG_SLAB
		err("free_priv %p",p);
		kmem_cache_free(urb_priv_kmem, p);
#else
		kfree (p);
#endif
		urb->hcpriv = NULL;
}

