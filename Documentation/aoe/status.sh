# collate and present sysfs information about AoE storage

set -e
format="%8s\t%12s\t%8s\t%8s\n"

printf "$format" device mac netif state

for d in `ls -d /sys/block/etherd* | grep -v p`; do
	dev=`echo "$d" | sed 's/.*!//'`
	printf "$format" \
		"$dev" \
		"`cat \"$d/mac\"`" \
		"`cat \"$d/netif\"`" \
		"`cat \"$d/state\"`"
done | sort
