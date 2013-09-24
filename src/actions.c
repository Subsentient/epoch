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
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>
#include "epoch.h"

/*Prototypes.*/
static void MountVirtuals(void);
static void *PrimaryLoop(void *ContinuePrimaryLoop);

/*Globals.*/
struct _HaltParams HaltParams = { -1, 0, 0, 0, 0, 0 };
Bool AutoMountOpts[5] = { false, false, false, false, false };
static Bool ContinuePrimaryLoop = true;

/*Functions.*/

static void MountVirtuals(void)
{
	const char *FSTypes[5] = { "proc", "sysfs", "devtmpfs", "devpts", "tmpfs" };
	const char *MountLocations[5] = { "/proc", "/sys", "/dev", "/dev/pts", "/dev/shm" };
	Bool HeavyPermissions[5] = { true, true, true, true, false };
	mode_t PermissionSet[2] = { (S_IRWXU | S_IRWXG | S_IRWXO), (S_IRWXU | S_IRGRP | S_IXGRP | S_IXOTH) };
	short Inc = 0;
	
	for (; Inc < 5; ++Inc)
	{
		if (AutoMountOpts[Inc])
		{
			if (AutoMountOpts[Inc] == 2)
			{ /*If we need to create a directory, do it.*/
				if (mkdir(MountLocations[Inc], PermissionSet[HeavyPermissions[Inc]] != 0))
				{
					char TmpBuf[1024];
					
					snprintf(TmpBuf, sizeof TmpBuf, "Failed to create directory for %s!", MountLocations[Inc]);
					SpitWarning(TmpBuf);
				} /*No continue statement because it might already exist*/
			}	/*and we might be able to mount it anyways.*/
			
			if (mount(FSTypes[Inc], MountLocations[Inc], FSTypes[Inc], 0, NULL) != 0)
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, sizeof TmpBuf, "Failed to mount %s!", MountLocations[Inc]);
				SpitWarning(TmpBuf);
				continue;
			}
		}
		
	}
}

static void *PrimaryLoop(void *ContinuePrimaryLoop)
{ /*Loop that provides essentially everything we cycle through.*/
	unsigned long CurHr, CurMin, CurSec, CurMon, CurDay, CurYear;
	ObjTable *Worker = NULL;
	struct tm *TimePtr;
	time_t TimeCore;
	
	while (*(Bool*)ContinuePrimaryLoop)
	{
		usleep(250000); /*Quarter of a second.*/
		
		ParseMemBus(); /*Check membus for new data.*/
		
		if (HaltParams.HaltMode != -1)
		{
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
				CurDay == HaltParams.TargetDay && CurYear == HaltParams.TargetYear)
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
			else if (CurSec == HaltParams.TargetSec && CurMin != HaltParams.TargetMin &&
					DateDiff(HaltParams.TargetHour, HaltParams.TargetMin, NULL, NULL, NULL) <= 20 )
			{ /*If 20 minutes or less until shutdown, warn us every minute.*/
				char TBuf[MAX_LINE_SIZE];
				const char *HaltMode = NULL;
				static short LastMin = -1;

				if (LastMin != CurMin)
				{ /*Don't repeat ourselves 80 times while the second rolls over.*/
					if (HaltParams.HaltMode == OSCTL_LINUX_HALT)
					{
						HaltMode = "halt";
					}
					else if (HaltParams.HaltMode == OSCTL_LINUX_POWEROFF)
					{
						HaltMode = "poweroff";
					}
					else
					{
						HaltParams.HaltMode = OSCTL_LINUX_REBOOT;
						HaltMode = "reboot";
					}
					
					snprintf(TBuf, sizeof TBuf, "System is going down for %s in %lu minutes!",
							HaltMode, DateDiff(HaltParams.TargetHour, HaltParams.TargetMin, NULL, NULL, NULL));
					EmulWall(TBuf, false);
					
					LastMin = CurMin;
				}
			}
		}
		
		if (ObjectTable)
		{
			for (Worker = ObjectTable; Worker->Next != NULL; Worker = Worker->Next)
			{ /*Handle objects intended for automatic restart.*/
				if (Worker->Opts.AutoRestart && Worker->Started && !ObjectProcessRunning(Worker))
				{
					ProcessConfigObject(Worker, true);
				}
			}
		}
		
		/*Lots of brilliant code here, but I typed it in invisible pixels.*/
	}
	
	return NULL;
}

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
	pthread_t LoopThread;
	Bool Insane = false;
	
	setsid();
	
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
	
	MountVirtuals(); /*Mounts any virtual filesystems, upon request.*/
	
	if (Hostname[0] != '\0')
	{ /*The system hostname.*/
		sethostname(Hostname, strlen(Hostname));
	}
	if (DisableCAD)
	{
		reboot(OSCTL_LINUX_DISABLE_CTRLALTDEL); /*Disable instant reboot on CTRL-ALT-DEL.*/
	}

	pthread_create(&LoopThread, NULL, &PrimaryLoop, &ContinuePrimaryLoop);
	pthread_detach(LoopThread); /*A lazier way than using attrs.*/
	
	if (!RunAllObjects(true))
	{
		EmergencyShell();
	}
	
	while (!Insane) /*We're still pretty insane.*/
	{ /*Now wait forever.*/
		usleep(1000);
		
		if (!RunningChildCount)
		{ /*Clean away extra child processes that are started by the rest of the system.
			Do this to avoid zombie process apocalypse.*/
			waitpid(-1, NULL, WNOHANG);
		}
	}
}

void LaunchShutdown(signed long Signal)
{ /*Responsible for reboot, halt, power down, etc.*/
	const char *AttemptMsg = NULL;
	
	ContinuePrimaryLoop = false; /*Bring down the primary loop.*/
	
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
