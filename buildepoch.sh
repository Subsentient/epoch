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

ShowHelp()
{
	Green="\033[32m"
	EndGreen="\033[0m"
	
	echo -e $Green"--nommu"$EndGreen":\n\tUse this to build Epoch for a CPU with no MMU."
	echo -e $Green"--configdir dir"$EndGreen":\n\tSets the directory Epoch will search for epoch.conf."
	echo -e "\tDefault is /etc/epoch."
	echo -e $Green"--logdir dir"$EndGreen":\n\tSets the directory Epoch will write system.log to."
	echo -e "\tDefault is /var/log."
	echo -e $Green"--membus-size"$EndGreen":\n\tTo set a size for the membus' chunk of shared memory."
	echo -e "\t2048 or greater is recommended. Default is the maximum page size."
	echo -e $Green"--binarypath path"$EndGreen":\n\tThe direct path to the Epoch binary. Default is /sbin/epoch."
	echo -e $Green"--env-home value"$EndGreen":\n\tDesired environment variable for \$HOME."
	echo -e "\tThis will be usable in Epoch start/stop commands."
	echo -e $Green"--env-user value"$EndGreen":\n\tDesired environment variable for \$USER."
	echo -e "\tThis will be usable in Epoch start/stop commands."
	echo -e $Green"--env-shell value"$EndGreen":\n\tDesired environment variable for \$SHELL."
	echo -e "\tThis will be usable in Epoch start/stop commands."
	echo -e $Green"--env-path value"$EndGreen":\n\tDesired environment variable for \$PATH"
	echo -e $Green"--outpath dir"$EndGreen":\n\tThe location that the compiled binary"
	echo -e "\tand symlinks will be placed upon completion."
	echo -e "\tSimilar to make install DESTDIR=\"\"."
	echo -e $Green"--disable-backtraces"$EndGreen":\n\tThis flag is necessary for building with"
	echo -e "\tuClibc and other libc implementations that don't provide execinfo.h."
	echo -e $Green"--disable-shell"$EndGreen":\n\tIf this flag is set, Epoch will be built"
	echo -e "\tto not launch objects with /bin/sh, and will instead try to use an"
	echo -e "\targument list. This may be useful on embedded systems,"
	echo -e "\tbut comes at a high price of removing support for any shell characters,"
	echo -e "\tsuch as '&' and '&&'."
	echo -e $Green"--shellpath"$EndGreen":\n\tThe shell that Epoch will use to assist in the launching of objects,"
	echo -e "\tprovided that --disable-shell is not specified."
	echo -e $Green"--shell-forks-with-dashc"$EndGreen":\n\tShells such as busybox create a new process"
	echo -e "\twhen we use 'sh -c', but most including bash, dash, ksh, csh, tcsh,"
	echo -e "\tand zsh do not. You are encouraged to make sure this is set for"
	echo -e "\tbusybox etc, because this option is used to assist tracking PIDs."
	echo -e $Green"--cflags value"$EndGreen":\n\tSets \$CFLAGS to the desired value."
	echo -e $Green"--ldflags value"$EndGreen":\n\tSets \$LDFLAGS to the desired value."
	echo -e $Green"--cc value"$EndGreen":\n\tSets \$CC to be the compiler for Epoch."
}

MEMBUS_SIZE_SET="0"
NEED_EMPTY_CFLAGS="0"
outdir="../built"

if [ "$CC" == "" ]; then
	CC="cc"
fi

if [ "$#" != "0" ]; then
	while [ -n "$1" ]
	do
		if [ "$1" == "--help" ]; then
			ShowHelp
			exit 0
		
		elif [ "$1" == "--nommu" ]; then
			CFLAGS=$CFLAGS" -DNOMMU"
	
		elif [ "$1" == "--configdir" ];then
			shift
			CFLAGS=$CFLAGS" -DCONFIGDIR=\"$1\""
	
		elif [ "$1" == "--membus-size" ]; then
			shift
			CFLAGS=$CFLAGS" -DMEMBUS_SIZE=$1"
			MEMBUS_SIZE_SET="1"
		
		elif [ "$1" == "--binarypath" ]; then
			shift
			CFLAGS=$CFLAGS" -DEPOCH_BINARY_PATH=\"$1\""
	
		elif [ "$1" == "--logdir" ]; then
			shift
			CFLAGS=$CFLAGS" -DLOGDIR=\"$1\""
		
		elif [ "$1" == "--env-home" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_HOME=\"$1\""
	
		elif [ "$1" == "--env-user" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_USER=\"$1\""
	
		elif [ "$1" == "--env-shell" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_SHELL=\"$1\""
	
		elif [ "$1" == "--env-path" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_PATH=\"$1\""
	
		elif [ "$1" == "--shell-forks-with-dashc" ]; then
			CFLAGS=$CFLAGS" -DSHELLDISSOLVES=false"
	
		elif [ "$1" == "--outpath" ]; then
			shift
			outdir="$1"
			
		elif [ "$1" == "--disable-shell" ]; then
			CFLAGS=$CFLAGS" -DNOSHELL"
			
		elif [ "$1" == "--disable-backtraces" ]; then
			CFLAGS=$CFLAGS" -DNO_EXECINFO"
			
		elif [ "$1" == "--shellpath" ]; then
			shift
			CFLAGS=$CFLAGS" -DSHELLPATH=\"$1\""
			
		elif [ "$1" == "--cflags" ]; then
			shift
			CFLAGS=$CFLAGS" $1"
			NEED_EMPTY_CFLAGS="1"
	
		elif [ "$1" == "--cc" ]; then
			shift
			CC="$1"
	
		elif [ "$1" == "--ldflags" ]; then
			shift
			LDFLAGS="$1"
		fi
		
		shift
	done
fi

if [ "$NEED_EMPTY_CFLAGS" == "0" ]; then
	CFLAGS=$CFLAGS" -std=gnu89 -pedantic -Wall -g -O0"
fi

if [ "$LDFLAGS" == "" ]; then
	LDFLAGS="-rdynamic"
fi

if [ "$MEMBUS_SIZE_SET" == "0" ]; then
	CFLAGS=$CFLAGS" -DMEMBUS_SIZE=$(getconf PAGESIZE)"
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
CMD "$CC $CFLAGS -c ../src/utilfuncs.c"

echo -e "\nBuilding main executable.\n"

mkdir -p $outdir/sbin/
mkdir -p $outdir/bin/

CMD "$CC $LDFLAGS $CFLAGS -pthread -o $outdir/sbin/epoch\
 actions.o config.o console.o main.o membus.o modes.o parse.o utilfuncs.o"

echo -e "\nCreating symlinks.\n"
cd $outdir/sbin/

CMD "ln -s -f ./epoch init"
CMD "ln -s -f ./epoch halt"
CMD "ln -s -f ./epoch poweroff"
CMD "ln -s -f ./epoch reboot"
CMD "ln -s -f ./epoch shutdown"
CMD "ln -s -f ./epoch killall5"

cd ../bin/

CMD "ln -s -f ../sbin/epoch wall"

echo -e "\nBuild complete.\n"
cd ..
