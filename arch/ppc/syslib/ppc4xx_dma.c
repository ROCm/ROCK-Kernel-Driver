/*
 * Author: Pete Popov <ppopov@mvista.com> or source@mvista.com
 *
 * arch/ppc/kernel/ppc405_dma.c
 *
 * 2000 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * IBM 405 DMA Controller Functions
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/ppc405_dma.h>


/*
 * Function prototypes
 */

int hw_init_dma_channel(unsigned int,  ppc_dma_ch_t *);
int init_dma_channel(unsigned int);
int get_channel_config(unsigned int, ppc_dma_ch_t *);
int set_channel_priority(unsigned int, unsigned int);
unsigned int get_peripheral_width(unsigned int);
int alloc_dma_handle(sgl_handle_t *, unsigned int, unsigned int);
void free_dma_handle(sgl_handle_t);


ppc_dma_ch_t dma_channels[MAX_405GP_DMA_CHANNELS];

/*
 * Configures a DMA channel, including the peripheral bus width, if a
 * peripheral is attached to the channel, the polarity of the DMAReq and
 * DMAAck signals, etc.  This information should really be setup by the boot
 * code, since most likely the configuration won't change dynamically.
 * If the kernel has to call this function, it's recommended that it's
 * called from platform specific init code.  The driver should not need to
 * call this function.
 */
int hw_init_dma_channel(unsigned int dmanr,  ppc_dma_ch_t *p_init)
{
    unsigned int polarity;
    uint32_t control = 0;
    ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];

#ifdef DEBUG_405DMA
    if (!p_init) {
        printk("hw_init_dma_channel: NULL p_init\n");
        return DMA_STATUS_NULL_POINTER;
    }
    if (dmanr >= MAX_405GP_DMA_CHANNELS) {
        printk("hw_init_dma_channel: bad channel %d\n", dmanr);
        return DMA_STATUS_BAD_CHANNEL;
    }
#endif

#if DCRN_POL > 0
    polarity = mfdcr(DCRN_POL);
#else
    polarity = 0;
#endif

    /* Setup the control register based on the values passed to
     * us in p_init.  Then, over-write the control register with this
     * new value.
     */

    control |= (
                SET_DMA_CIE_ENABLE(p_init->int_enable) | /* interrupt enable         */
                SET_DMA_BEN(p_init->buffer_enable)     | /* buffer enable            */
                SET_DMA_ETD(p_init->etd_output)        | /* end of transfer pin      */
                SET_DMA_TCE(p_init->tce_enable)        | /* terminal count enable    */
                SET_DMA_PL(p_init->pl)                 | /* peripheral location      */
                SET_DMA_DAI(p_init->dai)               | /* dest addr increment      */
                SET_DMA_SAI(p_init->sai)               | /* src addr increment       */
                SET_DMA_PRIORITY(p_init->cp)           |  /* channel priority        */
                SET_DMA_PW(p_init->pwidth)             |  /* peripheral/bus width    */
                SET_DMA_PSC(p_init->psc)               |  /* peripheral setup cycles */
                SET_DMA_PWC(p_init->pwc)               |  /* peripheral wait cycles  */
                SET_DMA_PHC(p_init->phc)               |  /* peripheral hold cycles  */
                SET_DMA_PREFETCH(p_init->pf)              /* read prefetch           */
                );

    switch (dmanr) {
        case 0:
            /* clear all polarity signals and then "or" in new signal levels */
            polarity &= ~(DMAReq0_ActiveLow | DMAAck0_ActiveLow | EOT0_ActiveLow);
            polarity |= p_dma_ch->polarity;
#if DCRN_POL > 0
            mtdcr(DCRN_POL, polarity);
#endif
            mtdcr(DCRN_DMACR0, control);
            break;
        case 1:
            polarity &= ~(DMAReq1_ActiveLow | DMAAck1_ActiveLow | EOT1_ActiveLow);
            polarity |= p_dma_ch->polarity;
#if DCRN_POL > 0
            mtdcr(DCRN_POL, polarity);
#endif
            mtdcr(DCRN_DMACR1, control);
            break;
        case 2:
            polarity &= ~(DMAReq2_ActiveLow | DMAAck2_ActiveLow | EOT2_ActiveLow);
            polarity |= p_dma_ch->polarity;
#if DCRN_POL > 0
            mtdcr(DCRN_POL, polarity);
#endif
            mtdcr(DCRN_DMACR2, control);
            break;
        case 3:
            polarity &= ~(DMAReq3_ActiveLow | DMAAck3_ActiveLow | EOT3_ActiveLow);
            polarity |= p_dma_ch->polarity;
#if DCRN_POL > 0
            mtdcr(DCRN_POL, polarity);
#endif
            mtdcr(DCRN_DMACR3, control);
            break;
        default:
            return DMA_STATUS_BAD_CHANNEL;
    }

    /* save these values in our dma channel structure */
    memcpy(p_dma_ch, p_init, sizeof(ppc_dma_ch_t));

    /*
     * The peripheral width values written in the control register are:
     *   PW_8                 0
     *   PW_16                1
     *   PW_32                2
     *   PW_64                3
     *
     *   Since the DMA count register takes the number of "transfers",
     *   we need to divide the count sent to us in certain
     *   functions by the appropriate number.  It so happens that our
     *   right shift value is equal to the peripheral width value.
     */
    p_dma_ch->shift = p_init->pwidth;

    /*
     * Save the control word for easy access.
     */
    p_dma_ch->control = control;

    mtdcr(DCRN_DMASR, 0xffffffff); /* clear status register */
    return DMA_STATUS_GOOD;
}




/*
 * This function returns the channel configuration.
 */
int get_channel_config(unsigned int dmanr, ppc_dma_ch_t *p_dma_ch)
{
    unsigned int polarity;
    unsigned int control;

#if DCRN_POL > 0
    polarity = mfdcr(DCRN_POL);
#else
    polarity = 0;
#endif

    switch (dmanr) {
        case 0:
            p_dma_ch->polarity =
                polarity & (DMAReq0_ActiveLow | DMAAck0_ActiveLow | EOT0_ActiveLow);
            control = mfdcr(DCRN_DMACR0);
            break;
        case 1:
            p_dma_ch->polarity =
                polarity & (DMAReq1_ActiveLow | DMAAck1_ActiveLow | EOT1_ActiveLow);
            control = mfdcr(DCRN_DMACR1);
            break;
        case 2:
            p_dma_ch->polarity =
                polarity & (DMAReq2_ActiveLow | DMAAck2_ActiveLow | EOT2_ActiveLow);
            control = mfdcr(DCRN_DMACR2);
            break;
        case 3:
            p_dma_ch->polarity =
                polarity & (DMAReq3_ActiveLow | DMAAck3_ActiveLow | EOT3_ActiveLow);
            control = mfdcr(DCRN_DMACR3);
            break;
        default:
            return DMA_STATUS_BAD_CHANNEL;
    }

    p_dma_ch->cp = GET_DMA_PRIORITY(control);
    p_dma_ch->pwidth = GET_DMA_PW(control);
    p_dma_ch->psc = GET_DMA_PSC(control);
    p_dma_ch->pwc = GET_DMA_PWC(control);
    p_dma_ch->phc = GET_DMA_PHC(control);
    p_dma_ch->pf = GET_DMA_PREFETCH(control);
    p_dma_ch->int_enable = GET_DMA_CIE_ENABLE(control);
    p_dma_ch->shift = GET_DMA_PW(control);

    return DMA_STATUS_GOOD;
}

/*
 * Sets the priority for the DMA channel dmanr.
 * Since this is setup by the hardware init function, this function
 * can be used to dynamically change the priority of a channel.
 *
 * Acceptable priorities:
 *
 * PRIORITY_LOW
 * PRIORITY_MID_LOW
 * PRIORITY_MID_HIGH
 * PRIORITY_HIGH
 *
 */
int set_channel_priority(unsigned int dmanr, unsigned int priority)
{
    unsigned int control;

#ifdef DEBUG_405DMA
    if ( (priority != PRIORITY_LOW) &&
            (priority != PRIORITY_MID_LOW) &&
            (priority != PRIORITY_MID_HIGH) &&
            (priority != PRIORITY_HIGH)) {
        printk("set_channel_priority: bad priority: 0x%x\n", priority);
    }
#endif

    switch (dmanr) {
        case 0:
            control = mfdcr(DCRN_DMACR0);
            control|= SET_DMA_PRIORITY(priority);
            mtdcr(DCRN_DMACR0, control);
            break;
        case 1:
            control = mfdcr(DCRN_DMACR1);
            control|= SET_DMA_PRIORITY(priority);
            mtdcr(DCRN_DMACR1, control);
            break;
        case 2:
            control = mfdcr(DCRN_DMACR2);
            control|= SET_DMA_PRIORITY(priority);
            mtdcr(DCRN_DMACR2, control);
            break;
        case 3:
            control = mfdcr(DCRN_DMACR3);
            control|= SET_DMA_PRIORITY(priority);
            mtdcr(DCRN_DMACR3, control);
            break;
        default:
#ifdef DEBUG_405DMA
            printk("set_channel_priority: bad channel: %d\n", dmanr);
#endif
            return DMA_STATUS_BAD_CHANNEL;
    }
    return DMA_STATUS_GOOD;
}



/*
 * Returns the width of the peripheral attached to this channel. This assumes
 * that someone who knows the hardware configuration, boot code or some other
 * init code, already set the width.
 *
 * The return value is one of:
 *   PW_8
 *   PW_16
 *   PW_32
 *   PW_64
 *
 *   The function returns 0 on error.
 */
unsigned int get_peripheral_width(unsigned int dmanr)
{
    unsigned int control;

    switch (dmanr) {
        case 0:
            control = mfdcr(DCRN_DMACR0);
            break;
        case 1:
            control = mfdcr(DCRN_DMACR1);
            break;
        case 2:
            control = mfdcr(DCRN_DMACR2);
            break;
        case 3:
            control = mfdcr(DCRN_DMACR3);
            break;
        default:
#ifdef DEBUG_405DMA
            printk("get_peripheral_width: bad channel: %d\n", dmanr);
#endif
            return 0;
    }
    return(GET_DMA_PW(control));
}




/*
 *   Create a scatter/gather list handle.  This is simply a structure which
 *   describes a scatter/gather list.
 *
 *   A handle is returned in "handle" which the driver should save in order to
 *   be able to access this list later.  A chunk of memory will be allocated
 *   to be used by the API for internal management purposes, including managing
 *   the sg list and allocating memory for the sgl descriptors.  One page should
 *   be more than enough for that purpose.  Perhaps it's a bit wasteful to use
 *   a whole page for a single sg list, but most likely there will be only one
 *   sg list per channel.
 *
 *   Interrupt notes:
 *   Each sgl descriptor has a copy of the DMA control word which the DMA engine
 *   loads in the control register.  The control word has a "global" interrupt
 *   enable bit for that channel. Interrupts are further qualified by a few bits
 *   in the sgl descriptor count register.  In order to setup an sgl, we have to
 *   know ahead of time whether or not interrupts will be enabled at the completion
 *   of the transfers.  Thus, enable_dma_interrupt()/disable_dma_interrupt() MUST
 *   be called before calling alloc_dma_handle().  If the interrupt mode will never
 *   change after powerup, then enable_dma_interrupt()/disable_dma_interrupt()
 *   do not have to be called -- interrupts will be enabled or disabled based
 *   on how the channel was configured after powerup by the hw_init_dma_channel()
 *   function.  Each sgl descriptor will be setup to interrupt if an error occurs;
 *   however, only the last descriptor will be setup to interrupt. Thus, an
 *   interrupt will occur (if interrupts are enabled) only after the complete
 *   sgl transfer is done.
 */
int alloc_dma_handle(sgl_handle_t *phandle, unsigned int mode, unsigned int dmanr)
{
    sgl_list_info_t *psgl;
    dma_addr_t dma_addr;
    ppc_dma_ch_t *p_dma_ch = &dma_channels[dmanr];
    uint32_t sg_command;
    void *ret;

#ifdef DEBUG_405DMA
    if (!phandle) {
            printk("alloc_dma_handle: null handle pointer\n");
            return DMA_STATUS_NULL_POINTER;
    }
    switch (mode) {
        case DMA_MODE_READ:
        case DMA_MODE_WRITE:
        case DMA_MODE_MM:
        case DMA_MODE_MM_DEVATSRC:
        case DMA_MODE_MM_DEVATDST:
            break;
        default:
            printk("alloc_dma_handle: bad mode 0x%x\n", mode);
            return DMA_STATUS_BAD_MODE;
    }
    if (dmanr >= MAX_405GP_DMA_CHANNELS) {
        printk("alloc_dma_handle: invalid channel 0x%x\n", dmanr);
        return DMA_STATUS_BAD_CHANNEL;
    }
#endif

    /* Get a page of memory, which is zeroed out by pci_alloc_consistent() */

/* wrong not a pci device - armin */
    /* psgl = (sgl_list_info_t *) pci_alloc_consistent(NULL, SGL_LIST_SIZE, &dma_addr);
*/

	ret = consistent_alloc(GFP_ATOMIC |GFP_DMA, SGL_LIST_SIZE, &dma_addr);
	if (ret != NULL) {
		memset(ret, 0,SGL_LIST_SIZE );
		psgl = (sgl_list_info_t *) ret;
	}


    if (psgl == NULL) {
        *phandle = (sgl_handle_t)NULL;
        return DMA_STATUS_OUT_OF_MEMORY;
    }

    psgl->dma_addr = dma_addr;
    psgl->dmanr = dmanr;

    /*
     * Modify and save the control word. These word will get written to each sgl
     * descriptor.  The DMA engine then loads this control word into the control
     * register every time it reads a new descriptor.
     */
    psgl->control = p_dma_ch->control;
    psgl->control &= ~(DMA_TM_MASK | DMA_TD);  /* clear all "mode" bits first               */
    psgl->control |= (mode | DMA_CH_ENABLE);   /* save the control word along with the mode */

    if (p_dma_ch->int_enable) {
        psgl->control |= DMA_CIE_ENABLE;       /* channel interrupt enabled                 */
    }
    else {
        psgl->control &= ~DMA_CIE_ENABLE;
    }

#if DCRN_ASGC > 0
    sg_command = mfdcr(DCRN_ASGC);
    switch (dmanr) {
        case 0:
            sg_command |= SSG0_MASK_ENABLE;
            break;
        case 1:
            sg_command |= SSG1_MASK_ENABLE;
            break;
        case 2:
            sg_command |= SSG2_MASK_ENABLE;
            break;
        case 3:
            sg_command |= SSG3_MASK_ENABLE;
            break;
        default:
#ifdef DEBUG_405DMA
            printk("alloc_dma_handle: bad channel: %d\n", dmanr);
#endif
            free_dma_handle((sgl_handle_t)psgl);
            *phandle = (sgl_handle_t)NULL;
            return DMA_STATUS_BAD_CHANNEL;
    }

    mtdcr(DCRN_ASGC, sg_command);  /* enable writing to this channel's sgl control bits */
#else
   (void)sg_command;
#endif
    psgl->sgl_control = SG_ERI_ENABLE | SG_LINK;   /* sgl descriptor control bits */

    if (p_dma_ch->int_enable) {
        if (p_dma_ch->tce_enable)
            psgl->sgl_control |= SG_TCI_ENABLE;
        else
            psgl->sgl_control |= SG_ETI_ENABLE;
    }

    *phandle = (sgl_handle_t)psgl;
    return DMA_STATUS_GOOD;
}



/*
 * Destroy a scatter/gather list handle that was created by alloc_dma_handle().
 * The list must be empty (contain no elements).
 */
void free_dma_handle(sgl_handle_t handle)
{
    sgl_list_info_t *psgl = (sgl_list_info_t *)handle;

    if (!handle) {
#ifdef DEBUG_405DMA
        printk("free_dma_handle: got NULL\n");
#endif
        return;
    }
    else if (psgl->phead) {
#ifdef DEBUG_405DMA
        printk("free_dma_handle: list not empty\n");
#endif
        return;
    }
    else if (!psgl->dma_addr) { /* should never happen */
#ifdef DEBUG_405DMA
        printk("free_dma_handle: no dma address\n");
#endif
        return;
    }

  /* wrong not a PCI device -armin */
  /*  pci_free_consistent(NULL, SGL_LIST_SIZE, (void *)psgl, psgl->dma_addr); */
	//	free_pages((unsigned long)psgl, get_order(SGL_LIST_SIZE));
    	consistent_free((void *)psgl);


}


EXPORT_SYMBOL(hw_init_dma_channel);
EXPORT_SYMBOL(get_channel_config);
EXPORT_SYMBOL(set_channel_priority);
EXPORT_SYMBOL(get_peripheral_width);
EXPORT_SYMBOL(alloc_dma_handle);
EXPORT_SYMBOL(free_dma_handle);
EXPORT_SYMBOL(dma_channels);
