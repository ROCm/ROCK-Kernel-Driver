/*
 *
 *    Copyright 2000-2001 MontaVista Software Inc.
 *      Completed implementation.
 *	Current maintainer
 *      Armin Kuster akuster@mvista.com
 *
 *    Module name: ibm405gp.c
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
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/ocp.h>
#include <platforms/4xx/ibm405gp.h>

struct ocp_def core_ocp[]  __initdata = {
	{OCP_VENDOR_IBM, OCP_FUNC_OPB, OPB_BASE_START, OCP_IRQ_NA, OCP_CPM_NA},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, UART0_IO_BASE, UART0_INT,IBM_CPM_UART0},
	{OCP_VENDOR_IBM, OCP_FUNC_16550, UART1_IO_BASE, UART1_INT, IBM_CPM_UART1},
	{OCP_VENDOR_IBM, OCP_FUNC_IIC, IIC0_BASE, IIC0_IRQ, IBM_CPM_IIC0},
	{OCP_VENDOR_IBM, OCP_FUNC_GPIO, GPIO0_BASE, OCP_IRQ_NA, IBM_CPM_GPIO0},
	{OCP_VENDOR_IBM, OCP_FUNC_EMAC, EMAC0_BASE, BL_MAC_ETH0, IBM_CPM_EMAC0},
	{OCP_VENDOR_INVALID, OCP_FUNC_INVALID, 0x0, OCP_IRQ_NA, OCP_CPM_NA},

};
