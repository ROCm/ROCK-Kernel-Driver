/* Linux ISDN subsystem, protocol encapsulation
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

extern struct concap_device_ops isdn_concap_reliable_dl_dops;
extern struct concap_device_ops isdn_concap_demand_dial_dops;

struct concap_proto *isdn_concap_new(int);

extern struct isdn_netif_ops isdn_x25_ops;
