/*

	mii.c: MII interface library

	Maintained by Jeff Garzik <jgarzik@mandrakesoft.com>
	Copyright 2001 Jeff Garzik

 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>

int mii_ethtool_gset(struct mii_if_info *mii, struct ethtool_cmd *ecmd)
{
	struct net_device *dev = mii->dev;
	u32 advert, bmcr, lpa, nego;

	ecmd->supported =
	    (SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
	     SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
	     SUPPORTED_Autoneg | SUPPORTED_TP | SUPPORTED_MII);

	/* only supports twisted-pair */
	ecmd->port = PORT_MII;

	/* only supports internal transceiver */
	ecmd->transceiver = XCVR_INTERNAL;

	/* this isn't fully supported at higher layers */
	ecmd->phy_address = mii->phy_id;

	ecmd->advertising = ADVERTISED_TP | ADVERTISED_MII;
	advert = mii->mdio_read(dev, mii->phy_id, MII_ADVERTISE);
	if (advert & ADVERTISE_10HALF)
		ecmd->advertising |= ADVERTISED_10baseT_Half;
	if (advert & ADVERTISE_10FULL)
		ecmd->advertising |= ADVERTISED_10baseT_Full;
	if (advert & ADVERTISE_100HALF)
		ecmd->advertising |= ADVERTISED_100baseT_Half;
	if (advert & ADVERTISE_100FULL)
		ecmd->advertising |= ADVERTISED_100baseT_Full;

	bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
	lpa = mii->mdio_read(dev, mii->phy_id, MII_LPA);
	if (bmcr & BMCR_ANENABLE) {
		ecmd->advertising |= ADVERTISED_Autoneg;
		ecmd->autoneg = AUTONEG_ENABLE;
		
		nego = mii_nway_result(advert & lpa);
		if (nego == LPA_100FULL || nego == LPA_100HALF)
			ecmd->speed = SPEED_100;
		else
			ecmd->speed = SPEED_10;
		if (nego == LPA_100FULL || nego == LPA_10FULL) {
			ecmd->duplex = DUPLEX_FULL;
			mii->full_duplex = 1;
		} else {
			ecmd->duplex = DUPLEX_HALF;
			mii->full_duplex = 0;
		}
	} else {
		ecmd->autoneg = AUTONEG_DISABLE;

		ecmd->speed = (bmcr & BMCR_SPEED100) ? SPEED_100 : SPEED_10;
		ecmd->duplex = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
	}

	/* ignore maxtxpkt, maxrxpkt for now */

	return 0;
}

int mii_ethtool_sset(struct mii_if_info *mii, struct ethtool_cmd *ecmd)
{
	struct net_device *dev = mii->dev;

	if (ecmd->speed != SPEED_10 && ecmd->speed != SPEED_100)
		return -EINVAL;
	if (ecmd->duplex != DUPLEX_HALF && ecmd->duplex != DUPLEX_FULL)
		return -EINVAL;
	if (ecmd->port != PORT_MII)
		return -EINVAL;
	if (ecmd->transceiver != XCVR_INTERNAL)
		return -EINVAL;
	if (ecmd->phy_address != mii->phy_id)
		return -EINVAL;
	if (ecmd->autoneg != AUTONEG_DISABLE && ecmd->autoneg != AUTONEG_ENABLE)
		return -EINVAL;
				  
	/* ignore supported, maxtxpkt, maxrxpkt */
	
	if (ecmd->autoneg == AUTONEG_ENABLE) {
		u32 bmcr, advert, tmp;

		if ((ecmd->advertising & (ADVERTISED_10baseT_Half |
					  ADVERTISED_10baseT_Full |
					  ADVERTISED_100baseT_Half |
					  ADVERTISED_100baseT_Full)) == 0)
			return -EINVAL;

		/* advertise only what has been requested */
		advert = mii->mdio_read(dev, mii->phy_id, MII_ADVERTISE);
		tmp = advert & ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
		if (ADVERTISED_10baseT_Half)
			tmp |= ADVERTISE_10HALF;
		if (ADVERTISED_10baseT_Full)
			tmp |= ADVERTISE_10FULL;
		if (ADVERTISED_100baseT_Half)
			tmp |= ADVERTISE_100HALF;
		if (ADVERTISED_100baseT_Full)
			tmp |= ADVERTISE_100FULL;
		if (advert != tmp) {
			mii->mdio_write(dev, mii->phy_id, MII_ADVERTISE, tmp);
			mii->advertising = tmp;
		}
		
		/* turn on autonegotiation, and force a renegotiate */
		bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
		bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
		mii->mdio_write(dev, mii->phy_id, MII_BMCR, bmcr);

		mii->duplex_lock = 0;
	} else {
		u32 bmcr, tmp;

		/* turn off auto negotiation, set speed and duplexity */
		bmcr = mii->mdio_read(dev, mii->phy_id, MII_BMCR);
		tmp = bmcr & ~(BMCR_ANENABLE | BMCR_SPEED100 | BMCR_FULLDPLX);
		if (ecmd->speed == SPEED_100)
			tmp |= BMCR_SPEED100;
		if (ecmd->duplex == DUPLEX_FULL) {
			tmp |= BMCR_FULLDPLX;
			mii->full_duplex = 1;
		} else
			mii->full_duplex = 0;
		if (bmcr != tmp)
			mii->mdio_write(dev, mii->phy_id, MII_BMCR, tmp);

		mii->duplex_lock = 1;
	}
	return 0;
}

int mii_link_ok (struct mii_if_info *mii)
{
	/* first, a dummy read, needed to latch some MII phys */
	mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR);
	if (mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR) & BMSR_LSTATUS)
		return 1;
	return 0;
}

int mii_nway_restart (struct mii_if_info *mii)
{
	int bmcr;
	int r = -EINVAL;

	/* if autoneg is off, it's an error */
	bmcr = mii->mdio_read(mii->dev, mii->phy_id, MII_BMCR);

	if (bmcr & BMCR_ANENABLE) {
		bmcr |= BMCR_ANRESTART;
		mii->mdio_write(mii->dev, mii->phy_id, MII_BMCR, bmcr);
		r = 0;
	}

	return r;
}

void mii_check_link (struct mii_if_info *mii)
{
	if (mii_link_ok(mii))
		netif_carrier_on(mii->dev);
	else
		netif_carrier_off(mii->dev);
}

unsigned int mii_check_media (struct mii_if_info *mii, unsigned int ok_to_print)
{
	unsigned int old_carrier, new_carrier;
	int advertise, lpa, media, duplex;

	/* if forced media, go no further */
	if (mii->duplex_lock)
		return 0; /* duplex did not change */

	/* check current and old link status */
	old_carrier = netif_carrier_ok(mii->dev) ? 1 : 0;
	new_carrier = (unsigned int) mii_link_ok(mii);

	/* if carrier state did not change, this is a "bounce",
	 * just exit as everything is already set correctly
	 */
	if (old_carrier == new_carrier)
		return 0; /* duplex did not change */

	/* no carrier, nothing much to do */
	if (!new_carrier) {
		netif_carrier_off(mii->dev);
		if (ok_to_print)
			printk(KERN_INFO "%s: link down\n", mii->dev->name);
		return 0; /* duplex did not change */
	}

	/*
	 * we have carrier, see who's on the other end
	 */
	netif_carrier_on(mii->dev);

	/* get MII advertise and LPA values */
	if (mii->advertising)
		advertise = mii->advertising;
	else {
		advertise = mii->mdio_read(mii->dev, mii->phy_id, MII_ADVERTISE);
		mii->advertising = advertise;
	}
	lpa = mii->mdio_read(mii->dev, mii->phy_id, MII_LPA);

	/* figure out media and duplex from advertise and LPA values */
	media = mii_nway_result(lpa & advertise);
	duplex = (media & (ADVERTISE_100FULL | ADVERTISE_10FULL)) ? 1 : 0;

	if (ok_to_print)
		printk(KERN_INFO "%s: link up, %sMbps, %s-duplex, lpa 0x%04X\n",
		       mii->dev->name,
		       media & (ADVERTISE_100FULL | ADVERTISE_100HALF) ?
		       		"100" : "10",
		       duplex ? "full" : "half",
		       lpa);

	if (mii->full_duplex != duplex) {
		mii->full_duplex = duplex;
		return 1; /* duplex changed */
	}

	return 0; /* duplex did not change */
}

MODULE_AUTHOR ("Jeff Garzik <jgarzik@mandrakesoft.com>");
MODULE_DESCRIPTION ("MII hardware support library");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(mii_link_ok);
EXPORT_SYMBOL(mii_nway_restart);
EXPORT_SYMBOL(mii_ethtool_gset);
EXPORT_SYMBOL(mii_ethtool_sset);
EXPORT_SYMBOL(mii_check_link);
EXPORT_SYMBOL(mii_check_media);

