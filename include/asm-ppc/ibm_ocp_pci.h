/*
 * ibm_ocp_pci.h
 *
 *	This was derived from the ibm_ocp.h 
 *
 * 	Current Maintainer
 *      Armin Kuster akuster@mvista.com
 *      AUg, 2002 
 *
 *
 * Copyright 2001-2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM_OCP_PCI_H__
#define __ASM_IBM_OCP_PCI_H__

/* PCI 32 */

struct pmm_regs {
	u32 la;
	u32 ma;
	u32 pcila;
	u32 pciha;
};

typedef struct pcil0_regs {
	struct pmm_regs pmm[3];
	u32 ptm1ms;
	u32 ptm1la;
	u32 ptm2ms;
	u32 ptm2la;
} pci0_t;

#endif				/* __ASM_IBM_OCP_PCI_H__ */
#endif				/* __KERNEL__ */
