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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <pthread.h>
#include "epoch.h"

/*Prototypes.*/
static void MountVirtuals(void);
static void *PrimaryLoop(void *UselessArg);

/*Globals.*/
volatile struct _HaltParams HaltParams = { -1, 0, 0, 0, 0, 0 };
Bool AutoMountOpts[5] = { false, false, false, false, false };
static volatile Bool ContinuePrimaryLoop = true;
static pthread_t PrimaryLoopThread; /*This isn't really changed much once we launch our thread,
* so we don't need to make it volatile.*/

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

static void *PrimaryLoop(void *UselessArg)
{ /*Loop that provides essentially everything we cycle through.*/
	unsigned long CurMin = 0, CurSec = 0;
	ObjTable *Worker = NULL;
	struct tm TimeStruct;
	time_t TimeCore;
	short ScanStepper = 0;
	
	(void)UselessArg;
	
	for (; ContinuePrimaryLoop; ++ScanStepper)
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
	
	ContinuePrimaryLoop = true;
	/*We do this so LaunchShutdown(), when called via SigHandler(),
	will know that we exited gracefully and not kill us.*/
	
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

void RecoverFromReexec(void)
{ /*This is called when we are reexecuted from ReexecuteEpoch() to receive data
	* from our child we started before we called execlp(). It will send back our tree
	* over membus along with other options, and we need to reassemble it.*/
	char MCode[MAX_DESCRIPT_SIZE] = MEMBUS_CODE_RXD, RecvData[MEMBUS_SIZE/2 - 1] = { '\0' };
	ObjTable *CurObj = NULL;
	unsigned long Inc = 0, MCodeLength = strlen(MCode) + 1;
	const void *const HaltParamData[7] = { (void*)&HaltParams.HaltMode, (void*)&HaltParams.TargetYear,
										(void*)&HaltParams.TargetMonth, (void*)&HaltParams.TargetDay,
										(void*)&HaltParams.TargetHour, (void*)&HaltParams.TargetMin,
										(void*)&HaltParams.TargetSec };
									
	const char *const SuccessMSG = CONSOLE_COLOR_GREEN "Successfully re-executed Epoch." CONSOLE_ENDCOLOR;
	pid_t ChildPID = 0;
	
	++MemBusKey; /*We are using one above to keep clients from interfering.*/
	
	if (!InitConfig() || !ObjectTable)
	{
		SpitError("Unable to reload configuration from disk during reexec!");
		EmergencyShell();
	}
		
	if (!InitMemBus(false))
	{
		SpitError("Unable to connect to child via membus to complete reexec!");
		EmergencyShell();
	}
	
	MemBus_Write(MEMBUS_CODE_RXD, false); /*Tell the child we are ready to receive the remote data.*/
	
	/*Retrieve the PID.*/
	while (!MemBus_BinRead(RecvData, sizeof(pid_t), false)) usleep(100);
	ChildPID = *(pid_t*)RecvData;
	
	while (!MemBus_BinRead(RecvData, sizeof RecvData, false)) usleep(100);
	
	while (!strncmp(RecvData, MCode, strlen(MCode)))
	{
		if ((CurObj = LookupObjectInTable(RecvData + MCodeLength)))
		{
			CurObj->ObjectPID = *(unsigned long*)(RecvData + strlen(RecvData) + 1);
			CurObj->Started = *(Bool*)(RecvData + strlen(RecvData) + 1 + sizeof(long));
		}
		
		while (!MemBus_BinRead(RecvData, sizeof RecvData, false)) usleep(100);
	}
			
	strncpy(MCode, RecvData, strlen(RecvData) + 1);
	MCodeLength = strlen(MCode) + 1;
	
	 
	/*HaltParams*/
	for (Inc = 0; Inc < 7; ++Inc)
	{
		 /*This isn't always the correct type, but it's irrelevant because they are the same size.*/
		*(long*)(HaltParamData[Inc]) = *(long*)(RecvData + MCodeLength);
		
		while (!MemBus_BinRead(RecvData, sizeof RecvData, false)) usleep(100);
	}
	
	/*CurRunlevel*/
	strncpy(CurRunlevel, RecvData + MCodeLength, strlen(RecvData + MCodeLength));
	
	/*EnableLogging and AlignStatusReports*/
	while (!MemBus_BinRead(RecvData, sizeof RecvData, false)) usleep(100);
	EnableLogging = *(Bool*)(RecvData + MCodeLength);
	AlignStatusReports = *(Bool*)(RecvData + MCodeLength + sizeof(Bool));
	
	MemBus_Write(MEMBUS_CODE_RXD, false);
	
	waitpid(ChildPID, NULL, 0);
	
	shmdt((void*)MemData);/*Detach the process.*/
	
	/*Reset environment.*/
	setenv("USER", ENVVAR_USER, true);
	setenv("PATH", ENVVAR_PATH, true);
	setenv("HOME", ENVVAR_HOME, true);
	setenv("SHELL", ENVVAR_SHELL, true);
	
	/*If logging is enabled, we need to do this to write to disk.*/
	LogInMemory = false;
	
	MemBusKey = MEMKEY;
	
	shmdt((void*)MemData); /*I don't care if it fails.*/
	BusRunning = false;
	
	
	if (!InitMemBus(true))
	{
		SpitError("Unable to initialize membus after reexec's config transfer!");
		EmergencyShell();
	}
	
	memset(&PrimaryLoopThread, 0, sizeof(pthread_t));
	
	/*Restart the primary loop.*/
	pthread_create(&PrimaryLoopThread, NULL, &PrimaryLoop, NULL);
	pthread_detach(PrimaryLoopThread);

	WriteLogLine(SuccessMSG, true);
	puts(SuccessMSG);
	fflush(NULL);
	
	while (1)
	{ /*Now sleep forever as usual.*/
		if (!RunningChildCount)
		{
			waitpid(-1, NULL, WNOHANG);
		}
		
		usleep(50000);
	}
	
}

void ReexecuteEpoch(void)
{ /*Used when Epoch needs to be restarted after we already booted.*/
	pid_t PID = 0;
	const char * const StartMSG = CONSOLE_COLOR_YELLOW "Re-executing Epoch..." CONSOLE_ENDCOLOR;
	
	WriteLogLine(StartMSG, true);
	puts(StartMSG);
	
	/*Stop the process harvesting loop.*/
	++RunningChildCount;
	
	ShutdownMemBus(true); /*Bring down our existing copy, since it's the fork that will be the server.*/
	
	/*Incrememt the membus key to prevent clients from interfering with the restart.*/
	++MemBusKey;
	
	if ((PID = fork()) == -1)
	{
		SpitError("ReexecuteEpoch(): Unable to fork()!");
		EmergencyShell();
	}
	
	if (PID == 0)
	{
		ObjTable *Worker = ObjectTable;
		unsigned long Inc = 0;
		char MemBusResponse[MEMBUS_SIZE/2 - 1] = { '\0' }, OutBuf[MEMBUS_SIZE/2 - 1] = { '\0' };
		const void *HaltParamData[7] = { (void*)&HaltParams.HaltMode, (void*)&HaltParams.TargetYear,
										(void*)&HaltParams.TargetMonth, (void*)&HaltParams.TargetDay,
										(void*)&HaltParams.TargetHour, (void*)&HaltParams.TargetMin,
										(void*)&HaltParams.TargetSec };
		const char *MCode = MEMBUS_CODE_RXD;
		unsigned long MCodeLength = strlen(MCode) + 1;
		
		if (!InitMemBus(true))
		{
			EmulWall("ERROR: Epoch child unable to connect to modified membus for reexec!", false);
			EmergencyShell();
			
		}

		while (!HandleMemBusPings()) usleep(100); /*The client verifies a connection this way.*/

		while (!MemBus_Read(MemBusResponse, true)) usleep(100); /*We don't care about the value.*/
		
		/*First thing we send is the PID.*/
		PID = getpid();
		MemBus_BinWrite(&PID, sizeof(pid_t), true);
		
		for (; Worker->Next != NULL; Worker = Worker->Next)
		{
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MCode, Worker->ObjectID);
			*(unsigned long*)&OutBuf[strlen(OutBuf) + 1] = Worker->ObjectPID;
			*(Bool*)&OutBuf[strlen(OutBuf) + 1 + sizeof(long)] = Worker->Started;
			
			if (!MemBus_BinWrite(OutBuf, sizeof OutBuf, true))
			{
				printf("REEXEC: " CONSOLE_COLOR_RED "Failed" CONSOLE_ENDCOLOR
						" to send status of object %s!\n", Worker->ObjectID);
				fflush(NULL);
			}
			
		}
			
		MCode = MEMBUS_CODE_RXD_OPTS;
		MCodeLength = strlen(MCode) + 1;

		/*HaltParams*/
		for (; Inc < 7; ++Inc)
		{			
			strncpy(OutBuf, MCode, MCodeLength);
			memcpy(&OutBuf[MCodeLength], HaltParamData[Inc], sizeof(long));
			
			MemBus_BinWrite(OutBuf, sizeof OutBuf, true);
		}
		

		/*CurRunlevel*/
		snprintf(OutBuf, sizeof OutBuf, "%s%s", MCode, CurRunlevel);
		MemBus_Write(OutBuf, true);
		

		/*EnableLogging and AlignStatusReports*/
		strncpy(OutBuf, MCode, MCodeLength);
		
		memcpy(&OutBuf[MCodeLength], &EnableLogging, sizeof(Bool));
		memcpy(&OutBuf[MCodeLength + sizeof(Bool)], &AlignStatusReports, sizeof(Bool));
		
		MemBus_BinWrite(OutBuf, MCodeLength + sizeof(Bool) * 2, true);
		
		while (!MemBus_Read(MemBusResponse, true)) usleep(100); /*we don't care about the value.*/
		
		/*We're done with this copy. The rest is up to the re-executed original.*/
		ShutdownConfig();
		ShutdownMemBus(true);

		exit(0);
	}
	/*The original process re-execs itself.*/
	else
	{
		/*Wait for the other side to bring up the membus.*/
		while (shmget(MemBusKey, MEMBUS_SIZE, 0660) == - 1) usleep(100);
		
		execlp(EPOCH_BINARY_PATH, "!rxd", "REEXEC", NULL); /*I'll *never* tell.*/
	}
		
}

void LaunchBootup(void)
{ /*Handles what would happen if we were PID 1.*/
	Bool Insane = false;
	
	/*I am going to grumble about the type of pthread_t being implementation defined.*/
	/*Grumble.*/
	memset(&PrimaryLoopThread, 0, sizeof(pthread_t));
	
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
	pthread_create(&PrimaryLoopThread, NULL, &PrimaryLoop, NULL);
	pthread_detach(PrimaryLoopThread); /*A lazier way than using attrs.*/
	
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
	
	if (!pthread_equal(pthread_self(), PrimaryLoopThread))
	{ /*We need to kill the primary loop if we aren't it.
		This happens when we are rebooted via CTRL-ALT-DEL.*/
		short Inc = 0;
		
		/*Give it a moment to come down peacefully.*/
		for (; !ContinuePrimaryLoop && Inc < 40; ++Inc) /*40/4 = 10 secs*/
		{
			usleep(250000);/*.25 seconds.*/
			
			if (Inc == 8) /*Warn us what we're waiting for after two seconds.*/
			{
				printf("Waiting for primary loop to exit...\n");
			}
		}
		
		if (!ContinuePrimaryLoop) /*Loop hasn't set the flag to 'true' again to let us know it's cooperating?*/
		{
			/*Too late.*/
			pthread_kill(PrimaryLoopThread, SIGKILL);
		}
	}
	
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
