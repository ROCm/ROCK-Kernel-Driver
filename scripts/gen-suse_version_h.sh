#!/bin/bash

if test -e include/config/auto.conf; then
        . include/config/auto.conf
else
        echo "Error: auto.conf not generated - run 'make prepare' to create it" >&2
	exit 1
fi

VERSION="${CONFIG_SUSE_VERSION}"
PATCHLEVEL="${CONFIG_SUSE_PATCHLEVEL}"
AUXRELEASE="${CONFIG_SUSE_AUXRELEASE}"

if [ -z "$VERSION" -o -z "$PATCHLEVEL" -o -z "$AUXRELEASE" ]; then
	# This would be a bug in the Kconfig
	cat <<- END >&2
	ERROR: Missing VERSION, PATCHLEVEL, or AUXRELEASE."
	Please check init/Kconfig.suse for correctness.
	END
	exit 1
fi

if [ "$VERSION" = 255 -o "$PATCHLEVEL" = 255 ]; then
	cat <<- END >&2

	ERROR: This release needs to be properly configured.
	Please add real values for SUSE_VERSION and SUSE_PATCHLEVEL.

	END
	exit 1
fi


case "$CONFIG_SUSE_PRODUCT_CODE" in
	1)
		if [ "${PATCHLEVEL}" = "0" ]; then
			SP=""
		else
			SP="${PATCHLEVEL}"
		fi
		SUSE_PRODUCT_NAME="SUSE Linux Enterprise ${VERSION}${SP:+ SP}${SP}"
		SUSE_PRODUCT_SHORTNAME="SLE${VERSION}${SP:+-SP}${SP}"
		SUSE_PRODUCT_FAMILY="SLE"
		;;
	2)
		SUSE_PRODUCT_NAME="openSUSE Leap ${VERSION}.${PATCHLEVEL}"
		SUSE_PRODUCT_SHORTNAME="$SUSE_PRODUCT_NAME"
		SUSE_PRODUCT_FAMILY="Leap"
		;;
	3)
		SUSE_PRODUCT_NAME="openSUSE Tumbleweed"
		SUSE_PRODUCT_SHORTNAME="$SUSE_PRODUCT_NAME"
		SUSE_PRODUCT_FAMILY="Tumbleweed"
		;;
	*)
		echo "Unknown SUSE_PRODUCT_CODE=${CONFIG_SUSE_PRODUCT_CODE}" >&2
		exit 1
		;;
esac

SUSE_PRODUCT_CODE=$(( (${CONFIG_SUSE_PRODUCT_CODE} << 24) + \
		      (${VERSION} << 16) + (${PATCHLEVEL} << 8) + \
		       ${AUXRELEASE} ))

cat <<END
#ifndef _SUSE_VERSION_H
#define _SUSE_VERSION_H

#define SUSE_PRODUCT_FAMILY     "${SUSE_PRODUCT_FAMILY}"
#define SUSE_PRODUCT_NAME       "${SUSE_PRODUCT_NAME}"
#define SUSE_PRODUCT_SHORTNAME  "${SUSE_PRODUCT_SHORTNAME}"
#define SUSE_VERSION            ${VERSION}
#define SUSE_PATCHLEVEL         ${PATCHLEVEL}
#define SUSE_AUXRELEASE		${AUXRELEASE}
#define SUSE_PRODUCT_CODE       ${SUSE_PRODUCT_CODE}
#define SUSE_PRODUCT(product, version, patchlevel, auxrelease)		\\
	(((product) << 24) + ((version) << 16) +			\\
	 ((patchlevel) << 8) + (auxrelease))

#endif /* _SUSE_VERSION_H */
END
