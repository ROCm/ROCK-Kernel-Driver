/*
 *	file: ppc4xx_pm.c
 *
 *	This an attempt to get Power Management going for the IBM 4xx processor.
 *	This was derived from the ppc4xx._setup.c file
 *
 *      Armin Kuster akuster@mvista.com
 *      Jan  2002
 *
 *
 * Copyright 2002 MontaVista Softare Inc.
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
 *	Version 1.0 (02/14/01) - A. Kuster
 *	Initial version	 - moved pm code from ppc4xx_setup.c
 *
 *	1.1 02/21/01 - A. Kuster
 *		minor fixes, init value to 0 & += to &=
 *		added stb03 ifdef for 2nd i2c device
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/ibm4xx.h>

void __init
ppc4xx_pm_init(void)
{

	unsigned int value = 0;

	/* turn off unused hardware to save power */
#ifdef CONFIG_405GP
	value |= CPM_DCP;	/* CodePack */
#endif

#if !defined(CONFIG_IBM_OCP_GPIO)
	value |= CPM_GPIO0;
#endif

#if !defined(CONFIG_PPC405_I2C_ADAP)
	value |= CPM_IIC0;
#ifdef CONFIG_STB03xxx
	value |= CPM_IIC1;
#endif
#endif


#if !defined(CONFIG_405_DMA)
	value |= CPM_DMA;
#endif

	mtdcr(DCRN_CPMFR, value);

}
