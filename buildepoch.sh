#!/bin/sh
# Script to built the Epoch Init System.

CMD()
{
	echo $1
	$1
	if [ "$?" != "0" ]; then
	{
		echo "Error building Epoch."
		exit 1;
	}
	fi
}

if [ "$CC" == "" ]; then
	CC="cc"
fi

if [ "$CFLAGS" == "" ]; then
	CFLAGS="-std=gnu89 -pedantic -Wall -g -O0 -rdynamic -fstack-protector"
fi

echo -e "\nBuilding object files.\n"
rm -rf objects built

mkdir objects
cd objects

CMD "$CC $CFLAGS -c ../src/actions.c"
CMD "$CC $CFLAGS -c ../src/config.c"
CMD "$CC $CFLAGS -c ../src/console.c"
CMD "$CC $CFLAGS -c ../src/main.c"
CMD "$CC $CFLAGS -c ../src/membus.c"
CMD "$CC $CFLAGS -c ../src/modes.c"
CMD "$CC $CFLAGS -c ../src/parse.c"

echo -e "\nBuilding main executable.\n"

mkdir -p ../built/sbin/

CMD "$CC $LDFLAGS $CFLAGS -o ../built/sbin/epoch actions.o config.o console.o main.o membus.o modes.o parse.o"

echo -e "\nCreating symlinks.\n"
cd ../built/sbin/

CMD "ln -s -f ./epoch init"
CMD "ln -s -f ./epoch halt"
CMD "ln -s -f ./epoch poweroff"
CMD "ln -s -f ./epoch reboot"
CMD "ln -s -f ./epoch shutdown"
CMD "ln -s -f ./epoch killall5"
CMD "ln -s -f ./epoch wall"

echo -e "\nBuild complete.\n"
cd ..
