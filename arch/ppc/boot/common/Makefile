#
# arch/ppc/boot/common/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Tom Rini	January 2001
#

lib-y					:= string.o util.o misc-common.o \
						serial_stub.o
lib-$(CONFIG_PPC_PREP)			+= mpc10x_memory.o
lib-$(CONFIG_LOPEC)			+= mpc10x_memory.o
lib-$(CONFIG_PAL4)			+= cpc700_memory.o
lib-$(CONFIG_SERIAL_8250_CONSOLE)	+= ns16550.o
