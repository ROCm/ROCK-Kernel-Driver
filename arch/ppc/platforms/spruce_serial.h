/*
 * include/asm-ppc/spruce_serial.h
 * 
 * Definitions for IBM Spruce reference board support
 *
 * Authors: Matt Porter and Johnnie Peters
 *          mporter@mvista.com
 *          jpeters@mvista.com
 *
 * Copyright 2000 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASMPPC_SPRUCE_SERIAL_H
#define __ASMPPC_SPRUCE_SERIAL_H

#include <linux/config.h>

/* This is where the serial ports exist */
#define SPRUCE_SERIAL_1_ADDR	0xff600300
#define SPRUCE_SERIAL_2_ADDR	0xff600400

#define RS_TABLE_SIZE  4

/* Rate for the baud clock for the onboard serial chip */
#ifndef CONFIG_SPRUCE_BAUD_33M
#define BASE_BAUD  (30000000 / 4 / 16)
#else
#define BASE_BAUD  (33000000 / 4 / 16)
#endif

#ifndef SERIAL_MAGIC_KEY
#define kernel_debugger ppc_kernel_debug
#endif

#ifdef CONFIG_SERIAL_DETECT_IRQ
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST|ASYNC_AUTO_IRQ)
#else
#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST)
#endif

#define STD_SERIAL_PORT_DFNS \
        { 0, BASE_BAUD, SPRUCE_SERIAL_1_ADDR, 3, STD_COM_FLAGS,	/* ttyS0 */ \
		iomem_base: (u8 *)SPRUCE_SERIAL_1_ADDR,			    \
		io_type: SERIAL_IO_MEM },				    \
        { 0, BASE_BAUD, SPRUCE_SERIAL_2_ADDR, 4, STD_COM_FLAGS,	/* ttyS1 */ \
		iomem_base: (u8 *)SPRUCE_SERIAL_2_ADDR,			    \
		io_type: SERIAL_IO_MEM },

#define SERIAL_PORT_DFNS \
        STD_SERIAL_PORT_DFNS

#endif /* __ASMPPC_SPRUCE_SERIAL_H */
