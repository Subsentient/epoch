#!/bin/sh
# Script to built the Epoch Init System.

CMD()
{
	printf "%s " $1
	printf "\n"
	$1
	if [ "$?" != "0" ]; then
	{
	   printf "Error building Epoch.\n"
		exit 1;
	}
	fi
}

ShowHelp()
{
	Green="\033[32m"
	EndGreen="\033[0m"
	
	printf $Green"--nommu"$EndGreen":\n\tUse this to build Epoch for a CPU with no MMU.\n"
	printf $Green"--configdir dir"$EndGreen":\n\tSets the directory Epoch will search for epoch.conf.\n"
	printf "\tDefault is /etc/epoch.\n"
	printf $Green"--logdir dir"$EndGreen":\n\tSets the directory Epoch will write system.log to.\n"
	printf "\tDefault is /var/log.\n"
	printf $Green"--binarypath path"$EndGreen":\n\tThe direct path to the Epoch binary. Default is /sbin/epoch.\n"
	printf $Green"--env-home value"$EndGreen":\n\tDesired environment variable for \$HOME.\n"
	printf "\tThis will be usable in Epoch start/stop commands.\n"
	printf $Green"--env-user value"$EndGreen":\n\tDesired environment variable for \$USER.\n"
	printf "\tThis will be usable in Epoch start/stop commands.\n"
	printf $Green"--env-shell value"$EndGreen":\n\tDesired environment variable for \$SHELL.\n"
	printf "\tThis will be usable in Epoch start/stop commands.\n"
	printf $Green"--env-path value"$EndGreen":\n\tDesired environment variable for \$PATH.\n"
	printf $Green"--outpath dir"$EndGreen":\n\tThe location that the compiled binary.\n"
	printf "\tand symlinks will be placed upon completion.\n"
	printf "\tSimilar to make install DESTDIR=\"\".\n"
	printf $Green"--disable-backtraces"$EndGreen":\n\tThis flag is necessary for building with\n"
	printf "\tuClibc and other libc implementations that don't provide execinfo.h.\n"
	printf $Green"--disable-shell"$EndGreen":\n\tIf this flag is set, Epoch will be built\n"
	printf "\tto not launch objects with /bin/sh, and will instead try to use an\n"
	printf "\targument list. This may be useful on embedded systems,\n"
	printf "\tbut comes at a high price of removing support for any shell characters,\n"
	printf "\tsuch as '&' and '&&'.\n"
	printf $Green"--shellpath"$EndGreen":\n\tThe shell that Epoch will use to assist in the launching of objects,\n"
	printf "\tprovided that --disable-shell is not specified.\n"
	printf $Green"--shell-forks-with-dashc"$EndGreen":\n\tShells such as busybox create a new process\n"
	printf "\twhen we use 'sh -c', but most including bash, dash, ksh, csh, tcsh,\n"
	printf "\tand zsh do not. You are encouraged to make sure this is set for\n"
	printf "\tbusybox etc, because this option is used to assist tracking PIDs.\n"
	printf $Green"--nokargsfile"$EndGreen":\n\tThe file that Epoch will check for to see if it should ignore kernel arg options.\n"
	printf $Green"--cflags value"$EndGreen":\n\tSets \$CFLAGS to the desired value.\n"
	printf $Green"--ldflags value"$EndGreen":\n\tSets \$LDFLAGS to the desired value.\n"
	printf $Green"--cc value"$EndGreen":\n\tSets \$CC to be the compiler for Epoch.\n"
}

NEED_EMPTY_CFLAGS="0"
outdir="../built"

if [ "$CC" = "" ]; then
	CC="cc"
fi

if [ "$#" != "0" ]; then
	while [ -n "$1" ]
	do
		if [ "$1" = "--help" ]; then
			ShowHelp
			exit 0
		
		elif [ "$1" = "--nommu" ]; then
			CFLAGS=$CFLAGS" -DNOMMU"
	
		elif [ "$1" = "--configdir" ];then
			shift
			CFLAGS=$CFLAGS" -DCONFIGDIR=\"$1\""
	
		elif [ "$1" = "--binarypath" ]; then
			shift
			CFLAGS=$CFLAGS" -DEPOCH_BINARY_PATH=\"$1\""
	
		elif [ "$1" = "--logdir" ]; then
			shift
			CFLAGS=$CFLAGS" -DLOGDIR=\"$1\""
		
		elif [ "$1" = "--env-home" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_HOME=\"$1\""
	
		elif [ "$1" = "--env-user" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_USER=\"$1\""
	
		elif [ "$1" = "--env-shell" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_SHELL=\"$1\""
		elif [ "$1" = "--nokargsfile" ]; then
			shift
			CFLAGS=$CFLAGS" -DNOKARGSFILE=\"$1\""
		elif [ "$1" = "--env-path" ]; then
			shift
			CFLAGS=$CFLAGS" -DENVVAR_PATH=\"$1\""
	
		elif [ "$1" = "--shell-forks-with-dashc" ]; then
			CFLAGS=$CFLAGS" -DSHELLDISSOLVES=false"
	
		elif [ "$1" = "--outpath" ]; then
			shift
			outdir="$1"
			
		elif [ "$1" = "--disable-shell" ]; then
			CFLAGS=$CFLAGS" -DNOSHELL"
			
		elif [ "$1" = "--disable-backtraces" ]; then
			CFLAGS=$CFLAGS" -DNO_EXECINFO"
			
		elif [ "$1" = "--shellpath" ]; then
			shift
			CFLAGS=$CFLAGS" -DSHELLPATH=\"$1\""
			
		elif [ "$1" = "--cflags" ]; then
			shift
			CFLAGS=$CFLAGS" $1"
			NEED_EMPTY_CFLAGS="1"
	
		elif [ "$1" = "--cc" ]; then
			shift
			CC="$1"
	
		elif [ "$1" = "--ldflags" ]; then
			shift
			LDFLAGS="$1"
		fi
		
		shift
	done
fi

if [ "$NEED_EMPTY_CFLAGS" = "0" ]; then
	CFLAGS=$CFLAGS" -std=gnu99 -pedantic -Wall -g -O0 -fstack-protector"
fi

if [ "$LDFLAGS" = "" ]; then
	LDFLAGS="-rdynamic"
fi

printf "\nBuilding object files.\n\n"
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

printf "\nBuilding main executable.\n\n"

mkdir -p $outdir/sbin/
mkdir -p $outdir/bin/

CMD "$CC $CFLAGS -o $outdir/sbin/epoch\
 actions.o config.o console.o main.o membus.o modes.o parse.o utilfuncs.o $LDFLAGS"

printf "\nCreating symlinks.\n"
cd $outdir/sbin/

CMD "ln -s -f ./epoch init"
CMD "ln -s -f ./epoch halt"
CMD "ln -s -f ./epoch poweroff"
CMD "ln -s -f ./epoch reboot"
CMD "ln -s -f ./epoch shutdown"
CMD "ln -s -f ./epoch killall5"

cd ../bin/

CMD "ln -s -f ../sbin/epoch wall"

printf "\nBuild complete.\n\n"
cd ..
