/*
 * ocp.c
 *
 *	The is drived from pci.c
 *
 * 	Current Maintainer
 *      Armin Kuster akuster@pacbell.net
 *      Jan, 2002
 *
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
 */
#include <linux/list.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <asm/ocp.h>


static int __init
ocparch_init(void)
{
	struct ocp_bus *bus;
	int next_hostno, max_hostno;

	printk(KERN_INFO "OCP: Probing OCP hardware\n");
	max_hostno = 1;
	/* Scan all of the recorded OCP controllers.  */
	for (next_hostno = 0;next_hostno < max_hostno; next_hostno++) {
		bus = ocp_scan_bus(next_hostno, NULL);
	}
	return 0;
}

subsys_initcall(ocparch_init);

