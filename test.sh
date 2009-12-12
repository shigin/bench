#!/bin/sh

die() {
    echo $0: $@ 1>&2
    exit 1;
}

DIR=`mktemp -d _benchXXX`
if [ -z $DIR ] ; then
    die "can't create temp dir"
fi

echo "echo 1 >> $DIR/result" > $DIR/script

clean() {
    rm -f $DIR/result $DIR/script $DIR/bench-out $DIR/expect
    rmdir $DIR
}

nbench() {
    COUNT=$1
    EXPECT=$2
    [ -z $EXPECT ] && die "count is empty"
    rm -f $DIR/result
    if ./bench -n $COUNT sh $DIR/script > $DIR/bench-out ; then
        true
    else
        clean
        die "bench failed"
    fi

    if [ ! -f $DIR/result ] ; then
        die "there isn't result file in temp dir"
    fi

    if [ `wc -l < $DIR/result` != $EXPECT ] ; then
        LINES=`wc -l < $DIR/result`
        clean
        die there is $LINES instead of $EXPECT
    fi
}

# check if -n works fine
nbench 3 4
nbench 5 6

# check if -i works fine
mv $DIR/result $DIR/expect
echo "cat > $DIR/result" > $DIR/script
if ./bench -n 1 -i $DIR/expect sh $DIR/script > $DIR/bench-out ; then
    true
else
    clean
    die "bench fails"
fi

if [ ! -f $DIR/result ] ; then
    clean
    die "file doesn't duplicate"
fi

if diff $DIR/result $DIR/expect > /dev/null; then
    true
else
    clean
    die "duplicated files differ"
fi

# check if bench eat options
echo "echo x \$@ > $DIR/result" > $DIR/script
echo "x -n 10" > $DIR/expect

if ./bench sh $DIR/script -n 10 > $DIR/bench-out ; then
    true
else
    clean
    die "bench failed"
fi

if diff $DIR/expect $DIR/result > /dev/null ; then
    true
else
    EXPECT="`cat $DIR/expect`"
    RESULT="`cat $DIR/result`"
    clean
    die "wrong output: $RESULT, $EXPECT expected"
fi

# check if fail on bad error code
if ./bench false 2> $DIR/bench-out ; then
    clean
    die "run on false error"
fi

if ./bench -b true > $DIR/bench-out ; then
    true
else
    clean
    die "can't produce batch output"
fi

. $DIR/bench-out

if [ ${bench_failed} != 0 ] ; then
    clean
    die "there are failed attempts"
fi

echo "R=\`ls $DIR/result 2> /dev/null\`; touch $DIR/result ; [ -z \"\$R\" ]" > $DIR/script
rm -f $DIR/result

if ./bench -b -n 1 sh $DIR/script > $DIR/bench-out ; then
    true
else
    clean
    die "bench failed"
fi

. $DIR/bench-out

if [ $bench_failed != 1 ] ; then
    #clean
    die "wrong failed results"
fi

clean
