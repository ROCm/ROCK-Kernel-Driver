#!/bin/bash
# Copyright (C) Martin Schlemmer <azarah@nosferatu.za.org>
# Released under the terms of the GNU GPL
#
# Generate a newline separated list of entries from the file/directory pointed
# out by the environment variable: CONFIG_INITRAMFS_SOURCE
#
# If CONFIG_INITRAMFS_SOURCE is non-existing then generate a small dummy file.
#
# The output is suitable for gen_init_cpio as found in usr/Makefile.
#
# TODO:  Add support for symlinks, sockets and pipes when gen_init_cpio
#        supports them.

simple_initramfs() {
	cat <<-EOF
		# This is a very simple initramfs

		dir /dev 0755 0 0
		nod /dev/console 0600 0 0 c 5 1
		dir /root 0700 0 0
	EOF
}

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

print_mtime() {
	local argv1="$1"
	local my_mtime="0"

	if [ -e "${argv1}" ]; then
		my_mtime=$(find "${argv1}" -printf "%T@\n" | sort -r | head -n 1)
	fi
	
	echo "# Last modified: ${my_mtime}"
	echo
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

if [ -z "$1" ]; then
	simple_initramfs
elif [ -f "$1" ]; then
	print_mtime "$1"
	cat "$1"
elif [ -d "$1" ]; then
	srcdir=$(echo "$1" | sed -e 's://*:/:g')
	dirlist=$(find "${srcdir}" -printf "%p %m %U %G\n" 2>/dev/null)

	# If $dirlist is only one line, then the directory is empty
	if [  "$(echo "${dirlist}" | wc -l)" -gt 1 ]; then
		print_mtime "$1"
		
		echo "${dirlist}" | \
		while read x; do
			parse ${x}
		done
	else
		# Failsafe in case directory is empty
		simple_initramfs
	fi
else
	echo "  $0: Cannot open '$1' (CONFIG_INITRAMFS_SOURCE)" >&2
	exit 1
fi

exit 0
