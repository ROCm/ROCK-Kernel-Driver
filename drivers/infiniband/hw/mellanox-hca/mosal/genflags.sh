#!/bin/sh

LL_VERSION=`echo $1 | sed -e 's/\(2\.4\...\?\-..\?\).*/\1/'`

RH_9_0_LIST="2.4.20-8@"
RH_9_0_FLAGS="-DRH_9_0"


RH_7_3_LIST="2.4.18-14@"
RH_7_3_FLAGS="-DMT_REDHAT_73"

echo $RH_9_0_LIST | grep "$LL_VERSION@" >> /dev/null
if [ $? == 0 ]; then
FLAGS=$RH_9_0_FLAGS
fi

echo $RH_7_3_LIST | grep "$LL_VERSION@" >> /dev/null
if [ $? == 0 ]; then
FLAGS=$RH_7_3_FLAGS
fi

echo $FLAGS
