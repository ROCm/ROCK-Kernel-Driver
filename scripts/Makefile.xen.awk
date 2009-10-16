BEGIN {
	is_rule = 0
}

/^[[:space:]]*#/ {
	next
}

/^[[:space:]]*$/ {
	if (is_rule)
		print("")
	is_rule = 0
	next
}

/:[[:space:]]*\$\(src\)\/%\.[cS][[:space:]]/ {
	line = gensub(/%.([cS])/, "%-xen.\\1", "g", $0)
	line = gensub(/(single-used-m)/, "xen-\\1", "g", line)
	print line
	is_rule = 1
	next
}

/^[^\t]$/ {
	if (is_rule)
		print("")
	is_rule = 0
	next
}

is_rule {
	print $0
	next
}
