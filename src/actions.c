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
					
					if (!Worker->Opts.HasPIDFile && AdvancedPIDFind(Worker, true))
					{ /* Try to update the PID rather than restart, since some things change their PIDs via forking etc.*/
						continue;
					}
					
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
				if (ScanStepper == 240 && Worker->Started && !Worker->Opts.HasPIDFile)
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
{ /*This is called when we are reexecuted from ReexecuteEpoch() to receive data*/
	pid_t ChildPID = 0;
	ObjTable *CurObj = NULL;
	char InBuf[MEMBUS_SIZE/2 - 1] = { '\0' };
	char *MCode = MEMBUS_CODE_RXD;
	unsigned long MCodeLength = strlen(MCode) + 1;
	short HPS = 0;
	
	MemBusKey = MEMKEY + 1;
	
	if (!InitConfig())
	{
		EmulWall("Epoch: "CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR
		"Cannot reload configuration for re-exec!", false);
		EmergencyShell();
	}

	if (!InitMemBus(false))
	{
		EmulWall("Epoch: "CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR
				"Re-executed process cannot connect to modified membus.", false);
		EmergencyShell();
	}
	
	while (!MemBus_BinRead(InBuf, sizeof InBuf, false)) usleep(100);
	
	memcpy(&ChildPID, InBuf + MCodeLength, sizeof(pid_t));
	
	while (!MemBus_BinRead(InBuf, sizeof InBuf, false)) usleep(100);
	
	while (!strcmp(InBuf, MCode))
	{
		if ((CurObj = LookupObjectInTable(InBuf + MCodeLength)) != NULL)
		{
			unsigned long TLength = strlen(CurObj->ObjectID) + 1;
			
			memcpy(&CurObj->ObjectPID, (InBuf + MCodeLength + TLength), sizeof(long));
			memcpy(&CurObj->Started, (InBuf + MCodeLength + TLength + sizeof(long)), sizeof(Bool));
		}
		
		while (!MemBus_BinRead(InBuf, sizeof InBuf, false)) usleep(100);
	}
	
	MCode = MEMBUS_CODE_RXD_OPTS;
	MCodeLength = strlen(MCode) + 1;
	
	/*Retrieve the HaltParams structure.*/
	memcpy((void*)&HaltParams.HaltMode, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	memcpy((void*)&HaltParams.TargetHour, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	memcpy((void*)&HaltParams.TargetMin, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	memcpy((void*)&HaltParams.TargetSec, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	memcpy((void*)&HaltParams.TargetMonth, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	memcpy((void*)&HaltParams.TargetDay, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	memcpy((void*)&HaltParams.TargetYear, InBuf + MCodeLength + (HPS++ * sizeof(long)), sizeof(long));
	
	/*Retrieve our trinity of important options.*/
	while (!MemBus_BinRead(InBuf, sizeof InBuf, false)) usleep(100);
	EnableLogging = (Bool)*(InBuf + MCodeLength);
	AlignStatusReports = (Bool)*(InBuf + MCodeLength + 1);
	ShellEnabled = (Bool)*(InBuf + MCodeLength + 2);
	
	/*Retrieve the current runlevel.*/
	while (!MemBus_BinRead(InBuf, sizeof InBuf, false)) usleep(100);
	snprintf(CurRunlevel, sizeof CurRunlevel, "%s", InBuf + MCodeLength);
	
	MemBus_Write(MCode, false); /*Tell the child they can quit.*/
	
	/*Wait for the child to terminate.*/
	waitpid(ChildPID, NULL, 0); /*We don't really have to, but I think we should.*/
	
	/**
	 * EVERYTHING BEYOND HERE IS USED TO RESUME NORMAL OPERATION!
	 * **/
	
	/*Bring down the old, custom membus and bring up the classic.*/
	ShutdownMemBus(false);
	MemBusKey = MEMKEY;
	
	if (!InitMemBus(true))
	{
		SpitWarning("Cannot restart normal membus after re-exec. System is otherwise operational.");
	}
	
	/*Reset environment variables.*/
	setenv("USER", ENVVAR_USER, true);
	setenv("PATH", ENVVAR_PATH, true);
	setenv("HOME", ENVVAR_HOME, true);
	setenv("SHELL", ENVVAR_SHELL, true);
	
	/*Restart the PrimaryLoop thread.*/
	memset(&PrimaryLoopThread, 0, sizeof(pthread_t));
	
	pthread_create(&PrimaryLoopThread, NULL, PrimaryLoop, NULL);
	pthread_detach(PrimaryLoopThread);
	
	LogInMemory = false; /*Nothing in here, but we need this to start our logging.*/
	WriteLogLine(CONSOLE_COLOR_GREEN "Re-executed Epoch.\nNow using " VERSIONSTRING
				"\nCompiled " __DATE__ " " __TIME__ "." CONSOLE_ENDCOLOR, true);
	
	/*Now start our eternal 'derp' loop.*/
	while (1)
	{
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
	FILE *TestDescriptor = fopen(EPOCH_BINARY_PATH, "rb");
	char OutBuf[MEMBUS_SIZE/2 - 1] = { '\0' };
	ObjTable *Worker = ObjectTable;
	const char *MCode = MEMBUS_CODE_RXD;
	unsigned long MCodeLength = strlen(MCode) + 1;
	short HPS = 0;
	
	if (!TestDescriptor)
	{
		EmulWall("Epoch: " CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR
				"Unable to read \"" EPOCH_BINARY_PATH "\"! Cannot reexec!", false);
		return;
	}
	else
	{
		fclose(TestDescriptor);
	}
	
	ShutdownMemBus(true); /*We are now going to use a different MemBus key.*/
	MemBusKey = MEMKEY + 1; /*This prevents clients from interfering.*/
	++RunningChildCount; /*Stop the tiny loop that reaps system wide dead processes.*/
	
	if ((PID = fork()) == -1)
	{
		EmulWall("Epoch: " CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR
				"Unable to fork! Aborting reexecution.", false);
				
		MemBusKey = MEMKEY;
		InitMemBus(true); /*Bring back the membus.*/
		
		return;
	}
	
	/*Handle us if we are the original process first.*/
	if (PID > 0)
	{
		WriteLogLine(CONSOLE_COLOR_YELLOW "Re-executing Epoch..." CONSOLE_ENDCOLOR, true);
		
		while (shmget(MEMKEY + 1, MEMBUS_SIZE, 0660) == -1) usleep(100);
		
		sleep(1); /*Wait a second for the child to wipe the membus, so we don't have a nasty,
		* nasty, nasty, nasty, unbelievably nasty heisenbug arise with membus pings not showing up.*/
		
		/**Execute the new binary.**/ /*We pass the custom args to tell us we are re-executing.*/
		execlp(EPOCH_BINARY_PATH, "!rxd", "REEXEC", NULL);
		
		/*Not supposed to be here.*/
		EmulWall(CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR
				"Failed to execute \"" EPOCH_BINARY_PATH "\"! Cannot reexec!", false);
				
		WriteLogLine(CONSOLE_COLOR_RED "Reexecution failed." CONSOLE_ENDCOLOR, true);
		kill(PID, SIGKILL); /*Kill the failed child.*/
		
		if (shmget(MEMKEY + 1, MEMBUS_SIZE, 0660) != -1)
		{
			ShutdownMemBus(true);
		}
		
		MemBusKey = MEMKEY;
		InitMemBus(true);
		
		return;
	}
	
	/**The child is responsible for sending us our data.**/
	if (!InitMemBus(true))
	{
		EmulWall("Epoch: " CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR
				"Re-exec: Child unable to start modified membus. Child terminating.", false);
		exit(1);
	}
	
	while (!HandleMemBusPings())
	{ /*Wait for the re-executed parent process to connect to receive it's config.*/
		static unsigned long Counter = 0;
	
		usleep(1000); /*0.001 seconds.*/
		
		if (Counter == 0) ++Counter;
		else if (Counter >= 10000) /*Sleep ten seconds.*/
		{
			/*Quietly exit.*/
			SpitError("Host process not responding for reexec data exchange. Child exiting.");
			ShutdownMemBus(true);
			
			exit(1);
		}
	}
	
	/*Send our PID. This doubles as a greeting.*/
	PID = getpid();
	MemBus_BinWrite(&PID, sizeof(pid_t), true);
	
	strncpy(OutBuf, MCode, MCodeLength); /*We only need to do this once per MCode change, since we never wipe OutBuf.*/
	
	/*PIDs and started states.
	 * It doesn't matter if they are done eating the PID,
	 * MemBus_*Write() blocks until they're done with the first message.*/
	for (; Worker->Next; Worker = Worker->Next)
	{
		unsigned long TLength = 0;
		strncpy(OutBuf + MCodeLength, Worker->ObjectID, (TLength = strlen(Worker->ObjectID) + 1));
		
		memcpy(OutBuf + MCodeLength + TLength, &Worker->ObjectPID, sizeof(long));
		memcpy(OutBuf + sizeof(long) + TLength + MCodeLength, &Worker->Started, sizeof(Bool));
		
		MemBus_BinWrite(OutBuf, sizeof OutBuf, true);
	}
	
	/*We change to a new code to mark the end of our loop.*/
	strncpy(OutBuf, (MCode = MEMBUS_CODE_RXD_OPTS), (MCodeLength = strlen(MEMBUS_CODE_RXD_OPTS) + 1));
	
	/*HaltParams, we're lazy and just write the whole structure.*/
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.HaltMode, sizeof HaltParams);
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.TargetHour, sizeof HaltParams);
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.TargetMin, sizeof HaltParams);
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.TargetSec, sizeof HaltParams);
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.TargetMonth, sizeof HaltParams);
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.TargetDay, sizeof HaltParams);
	memcpy(OutBuf + MCodeLength + (HPS++ * sizeof(long)), (void*)&HaltParams.TargetYear, sizeof HaltParams);
	
	MemBus_BinWrite(OutBuf, sizeof HaltParams + MCodeLength, true);
	
	/*Misc. global options. We don't include all because only some are used after initial boot.*/
	*(OutBuf + MCodeLength) = (char)EnableLogging;
	*(OutBuf + MCodeLength + 1) = (char)AlignStatusReports;
	*(OutBuf + MCodeLength + 2) = (char)ShellEnabled;
	MemBus_BinWrite(OutBuf, MCodeLength + 3, true);
	
	/*The current runlevel is very important.*/
	strncpy(OutBuf + MCodeLength, CurRunlevel, strlen(CurRunlevel) + 1);
	MemBus_BinWrite(OutBuf, sizeof OutBuf, true);
	
	while (!MemBus_Read(OutBuf, true)) usleep(100); /*Wait for the main process to say we can quit.*/
	ShutdownMemBus(true); /*Nothing is deleted until the new process releases the lock, don't worry.*/
	ShutdownConfig();
	
	exit(0);
}

void LaunchBootup(void)
{ /*Handles what would happen if we were PID 1.*/
	Bool Insane = false;
	
	/*I am going to grumble about the type of pthread_t being implementation defined.*/
	/*Grumble.*/
	memset(&PrimaryLoopThread, 0, sizeof(pthread_t));
	
	setsid();
	
	printf("\n%s\nCompiled %s\n\n", VERSIONSTRING, __DATE__ " " __TIME__);
	
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
		WriteLogLine(CONSOLE_COLOR_CYAN VERSIONSTRING " Booting up\n" "Compiled " __DATE__ " " __TIME__ CONSOLE_ENDCOLOR "\n", true);
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
