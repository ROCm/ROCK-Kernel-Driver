/*
 *
 *    Copyright 2000-2002 MontaVista Software Inc.
 *      Completed implementation.
 *	Current maintainer
 *      Armin Kuster akuster@mvista.com
 *
 *    Module name: virtex-ii_pro.c
 *Xilinx Manua Loa 2 evaluation board initialization
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
#include "virtex-ii_pro.h"

/* Have OCP take care of the serial ports. */
struct ocp_def core_ocp[] = {
#ifdef XPAR_UARTNS550_0_BASEADDR
	{UART, XPAR_UARTNS550_0_BASEADDR, 31 - XPAR_INTC_0_UARTNS550_0_VEC_ID,
	 OCP_CPM_NA},
#ifdef XPAR_UARTNS550_1_BASEADDR
	{UART, XPAR_UARTNS550_1_BASEADDR, 31 - XPAR_INTC_0_UARTNS550_1_VEC_ID,
	 OCP_CPM_NA},
#ifdef XPAR_UARTNS550_2_BASEADDR
	{UART, XPAR_UARTNS550_2_BASEADDR, 31 - XPAR_INTC_0_UARTNS550_2_VEC_ID,
	 OCP_CPM_NA},
#ifdef XPAR_UARTNS550_3_BASEADDR
	{UART, XPAR_UARTNS550_3_BASEADDR, 31 - XPAR_INTC_0_UARTNS550_3_VEC_ID,
	 OCP_CPM_NA},
#ifdef XPAR_UARTNS550_4_BASEADDR
#error Edit this file to add more devices.
#endif				/* 4 */
#endif				/* 3 */
#endif				/* 2 */
#endif				/* 1 */
#endif				/* 0 */
	{OCP_NULL_TYPE, 0x0, OCP_IRQ_NA, OCP_CPM_NA}
};

