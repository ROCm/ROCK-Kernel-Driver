#!/usr/bin/perl

#	Check the stack usage of functions
#
#	Copyright Joern Engel <joern@wh.fh-wedel.de>
#	Inspired by Linus Torvalds
#	Original idea maybe from Keith Owens
#	s390 port and big speedup by Arnd Bergmann <arnd@bergmann-dalldorf.de>
#	Mips port by Juan Quintela <quintela@mandrakesoft.com>
#	IA64 port via Andreas Dilger
#	Arm port by Holger Schurig
#	Random bits by Matt Mackall <mpm@selenic.com>
#
#	Usage:
#	objdump -d vmlinux | stackcheck_ppc.pl [arch]
#
#	TODO :	Port to all architectures (one regex per arch)

# check for arch
#
# $re is used for two matches:
# $& (whole re) matches the complete objdump line with the stack growth
# $1 (first bracket) matches the size of the stack growth
#
# use anything else and feel the pain ;)
{
	my $arch = shift;
	if ($arch eq "") {
		$arch = `uname -m`;
	}

	$x	= "[0-9a-f]";	# hex character
	$xs	= "[0-9a-f ]";	# hex character or space
	if ($arch =~ /^arm$/) {
		#c0008ffc:	e24dd064	sub	sp, sp, #100	; 0x64
		$re = qr/.*sub.*sp, sp, #(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch =~ /^i[3456]86$/) {
		#c0105234:       81 ec ac 05 00 00       sub    $0x5ac,%esp
		$re = qr/^.*[as][du][db]    \$(0x$x{1,8}),\%esp$/o;
	} elsif ($arch =~ /^ia64$/) {
		#e0000000044011fc:       01 0f fc 8c     adds r12=-384,r12
		$re = qr/.*adds.*r12=-(([0-9]{2}|[3-9])[0-9]{2}),r12/o;
	} elsif ($arch =~ /^mips64$/) {
		#8800402c:       67bdfff0        daddiu  sp,sp,-16
		$re = qr/.*daddiu.*sp,sp,-(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch =~ /^mips$/) {
		#88003254:       27bdffe0        addiu   sp,sp,-32
		$re = qr/.*addiu.*sp,sp,-(([0-9]{2}|[3-9])[0-9]{2})/o;
	} elsif ($arch =~ /^ppc$/) {
		#c00029f4:       94 21 ff 30     stwu    r1,-208(r1)
		$re = qr/.*stwu.*r1,-($x{1,8})\(r1\)/o;
	} elsif ($arch =~ /^ppc64$/) {
		#XXX
		$re = qr/.*stdu.*r1,-($x{1,8})\(r1\)/o;
	} elsif ($arch =~ /^s390x?$/) {
		#   11160:       a7 fb ff 60             aghi   %r15,-160
		$re = qr/.*ag?hi.*\%r15,-(([0-9]{2}|[3-9])[0-9]{2})/o;
	} else {
		print("wrong or unknown architecture\n");
		exit
	}
}

sub bysize($) {
	($asize = $a) =~ s/.*	+(.*)$/$1/;
	($bsize = $b) =~ s/.*	+(.*)$/$1/;
	$bsize <=> $asize
}

#
# main()
#
$funcre = qr/^$x* \<(.*)\>:$/;
while ($line = <STDIN>) {
	if ($line =~ m/$funcre/) {
		$func = $1;
	}
	if ($line =~ m/$re/) {
		my $size = $1;
		$size = hex($size) if ($size =~ /^0x/);

		if ($size > 0x80000000) {
			$size = - $size;
			$size += 0x80000000;
			$size += 0x80000000;
		}

		$line =~ m/^($xs*).*/;
		my $addr = $1;
		$addr =~ s/ /0/g;
		$addr = "0x$addr";

		my $intro = "$addr $func:";
		my $padlen = 56 - length($intro);
		while ($padlen > 0) {
			$intro .= '	';
			$padlen -= 8;
		}
		next if ($size < 100);
		$stack[@stack] = "$intro$size\n";
	}
}

@sortedstack = sort bysize @stack;

foreach $i (@sortedstack) {
	print("$i");
}
