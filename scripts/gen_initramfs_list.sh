#!/bin/bash
# Copyright (C) Martin Schlemmer <azarah@nosferatu.za.org>
# Released under the terms of the GNU GPL
#
# A script to generate newline separated entries (to stdout) from a directory's
# contents suitable for use as a cpio_list for gen_init_cpio.
#
# Arguements: $1 -- the source directory
#
# TODO:  Add support for symlinks, sockets and pipes when gen_init_cpio
#        supports them.

usage() {
	echo "Usage: $0 initramfs-source-dir"
	exit 1
}

srcdir=$(echo "$1" | sed -e 's://*:/:g')

if [ "$#" -gt 1 -o ! -d "${srcdir}" ]; then
	usage
fi

filetype() {
	local argv1="$1"

	if [ -f "${argv1}" ]; then
		echo "file"
	elif [ -d "${argv1}" ]; then
		echo "dir"
	elif [ -b "${argv1}" -o -c "${argv1}" ]; then
		echo "nod"
	else
		echo "invalid"
	fi
	return 0
}

parse() {
	local location="$1"
	local name="${location/${srcdir}//}"
	local mode="$2"
	local uid="$3"
	local gid="$4"
	local ftype=$(filetype "${location}")
	local str="${mode} ${uid} ${gid}"

	[ "${ftype}" == "invalid" ] && return 0
	[ "${location}" == "${srcdir}" ] && return 0

	case "${ftype}" in
		"file")
			str="${ftype} ${name} ${location} ${str}"
			;;
		"nod")
			local dev_type=
			local maj=$(LC_ALL=C ls -l "${location}" | \
					gawk '{sub(/,/, "", $5); print $5}')
			local min=$(LC_ALL=C ls -l "${location}" | \
					gawk '{print $6}')

			if [ -b "${location}" ]; then
				dev_type="b"
			else
				dev_type="c"
			fi
			str="${ftype} ${name} ${str} ${dev_type} ${maj} ${min}"
			;;
		*)
			str="${ftype} ${name} ${str}"
			;;
	esac

	echo "${str}"

	return 0
}

find "${srcdir}" -printf "%p %m %U %G\n" | \
while read x; do
	parse ${x}
done

exit 0
