/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**Handles bootup, shutdown, poweroff and reboot, etc, and some misc stuff.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include "epoch.h"

/**Functions.**/
/*This does what it sounds like. It exits us to go to a shell in event of catastrophe.*/
void EmergencyShell(void)
{
	fprintf(stderr, CONSOLE_COLOR_MAGENTA "\nPreparing to start emergency shell." CONSOLE_ENDCOLOR "\n---\n");
	
	fprintf(stderr, "\nSyncing disks...\n");
	fflush(NULL);
	sync(); /*First things first, sync disks.*/
	
	fprintf(stderr, "Shutting down Epoch...\n");
	fflush(NULL);
	ShutdownConfig(); /*Release all memory.*/
	ShutdownMemBus(true); /*Stop the membus.*/
	
	fprintf(stderr, "Launching the shell...\n");
	fflush(NULL);
	
	execlp("sh", "sh", NULL); /*Nuke our process image and replace with a shell. No point forking.*/
	
	/*We're supposed to be gone! Something went wrong!*/
	SpitError("Failed to start emergency shell! Sleeping forever.");
	fflush(NULL);
	
	while (1) sleep(1); /*Hang forever to prevent a kernel panic.*/
}

void LaunchBootup(void)
{ /*Handles what would happen if we were PID 1.*/
	printf("\n%s\n", VERSIONSTRING);
	
	if (!InitConfig())
	{ /*That is very very bad.*/
		EmergencyShell();
	}
	
	if (!InitMemBus(true))
	{
		SpitError("FAILURE IN MEMBUS! You won't be able to shut down the system with Epoch!");
		putc('\007', stderr); /*Beep.*/
	}
	
	PrintBootBanner();
	
	if (DisableCAD)
	{
		reboot(OSCTL_LINUX_DISABLE_CTRLALTDEL); /*Disable instant reboot on CTRL-ALT-DEL.*/
	}
	
	if (!RunAllObjects(true))
	{
		EmergencyShell();
	}
	
	EpochMemBusLoop(); /*Now enter into limbo of scanning the membus forever.*/
	
	/*We were never supposed to get this far.*/
	SpitError("EpochMemBusLoop() exited! That should never happen!");
	
	EmergencyShell();
}


void LaunchShutdown(unsigned long Signal)
{ /*Responsible for reboot, halt, power down, etc.*/
	
	printf("%s", CONSOLE_COLOR_RED);
	
	if (Signal == OSCTL_LINUX_HALT || Signal == OSCTL_LINUX_POWEROFF)
	{
		printf("%s", "Shutting down.\n");
	}
	else
	{
		printf("%s", "Rebooting.");
	}
	
	printf("%s", CONSOLE_ENDCOLOR);
	
	if (!RunAllObjects(false)) /*Run all the service stopping things.*/
	{
		SpitError("Failed to complete shutdown/reboot sequence!");
		EmergencyShell();
	}
	
	ShutdownConfig();
	ShutdownMemBus(true);
	
	printf("\n%s", CONSOLE_COLOR_CYAN);
	
	if (Signal == OSCTL_LINUX_HALT)
	{
		printf("%s\n", "Attempting to halt the system...");
	}
	else if (Signal == OSCTL_LINUX_POWEROFF)
	{
		printf("%s\n", "Attempting to power down the system...");
	}
	else
	{
		printf("%s\n", "Attempting to reboot the system...");
	}
	
	printf("%s", CONSOLE_ENDCOLOR);
	
	sync(); /*Force sync of disks in case somebody forgot.*/
	reboot(Signal); /*Send the signal.*/
	
	/*Again, not supposed to be here.*/
	
	SpitError("Failed to reboot/halt/power down!");
	EmergencyShell();
}
