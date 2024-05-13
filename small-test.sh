#!/bin/sh



if [ -z $1 ]; then
    FILE="./test.img"
else
    FILE=$1
fi

set -o  nounset

PROGRAM=$2


# 32 bytes
strend="0123456789ABCDEF________________-__End-of-512-byte-Block"
str () {
    echo "0123456789ABCDEF___<-${1}->________0123456789ABCDEF________________"
}
x=""

# create a "dummy" 512-byte block
for i in $(seq 8); do 

    if [ $i -lt 8 ]; then
        x="${x}"$(str $i)
        x=$x$'\n'

    else
        x="${x}$strend"
    fi
done

if [ -e $FILE ]; then
    rm -i $FILE
fi

echo running test

for i in $(seq 1024); do 
    echo "$x" >> $FILE
done


$PROGRAM --receive --port 5000 --address 127.0.0.1 --path ./$FILE.dst >/dev/null &
$PROGRAM --send --total-retransmits 5 --range-size 128 --port 5000 --address 127.0.0.1 --path ./$FILE >/dev/null

if [ $(md5 -q test.img) == $(md5 -q test.img.dst) ]; then 
    echo Test passed
else
    echo "Test failed ($FILE and $FILE.dst differ)"
    exit 1
fi
