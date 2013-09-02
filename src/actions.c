/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**Handles bootup, shutdown, poweroff and reboot, etc, and some misc stuff.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include "epoch.h"

/*Globals.*/
struct _HaltParams HaltParams = { -1, 0, 0, 0, 0, 0 };

/*Functions.*/


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
	Bool DoPrimaryScan = true;
	unsigned long CurHr, CurMin, CurSec, CurMon, CurDay, CurYear;
	struct tm *TimePtr;
	time_t TimeCore;
	
	printf("\n%s\n", VERSIONSTRING);
	
	if (!InitMemBus(true))
	{
		SpitError("FAILURE IN MEMBUS! You won't be able to shut down the system with Epoch!");
		putc('\007', stderr); /*Beep.*/
	}
	
	if (!InitConfig())
	{ /*That is very very bad.*/
		EmergencyShell();
	}
	
	PrintBootBanner();
	
	if (Hostname[0] != '\0')
	{ /*The system hostname.*/
		sethostname(Hostname, strlen(Hostname));
	}
	if (DisableCAD)
	{
		reboot(OSCTL_LINUX_DISABLE_CTRLALTDEL); /*Disable instant reboot on CTRL-ALT-DEL.*/
	}
	
	if (!RunAllObjects(true))
	{
		EmergencyShell();
	}
	
	while (DoPrimaryScan)
	{
		usleep(250000); /*Quarter of a second.*/
		
		ParseMemBus(); /*Check membus for new data.*/
		
		time(&TimeCore);
		TimePtr = localtime(&TimeCore);
		
		CurHr = TimePtr->tm_hour;
		CurMin = TimePtr->tm_min;
		CurSec = TimePtr->tm_sec;
		CurMon = TimePtr->tm_mon + 1;
		CurDay = TimePtr->tm_mday;
		CurYear = TimePtr->tm_year + 1900;
		
		if (CurHr == HaltParams.TargetHour && CurMin == HaltParams.TargetMin &&
			CurSec == HaltParams.TargetSec && CurMon == HaltParams.TargetMonth &&
			CurDay == HaltParams.TargetDay && CurYear == HaltParams.TargetYear &&
			HaltParams.HaltMode != -1)
		{
			if (HaltParams.HaltMode == OSCTL_LINUX_HALT)
			{
				EmulWall("System is going down for halt NOW!", false);
			}
			else if (HaltParams.HaltMode == OSCTL_LINUX_POWEROFF)
			{
				EmulWall("System is going down for poweroff NOW!", false);
			}
			else
			{
				HaltParams.HaltMode = OSCTL_LINUX_REBOOT;
				EmulWall("System is going down for reboot NOW!", false);
			}
			
			LaunchShutdown(HaltParams.HaltMode);
		}
		
		/*Lots of brilliant code here, but I typed it in invisible pixels.*/
		
	}
	
	/*We were never supposed to get this far.*/
	SpitError("Primary loop exited! That should never happen!");
	
	EmergencyShell();
}


void LaunchShutdown(signed long Signal)
{ /*Responsible for reboot, halt, power down, etc.*/
	const char *AttemptMsg = NULL;
	
	if (!ShutdownMemBus(true))
	{ /*Shutdown membus first, so no other signals will reach us.*/
		SpitWarning("Failed to shut down membus interface.");
	}
	
	
	if (Signal == OSCTL_LINUX_HALT || Signal == OSCTL_LINUX_POWEROFF)
	{
		printf("%s", CONSOLE_COLOR_RED "Shutting down." CONSOLE_ENDCOLOR "\n");
	}
	else
	{
		printf("%s", CONSOLE_COLOR_RED "Rebooting.\n" CONSOLE_ENDCOLOR "\n");
	}
	
	if (!RunAllObjects(false)) /*Run all the service stopping things.*/
	{
		SpitError("Failed to complete shutdown/reboot sequence!");
		EmergencyShell();
	}
	
	ShutdownConfig();
	
	
	if (Signal == OSCTL_LINUX_HALT)
	{
		AttemptMsg = "Attempting to halt the system...";
	}
	else if (Signal == OSCTL_LINUX_POWEROFF)
	{
		AttemptMsg = "Attempting to power down the system...";
	}
	else
	{
		AttemptMsg = "Attempting to reboot the system...";
	}
	
	printf("%s%s%s\n", CONSOLE_COLOR_CYAN, AttemptMsg, CONSOLE_ENDCOLOR);
	
	sync(); /*Force sync of disks in case somebody forgot.*/
	reboot(Signal); /*Send the signal.*/
	
	/*Again, not supposed to be here.*/
	
	SpitError("Failed to reboot/halt/power down!");
	EmergencyShell();
}
