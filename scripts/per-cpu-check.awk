/ __per_cpu_start$$/ {
	IN_PER_CPU=1
}

/ __per_cpu_end$$/ {
	IN_PER_CPU=0
}

/__per_cpu$$/ && ! ( / __ksymtab_/ || / __kstrtab_/ || / __kcrctab_/ || / __crc_/ ) {
	if (!IN_PER_CPU) {
		print $$3 " not in per-cpu section" > "/dev/stderr";
		FOUND=1;
	}
}

END {
	exit FOUND;
}

