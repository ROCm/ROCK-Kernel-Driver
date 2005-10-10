#
# A simple script used to compile relocation information needed
# to move critical low-level code to continuous physical 
# memory
#
# (C) 1999, 2001 Samuel Rydh <samuel@ibrium.se>
#

#use POSIX;

# variables set by make:
# $NM, $OBJDUMP, $GCC, $ARCH

# Note: Under Darwin, assembly symbols have a leading '_'
$SYM_PREFIX = '';
if( "$ARCH" eq "darwin" ) {
    $SYM_PREFIX = '_';
}

if( $#ARGV < 1 ) {
    print "Usage: symextract objectfile source\n";
    exit 1;
}

#print `cat warning.txt`;
$output = `$OBJDUMP -r $ARGV[0]` or die "Script failed";
$bad = 0;

######################################################################
# extract action symbol declarations from the specified source files
######################################################################

# find the symbols to export from the files specified on the cmdline 
$basecode = `$GCC -I./include -I../sinclude -E -DACTION_SYM_GREPPING $ARGV[1] 2> /dev/null` 
    or die "preprocessing failed";
@ASYMS = $basecode =~ /ACTION_SYM_START, *(\w*) *, *(\w*) *,ACTION_SYM_END.*/g;

# find exported global symbols
@GSYMS = $basecode =~ /GLOBAL_SYM_START, *(\w*) *,GLOBAL_SYM_END.*/g;

# make sure no action symbols are defined twice (a programming error)
for( $j=0 ; $j <= $#ASYMS ; $j+=2 ) {
    for( $i=$j+2 ; $i <= $#ASYMS ; $i+=2 ) {
	if( $ASYMS[$j] eq $ASYMS[$i] ) {
	    print STDERR "*** Error: Action symbol '".
		   $ASYMS[$j]."' declared twice! ***\n";
	    $bad++;
	}
    }
}

# make sure no symbols are exported which are not declared
@esyms = `$NM $ARGV[0]` =~ / T (.*)/g;

for( $j=0 ; $j <= $#esyms ; $j++ ) {
    $match = 0;
    for( $i=0; $i <= $#GSYMS; $i++ ) {
	if( "$SYM_PREFIX$GSYMS[$i]" eq $esyms[$j] ) {
	    $match = 1;
	}
    }
    if( !$match ) {
	$bad++; 
	print STDERR "*** Error: Symbol `".$esyms[$j].
	    "` is not explicitely exported ***\n"; 
    }
}

######################################################################
# eliminate external symbols (absolutely addressed) from the
# reloctable list (and detect relative addressing of external
# symbols - we can't use such since the 24bit limit will cause problem)
######################################################################

@ext_syms = (`$NM -g -u $ARGV[0]` or die "nm failed") =~ /(\w*)\n/g;

foreach( @ext_syms ) {
    # Don't remove external Action_* or Symbol_* symbols.
    if($_ =~ /Action_\w*|Symbol_\w*/ ) {
	next;
    }

    if( $output =~ /.*PPC_REL.*$_[+\n\-0-9 ].*/ ) {
	print STDERR "*** Error: External symbol '".$_.
	    "' is referenced relatively ***\n";
	$bad++;
    } 
    $output =~ s/^\w*[ \t]\w*PPC_ADDR\w*[ \t]*$_[+\- \t]*\w*[ \t]*$//mg;
    $output =~ s/^\w*[ \t]\w*PPC_REL\w*[ \t]*$_[+\- \t]*\w*[ \t]*$//mg;
}
# At this point only local symbols are in $output.


######################################################################
# Print the relocation table
######################################################################

# Find all local labels and the corresponding offset (in the .text segment)
@lsyms = (`$NM -n $ARGV[0]` or die "nm failed") =~ /(.*) [tT] (.*)/g;

# Put the local symbols in an arrary (ADDR_MODE, sym_name, offset) 
@E = $output =~ /([0-9,a-f,A-FxX]*) *(\w*_PPC_\w*) *(\w*)([xX+\-0-9,a-f,A-F]*)\n/g;

print "\nstatic reloc_table_t reloc_table[] = {\n";
for($i=0; $i<=$#E; $i+=4 ) {
    $match = 0;

    # remove leading '+' in the offset (hex doesn't like it)
    #$orgoffs = $E[$i+3];
    $E[$i+3] =~ s/\+//;

    # Action symbols have top priority
    $addr = hex($E[$i]);
    $name = $E[$i+2];
    $type = $E[$i+1];
    $action = 0;
    $offs1 = 0;
    $offs2 = hex( $E[$i+3] );
    $skip = 0;
    for( $k=0; $k <= $#ASYMS; $k+=2 ){
	if( $ASYMS[$k] eq $name ) {
	    $match = 1;
	    $action = $ASYMS[$k+1];
	}
    }

    # Find "normal" local symbols
    for( $k=1; !$match && $k<=$#lsyms; $k+=2 ) {
	if( $lsyms[$k] eq $name ) {
	    $match = 1;
	    $offs1 = hex( $lsyms[$k-1]);
	    # local relative symbols can be skipped - these will work properly anyway
	    if( $type =~ /PPC_REL/ ) {
		$skip = 1;
	    }
	}
    } 
    # Change R_PPC_ADDRxx to MOL_R_PPC_ADDRxx (kernel header name clashes)
    $type =~ s/R_PPC/MOL_R_PPC/;

    if( !$match ) {
	# This is typically a unhandled local symbol in the data section...   
	$bad++;
	print STDERR "*** Error: Could not obtain the address of the symbol ".$name.
	    " in the .text section\n";
    }

    # We have a good entry, print it!
    if( !$skip && $match ) {
	printf "  { 0x%04x, ", $addr;
	print $action.", ";
	printf "%-16s", sprintf "%s,", $type;
	printf " 0x%04x ", $offs1;
	if( $offs2 != 0 ) {
	    printf "+ 0x%-8x ", $offs2;
	} else {
	    print "             ";
	}
	print "}, \t /* ".$name." */\n"
    }
}
print "  { 0, -1, 0, 0 }   /* End marker */ \n};\n";

$bad && print STDERR "*** There were errors (perhaps in ".$ARGV[0].") ***\n";
exit $bad
