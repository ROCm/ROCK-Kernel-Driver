/*
 *
 *    Copyright 2000-2001 MontaVista Software Inc.
 *      Completed implementation.
 *	Current maintainer
 *      Armin Kuster akuster@mvista.com
 *
 *    Module name: ibmnp405l.c
 *
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
 *
 *      History: 12/26/2001 - armin
 *		initial release
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/param.h>
#include <linux/string.h>
#include <platforms/ibmnp405l.h>

const struct NS16550* COM_PORTS[] =
{
  (struct NS16550*) UART0_IO_BASE,
  (struct NS16550*) UART1_IO_BASE,
};

const struct iic_regs* IIC_ADDR[]=
{
	(struct iic_regs*) IIC0_BASE,
};


const struct gpio_regs* GPIO_ADDR[] =
{
	(struct gpio_regs*)	GPIO0_BASE,
};

const struct emac_regs* EMAC_ADDR[]=
{
	(struct emac_regs*) EMAC0_BASE,
	(struct emac_regs*) EMAC1_BASE
};

const struct zmii_regs* ZMII_ADDR[]=
{
	(zmii_t*)	ZMII0_BASE,
};
