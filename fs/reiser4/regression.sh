#! /bin/sh

PROGRAM=./a.out

printf "REISER4_TRACE_FLAGS:     %x\n" $REISER4_TRACE_FLAGS
printf "REISER4_BLOCK_SIZE:      %i\n" ${REISER4_BLOCK_SIZE:-256}
echo "REISER4_MOUNT:         " $REISER4_MOUNT
echo "REISER4_MOUNT_OPTS:    " $REISER4_MOUNT_OPTS

ROUNDS=${1:-1}

function run()
{
#   do_mkfs
	echo -n $* "..."
	( /usr/bin/time -f " T: %e/%S/%U F: %F/%R" $* >/dev/null ) || exit 1
#	shift
#	shift
	RNAME=`echo $* | sed 's/[ \/]/./g'`.$r
#	echo $RNAME
	mv gmon.out gmon.out.$RNAME 2>/dev/null
}

function do_mkfs()
{
	echo "mkfs $REISER4_MOUNT tail" | ${PROGRAM} sh >/dev/null
}

export REISER4_PRINT_STATS=1
export REISER4_CRASH_MODE=suspend
export REISER4_TRAP=1
#export REISER4_SWAPD=1

rm -f gmon.out.*

#ORDER='000'
ORDER=${2:-''}

do_mkfs || exit 1

if [ -z $REISER4_MOUNT ]
then
	echo 'Set $REISER4_MOUNT'
	exit 3
fi

for r in `seq 1 $ROUNDS`
do
echo Round $r

run ${PROGRAM} nikita ibk 30${ORDER}
#	run ${PROGRAM} jmacd build 3 1000 1000
run ${PROGRAM} nikita dir 1 100${ORDER} 0
run ${PROGRAM} nikita dir 1 100${ORDER} 1
run ${PROGRAM} nikita dir 4 7${ORDER} 0
run ${PROGRAM} nikita dir 4 7${ORDER} 1
run ${PROGRAM} nikita mongo 3 20${ORDER} 0
run ${PROGRAM} nikita mongo 3 20${ORDER} 1
run ${PROGRAM} nikita rm 6 10${ORDER} 0
run ${PROGRAM} nikita rm 6 10${ORDER} 1
run ${PROGRAM} nikita unlink 15${ORDER}
#run ${PROGRAM} nikita queue 30 10${ORDER} 10000
run ${PROGRAM} nikita mongo 30 1${ORDER} 0
run ${PROGRAM} nikita mongo 30 1${ORDER} 1
run ${PROGRAM} nikita bobber 100${ORDER} 300
#run ulevel/cp-r plugin
#( find /tmp | ${PROGRAM} vs copydir )

echo Round $r done.
done
