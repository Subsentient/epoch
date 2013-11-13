/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

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
volatile struct _HaltParams HaltParams = { -1, 0, 0, 0, 0, 0 };
Bool AutoMountOpts[5] = { false, false, false, false, false };
static volatile Bool ContinuePrimaryLoop = true;

/*Functions.*/

static void MountVirtuals(void)
{
	enum { MVIRT_PROC, MVIRT_SYSFS, MVIRT_DEVFS, MVIRT_PTS, MVIRT_SHM };
	
	const char *FSTypes[5] = { "proc", "sysfs", "devtmpfs", "devpts", "tmpfs" };
	const char *MountLocations[5] = { "/proc", "/sys", "/dev", "/dev/pts", "/dev/shm" };
	const char *PTSArg = "gid=5,mode=620";
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
			
			if (mount(FSTypes[Inc], MountLocations[Inc], FSTypes[Inc], 0, (Inc == MVIRT_PTS ? PTSArg : NULL)) != 0)
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, sizeof TmpBuf, "Failed to mount virtual filesystem %s!", MountLocations[Inc]);
				SpitWarning(TmpBuf);
				WriteLogLine(TmpBuf, true);
				continue;
			}
			else
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, sizeof TmpBuf, "Mounted virtual filesystem %s", MountLocations[Inc]);
				WriteLogLine(TmpBuf, true);
			}
		}
		
	}
}

static void *PrimaryLoop(void *ContinuePrimaryLoop)
{ /*Loop that provides essentially everything we cycle through.*/
	unsigned long CurMin = 0, CurSec = 0;
	ObjTable *Worker = NULL;
	struct tm TimeStruct;
	time_t TimeCore;
	short ScanStepper = 0;
	
	for (; *(volatile Bool*)ContinuePrimaryLoop; ++ScanStepper)
	{
		usleep(250000); /*Quarter of a second.*/
		
		HandleMemBusPings(); /*Tell clients we are alive if they ask.*/
		
		ParseMemBus(); /*Check membus for new data.*/
		
		if (HaltParams.HaltMode != -1)
		{
			time(&TimeCore);
			localtime_r(&TimeCore, &TimeStruct);
			
			CurMin = TimeStruct.tm_min;
			CurSec = TimeStruct.tm_sec;
			
			/*Allow a membus job to finish before shutdown, but actually do the shutdown afterwards.*/
			if (GetStateOfTime(HaltParams.TargetHour, HaltParams.TargetMin, HaltParams.TargetSec,
					HaltParams.TargetMonth, HaltParams.TargetDay, HaltParams.TargetYear))
			{ /*GetStateOfTime() returns 1 if the passed time is the present, and 2 if it's the past,
				so we can just take whatever positive value we are given.*/		
				LaunchShutdown(HaltParams.HaltMode);
			}
			else if (CurSec == HaltParams.TargetSec && CurMin != HaltParams.TargetMin &&
					DateDiff(HaltParams.TargetHour, HaltParams.TargetMin, NULL, NULL, NULL) <= 20 )
			{ /*If 20 minutes or less until shutdown, warn us every minute.
				* If we miss a report because the membus was being parsed or something,
				* don't try to report it after, because that time has probably passed.*/
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
					char TmpBuf[MAX_LINE_SIZE];
					
					snprintf(TmpBuf, MAX_LINE_SIZE, "AUTORESTART: Object %s is not running. Restarting.", Worker->ObjectID);
					WriteLogLine(TmpBuf, true);
					
					if (ProcessConfigObject(Worker, true, false))
					{
						snprintf(TmpBuf, MAX_LINE_SIZE, "AUTORESTART: Object %s successfully restarted.", Worker->ObjectID);
					}
					else
					{
						snprintf(TmpBuf, MAX_LINE_SIZE, "AUTORESTART: " CONSOLE_COLOR_RED "Failed" CONSOLE_ENDCOLOR
								" to restart object %s automatically.\nMarking object stopped.", Worker->ObjectID);
						Worker->Started = false;
					}
					
					WriteLogLine(TmpBuf, true);
				}
				
				/*Rescan PIDs every minute to keep them up-to-date.*/
				if (ScanStepper == 240 && Worker->Started)
				{
					AdvancedPIDFind(Worker, true);
				}
			}
		}
		
		if (ScanStepper == 240)
		{
			ScanStepper = 0;
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
	
	printf("\n%s\nCompiled %s\n\n", VERSIONSTRING, __DATE__);
	
	/*Set environment variables.*/
	setenv("USER", ENVVAR_USER, true);
	setenv("PATH", ENVVAR_PATH, true);
	setenv("HOME", ENVVAR_HOME, true);
	setenv("SHELL", ENVVAR_SHELL, true);
	
	/*Add tiny message if we passed runlevel= on the kernel cli.*/
	if (*CurRunlevel != '\0')
	{
		printf("Booting to runlevel \"%s\".\n\n", CurRunlevel);
	}

	if (!InitConfig())
	{ /*That is very very bad if we fail here.*/
		EmergencyShell();
	}
	
	PrintBootBanner();

	if (EnableLogging)
	{
		WriteLogLine(CONSOLE_COLOR_CYAN VERSIONSTRING " Booting up\n" "Compiled " __DATE__ CONSOLE_ENDCOLOR "\n", true);
	}
	
	MountVirtuals(); /*Mounts any virtual filesystems, upon request.*/
	
	if (Hostname[0] != '\0')
	{ /*The system hostname.*/
		char TmpBuf[MAX_LINE_SIZE];
		
		if (!sethostname(Hostname, strlen(Hostname)))
		{
			snprintf(TmpBuf, MAX_LINE_SIZE, "Hostname set to \"%s\".", Hostname);
		}
		else
		{
			snprintf(TmpBuf, MAX_LINE_SIZE, "Unable to set hostname to \"%s\"!\n"
					"Ensure that this is a valid hostname.", Hostname);
			SpitWarning(TmpBuf);
		}
		
		WriteLogLine(TmpBuf, true);
	}
	
	if (DisableCAD)
	{	
		const char *CADMsg[2] = { "Epoch has taken control of CTRL-ALT-DEL events.",
							"Epoch was unable to take control of CTRL-ALT-DEL events." };
			
		if (!reboot(OSCTL_LINUX_DISABLE_CTRLALTDEL)) /*Disable instant reboot on CTRL-ALT-DEL.*/
		{			
			WriteLogLine(CADMsg[0], true);
		}
		else
		{
			WriteLogLine(CADMsg[1], true);
			SpitWarning(CADMsg[1]);
		}
	}
	else
	{
		WriteLogLine("Epoch will not request control of CTRL-ALT-DEL events.", true);
	}
	
	WriteLogLine(CONSOLE_COLOR_YELLOW "Starting all objects and services.\n" CONSOLE_ENDCOLOR, true);
	
	if (!RunAllObjects(true))
	{
		EmergencyShell();
	}
	
	if (EnableLogging)
	{ /*Switch logging out of memory mode and write it's memory buffer to disk.*/
		FILE *Descriptor = fopen(LOGDIR LOGFILE_NAME, (BlankLogOnBoot ? "w" : "a"));
		
		LogInMemory = false;
		
		if (MemLogBuffer != NULL)
		{
			if (!Descriptor)
			{
				SpitWarning("Cannot record logs to disk. Shutting down logging.");
				EnableLogging = false;
			}
			else
			{
				fwrite(MemLogBuffer, 1, strlen(MemLogBuffer), Descriptor);
				fflush(Descriptor);
				fclose(Descriptor);
				
				WriteLogLine(CONSOLE_COLOR_GREEN "Completed starting objects and services. Entering standby loop.\n" CONSOLE_ENDCOLOR, true);
			}
			free(MemLogBuffer);
		}
	}
	
	if (!InitMemBus(true))
	{
		const char *MemBusErr = CONSOLE_COLOR_RED "FAILURE IN MEMBUS! "
								"You won't be able to shut down the system with Epoch!"
								CONSOLE_ENDCOLOR;
		
		SpitError(MemBusErr);
		WriteLogLine(MemBusErr, true);
		
		putc('\007', stderr); /*Beep.*/
	}
	
	/*Start the primary loop's thread. It's responsible for parsimg membus,
	 * handling scheduled shutdowns and service auto-restarts, and more.
	 * We pass it a Bool so we can shut it down when the time comes.*/
	pthread_create(&LoopThread, NULL, &PrimaryLoop, (void*)&ContinuePrimaryLoop);
	pthread_detach(LoopThread); /*A lazier way than using attrs.*/
	
	while (!Insane) /*We're still pretty insane.*/
	{ /*Now wait forever.*/
		if (!RunningChildCount)
		{ /*Clean away extra child processes that are started by the rest of the system.
			Do this to avoid zombie process apocalypse.*/
			waitpid(-1, NULL, WNOHANG);
		}
		
		usleep(50000);
	}
}

void LaunchShutdown(signed long Signal)
{ /*Responsible for reboot, halt, power down, etc.*/
	char MsgBuf[MAX_LINE_SIZE];
	const char *HType = NULL;
	const char *AttemptMsg = NULL;
	const char *LogMsg = ((Signal == OSCTL_LINUX_HALT || Signal == OSCTL_LINUX_POWEROFF) ?
						CONSOLE_COLOR_RED "Shutting down." CONSOLE_ENDCOLOR :
						CONSOLE_COLOR_RED "Rebooting." CONSOLE_ENDCOLOR);
	
	switch (Signal)
	{
		case OSCTL_LINUX_HALT:
			HType = "halt";
			break;
		case OSCTL_LINUX_POWEROFF:
			HType = "poweroff";
			break;
		default:
			HType = "reboot";
			break;
	}
	
	snprintf(MsgBuf, sizeof MsgBuf, "System is going down for %s NOW!", HType);
	EmulWall(MsgBuf, false);
	
	WriteLogLine(LogMsg, true);
	

	EnableLogging = false; /*Prevent any additional log entries.*/
	
	ContinuePrimaryLoop = false; /*Bring down the primary loop.*/
	
	if (!ShutdownMemBus(true))
	{ /*Shutdown membus first, so no other signals will reach us.*/
		SpitWarning("Failed to shut down membus interface.");
	}
	
	if (Signal == OSCTL_LINUX_HALT || Signal == OSCTL_LINUX_POWEROFF)
	{
		printf("%s", CONSOLE_COLOR_RED "Shutting down.\n" CONSOLE_ENDCOLOR "\n");
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
