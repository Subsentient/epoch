/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

/**CLI parsing, etc. main() is here.**/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <sys/reboot.h>
#include <sys/shm.h>

#ifndef NO_EXECINFO
#include <execinfo.h>
#endif

#include "epoch.h"

#define ArgIs(z) !strcmp(CArg, z)
#define CmdIs(z) __CmdIs(argv[0], z)

/*Forward declarations for static functions.*/
static ReturnCode ProcessGenericHalt(int argc, char **argv);
static Bool __CmdIs(const char *CArg, const char *InCmd);
static void PrintEpochHelp(const char *RootCommand, const char *InCmd);
static ReturnCode HandleEpochCommand(int argc, char **argv);
static void SigHandler(int Signal);
static void SetDefaultProcessTitle(int argc, char **argv);

/*
 * Actual functions.
 */
 
 
Bool AreInit;

static Bool __CmdIs(const char *CArg, const char *InCmd)
{ /*Check if we are or end in the command name specified.*/
	const char *TWorker = CArg;
	
	if ((TWorker = strstr(CArg, InCmd)))
	{
		while (*TWorker != '\0') ++TWorker;
		TWorker -= strlen(InCmd);
		
		if (!strcmp(TWorker, InCmd))
		{
			return true;
		}
	}
			
	return false;
}

static void SigHandler(int Signal)
{
	const char *ErrorM = NULL;
	char OutMsg[MAX_LINE_SIZE * 2] = { '\0' };
	static Bool RecursiveProblem = false;
#ifndef NO_EXECINFO
	void *BTList[25];
	char **BTStrings;
	size_t BTSize;
	char *TWorker = OutMsg;
#endif
	
	switch (Signal)
	{
		case SIGINT:
		{
			static unsigned LastKillAttempt = 0;
			
			if (AreInit)
			{
				if (CurrentTask.Set && CurrentBootMode != BOOT_NEUTRAL
					&& (LastKillAttempt == 0 || CurrentBootMode == BOOT_SHUTDOWN || time(NULL) > (LastKillAttempt + 5)))
				{
					char MsgBuf[MAX_LINE_SIZE];
					ReturnCode KilledOK = SUCCESS;
					
					snprintf(MsgBuf, sizeof MsgBuf, 
							"\n%sKilling task %s. %s",
							CONSOLE_COLOR_YELLOW, CurrentTask.TaskName, CONSOLE_ENDCOLOR);

					if (CurrentBootMode == BOOT_BOOTUP)
					{
						strncat(MsgBuf, "Press CTRL-ALT-DEL within 5 seconds to reboot.", MAX_LINE_SIZE - strlen(MsgBuf) - 1);
					}
					
					puts(MsgBuf);
					
					WriteLogLine(MsgBuf, true);
					
					if (CurrentTask.PID == 0)
					{
						Bool *TPtr = (void*)CurrentTask.Node;
						
						*TPtr = true;
						
						KilledOK = SUCCESS;
					}
					else
					{
						KilledOK = !kill(CurrentTask.PID, SIGKILL);
					}
					
					if (!KilledOK)
					{
						snprintf(MsgBuf, sizeof MsgBuf, "%sUnable to kill %s.%s",
								CONSOLE_COLOR_RED, CurrentTask.TaskName, CONSOLE_ENDCOLOR);
					}
					else
					{
						snprintf(MsgBuf, sizeof MsgBuf, "%s%s was successfully killed.%s", CONSOLE_COLOR_GREEN,
								CurrentTask.TaskName, CONSOLE_ENDCOLOR);
					}
					puts(MsgBuf);
					
					WriteLogLine(MsgBuf, true);
					
					LastKillAttempt = time(NULL);
					return;
				}
				else
				{
					if (CurrentBootMode == BOOT_SHUTDOWN)
					{
						puts(CONSOLE_COLOR_YELLOW "System halt/reboot already in progress." CONSOLE_ENDCOLOR);
						return;
					}
					else
					{
						LaunchShutdown(OSCTL_REBOOT);
					}
				}
			}
			else
			{
				puts("SIGINT received. Exiting.");
				ShutdownMemBus(false);
				exit(0);
			}
		}
		case SIGSEGV:
		{
			ErrorM = "A segmentation fault has occurred in Epoch!";
			break;
		}
		case SIGILL:
		{
			ErrorM = "Epoch has encountered an illegal instruction!";
			break;
		}
		case SIGFPE:
		{
			ErrorM = "Epoch has encountered an arithmetic error!";
			break;
		}
		case SIGABRT:
		{
			ErrorM = "Epoch has received an abort signal!";
			break;
		}
		case SIGUSR2: /**We are init and being ordered to restart ourselves.**/
		{
			WriteLogLine(CONSOLE_COLOR_RED "Received SIGUSR2, reexecuting as requested." CONSOLE_ENDCOLOR, true);
			ReexecuteEpoch();
			return;
		}
		
	}
	
	if (RecursiveProblem)
	{
		EmulWall("Epoch: Recursive fault detected. Sleeping forever.", false);
		while (1) sleep(1);
	}
	
	RecursiveProblem = true;
	
#ifndef NO_EXECINFO
	BTSize = backtrace(BTList, 25);
	BTStrings = backtrace_symbols(BTList, BTSize);

	snprintf(OutMsg, sizeof OutMsg, "%s\n\nBacktrace:\n", ErrorM);
	TWorker += strlen(TWorker);
	
	for (; BTSize > 0 && *BTStrings != NULL; --BTSize, ++BTStrings, TWorker += strlen(TWorker))
	{
		snprintf(TWorker, sizeof OutMsg - strlen(OutMsg) - 1, "\n%s", *BTStrings);
	}
#else
	snprintf(OutMsg, sizeof OutMsg, "%s\n\nEpoch was compiled without backtrace support.", ErrorM);
#endif
	
	if (AreInit)
	{
		EmulWall(OutMsg, false);
		EmergencyShell();
	}
	else
	{
		SpitError(OutMsg);
		exit(1);
	}
}
	
static void PrintEpochHelp(const char *RootCommand, const char *InCmd)
{ /*Used for help for the epoch command.*/
	const char *HelpMsgs[] =
	{ 
		("[poweroff/halt/reboot]:\n\t" CONSOLE_ENDCOLOR
		
		 "Enter poweroff, halt, or reboot to do the obvious."
		),
		
		( "shutdown [-r/-p/-h] <time>:\n\t" CONSOLE_ENDCOLOR
		  "Wrapper for the 'shutdown' command. See 'shutdown --help' for more."
		),
		
		( "[disable/enable] objectid:\n\t" CONSOLE_ENDCOLOR
		  "Enter disable or enable followed by an object ID to disable or enable\n\tthat object."
		),
		
		( "[start/stop/restart] objectid:\n\t" CONSOLE_ENDCOLOR
		  "Enter start, stop, or restart followed by an object ID to control\n\tthat object."
		),
		
		( "reload objectid:\n\t" CONSOLE_ENDCOLOR
		  "If a reload command exists for the object specified,\n\tthe object is reloaded."
		),
		
		( "objrl objectid [del/add/check] runlevel:\n\t" CONSOLE_ENDCOLOR
		
		  "runlevel del and add do pretty much what it sounds like,\n\t"
		  "and check will tell you if that object is enabled for that runlevel."
		),
		
		( "status [objectid]:\n\t" CONSOLE_ENDCOLOR
		
		  "Prints information about the object specified.\n\t"
		  "If an object is not specified, it prints info on all known objects."
		),
		( "setcad [on/off]:\n\t" CONSOLE_ENDCOLOR
		
		  "Sets Ctrl-Alt-Del instant reboot modes. If set to on,\n\t"
		  "striking Ctrl-Alt-Del at a console will instantly reboot the system\n\t"
		  "without intervention by Epoch. Otherwise, if set to off, Epoch will\n\t"
		  "perform a normal reboot when Ctrl-Alt-Del is pressed."
		),
		
		( "configreload:\n\t" CONSOLE_ENDCOLOR
		
		  "Enter configreload to reload the configuration file from disk.\n\t"
		  "This is useful for when you change it\n\t"
		  "to add or remove services, change runlevels, and more."
		),
		
		( "reexec:\n\t" CONSOLE_ENDCOLOR
		
		  "Enter reeexec to partially restart Epoch from disk.\n\t"
		  "This is necessary for updating the Epoch binary to prevent\n\t"
		  "a failure with unmounting the filesystem the binary is on."
		),
		
		( "runlevel:\n\t" CONSOLE_ENDCOLOR
		
		  "Enter runlevel without any arguments to print the current runlevel,\n\t"
		  "or enter an argument as the new runlevel."
		),
		
		( "getpid objectid:\n\t" CONSOLE_ENDCOLOR
		
		  "Retrieves the PID Epoch has on record for the given object.\n\t"
		  "If a PID file is specified, then the PID will be gotten from there."
		),
		
		( "kill objectid:\n\t" CONSOLE_ENDCOLOR
		
		  "Sends SIGKILL to the object specified. If a PID file is specified,\n\t"
		  "the PID will be retrieved from that."
		),
		
		( "version:\n\t" CONSOLE_ENDCOLOR
		
		  "Prints the current version of the Epoch Init System."
		)
	};
	enum { HCMD, SHTDN, ENDIS, STAP, REL, OBJRL, STATUS, SETCAD, CONFRL, REEXEC,
		RLCTL, GETPID, KILLOBJ, VER, ENUM_MAX };
	
	printf("%s\nCompiled %s %s\n\n", VERSIONSTRING, __DATE__, __TIME__);
	
	if (InCmd == NULL)
	{
		short Inc = 0;
		
		puts(CONSOLE_COLOR_RED "Printing all help.\n" CONSOLE_ENDCOLOR "-----\n");
		
		for (; Inc < ENUM_MAX; ++Inc)
		{
			printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[Inc]);
		}
	}
	else if (!strcmp(InCmd, "poweroff") || !strcmp(InCmd, "halt") || !strcmp(InCmd, "reboot"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[HCMD]);
		return;
	}
	else if (!strcmp(InCmd, "shutdown"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[SHTDN]);
		return;
	}
	else if (!strcmp(InCmd, "disable") || !strcmp(InCmd, "enable"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[ENDIS]);
		return;
	}
	else if (!strcmp(InCmd, "start") || !strcmp(InCmd, "stop") || !strcmp(InCmd, "restart"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[STAP]);
		return;
	}
	else if (!strcmp(InCmd, "objrl"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[OBJRL]);
		return;
	}
	else if (!strcmp(InCmd, "status"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[STATUS]);
		return;
	}
	else if (!strcmp(InCmd, "setcad"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[SETCAD]);
		return;
	}
	else if (!strcmp(InCmd, "configreload"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[CONFRL]);
		return;
	}
	else if (!strcmp(InCmd, "reexec"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[REEXEC]);
		return;
	}
	else if (!strcmp(InCmd, "reload"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[REL]);
		return;
	}
	else if (!strcmp(InCmd, "runlevel"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[RLCTL]);
		return;
	}
	else if (!strcmp(InCmd, "getpid"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[GETPID]);
		return;
	}
	else if (!strcmp(InCmd, "kill"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[KILLOBJ]);
		return;
	}
	else if (!strcmp(InCmd, "version"))
	{
		printf(CONSOLE_COLOR_GREEN "%s %s\n\n", RootCommand, HelpMsgs[VER]);
		return;
	}
	else
	{
		fprintf(stderr, "Unknown command name, \"%s\".\n", InCmd);
		return;
	}
}

static ReturnCode ProcessGenericHalt(int argc, char **argv)
{
	/*Figure out what we are.*/
	if (CmdIs("poweroff") || CmdIs("halt") || CmdIs("reboot"))
	{
		char *GCode = NULL, *SuccessMsg = NULL, *FailMsg[2] = { NULL, NULL };
		const char *CArg = NULL;
		signed OSCode = -1;

		if (CmdIs("poweroff"))
		{
			GCode = MEMBUS_CODE_POWEROFF;
			OSCode = OSCTL_POWEROFF;
			SuccessMsg = "Power off in progress.";
			FailMsg[0] = "Failed to request immediate poweroff.";
			FailMsg[1] = "Failed to request poweroff.";
		}
		else if (CmdIs("reboot"))
		{
			GCode = MEMBUS_CODE_REBOOT;
			OSCode = OSCTL_REBOOT;
			SuccessMsg = "Reboot in progress.";
			FailMsg[0] = "Failed to request immediate reboot.";
			FailMsg[1] = "Failed to request reboot.";
		}
		else if (CmdIs("halt"))
		{
			GCode = MEMBUS_CODE_HALT;
			OSCode = OSCTL_HALT;
			SuccessMsg = "System halt in progress.";
			FailMsg[0] = "Failed to request immediate halt.";
			FailMsg[1] = "Failed to request halt.";
		}
		else
		{ /*Why are we called for a different task?*/
			SpitError("ProcessGenericHalt(): We are being called for a task"
					"other than shutdown procedures.\nThis is probably a bug. Please report.");
			return FAILURE;
		}
		
		
		if ((CArg = argv[1]))
		{
			if (argc == 2 && ArgIs("-f"))
			{
				sync();
				reboot(OSCode);
			}
			else
			{
				puts("Bad argument(s).");
				return FAILURE;
			}
		}
		else
		{
			if (!SendPowerControl(GCode))
			{
				SpitError(FailMsg[1]);
				return FAILURE;
			}
			else
			{
				printf("\n%s\n", SuccessMsg);
			}
			
		}
	}
	else
	{
		return FAILURE;
	}
	
	return SUCCESS;
}

static ReturnCode HandleEpochCommand(int argc, char **argv)
{
	const char *CArg = argv[1];
	
	/*No arguments?*/
	if (argc == 1)
	{
		PrintEpochHelp(argv[0], NULL);;
		return SUCCESS;
	}
	
	/*Help parser and shutdown commands (for possible -f).*/
	if (ArgIs("help"))
	{
		if (argc == 2)
		{
			PrintEpochHelp(argv[0], NULL);
		}
		else if (argc == 3)
		{
			PrintEpochHelp(argv[0], argv[2]);
		}
		else
		{
			puts("Too many arguments.\n");
			return FAILURE;
		}
	
		return SUCCESS;
	}
	else if (ArgIs("--version") || ArgIs("version") || ArgIs("-v"))
	{
		if (argc > 2)
		{
			puts("Too many arguments.\n");
			return FAILURE;
		}
		
		printf("%s\nCompiled %s %s\n", VERSIONSTRING, __DATE__, __TIME__);
		return SUCCESS;
	}
	else if (ArgIs("poweroff") || ArgIs("reboot") || ArgIs("halt"))
	{
		Bool RVal;
		
		if (argc > 3)
		{
			puts("Too many arguments.\n");
			PrintEpochHelp(argv[0], "reboot");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			
			return FAILURE;
		}
		
		RVal = !ProcessGenericHalt(argc - 1, argv + 1);
		
		ShutdownMemBus(false);
		return (int)RVal;
	}
	else if (ArgIs("shutdown"))
	{
		return EmulShutdown(argc - 1, (const char**)argv + 1);
	}
	else if (ArgIs("reexec"))
	{
		char InStream[MAX_LINE_SIZE];
		
		if (argc > 2)
		{
			puts("Too many arguments.\n");
			PrintEpochHelp(argv[0], "reexec");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			return FAILURE;
		}
		
		MemBus_Write(MEMBUS_CODE_RXD, false);
		
		puts("Re-executing Epoch.");
		
		ShutdownMemBus(false);
		while (shmget(MEMKEY, MEMBUS_SIZE, 0660) != -1) usleep(100); /*Wait for it to quit...*/
		while (shmget(MEMKEY, MEMBUS_SIZE, 0660) == -1) usleep(100); /*Then wait for it to start...*/
		InitMemBus(false);

		while (!MemBus_Read(InStream, false)) usleep(100);
		
		if (!strcmp(InStream, MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_RXD))
		{
			puts("Reexecution successful.");
		}
		else
		{
			puts(CONSOLE_COLOR_RED "FAILED TO REEXECUTE!" CONSOLE_ENDCOLOR);
		}
		
		ShutdownMemBus(false);
		
		return SUCCESS;
	}	
	else if (ArgIs("configreload"))
	{
		char TRecv[MEMBUS_MSGSIZE];
		char TBuf[3][MAX_LINE_SIZE];
		
		if (argc > 2)
		{
			puts("Too many arguments.\n");
			PrintEpochHelp(argv[0], "configreload");
			return FAILURE;
		}

		if (!InitMemBus(false))
		{
			
			return FAILURE;
		}

		if (!MemBus_Write(MEMBUS_CODE_RESET, false))
		{
			SpitError("Failed to write to membus.");
			ShutdownMemBus(false);
			return FAILURE;
		}
		
		while (!MemBus_Read(TRecv, false)) usleep(1000);
		
		snprintf(TBuf[0], sizeof TBuf[0], "%s %s", MEMBUS_CODE_ACKNOWLEDGED, MEMBUS_CODE_RESET);
		snprintf(TBuf[1], sizeof TBuf[1], "%s %s", MEMBUS_CODE_FAILURE, MEMBUS_CODE_RESET);
		snprintf(TBuf[2], sizeof TBuf[2], "%s %s", MEMBUS_CODE_BADPARAM, MEMBUS_CODE_RESET);
		
		if (!strcmp(TBuf[0], TRecv))
		{
			puts("Reload successful.");
			ShutdownMemBus(false);
			
			return SUCCESS;
		}
		else if (!strcmp(TBuf[1], TRecv))
		{
			puts("Reload failed!");
			ShutdownMemBus(false);
			
			return FAILURE;
		}
		else if (!strcmp(TBuf[2], TRecv))
		{
			SpitError("We are being told that MEMBUS_CODE_RESET is not a valid signal! Please report to Epoch.");
			ShutdownMemBus(false);
			
			return FAILURE;
		}
		else
		{
			SpitError("Unknown response received! Can't handle this! Report to Epoch please!");
			ShutdownMemBus(false);
			
			return FAILURE;
		}
	}
	else if (ArgIs("status"))
	{
		char OutBuf[MEMBUS_MSGSIZE], InBuf[MEMBUS_MSGSIZE];
		char *Worker = NULL;
		const char *const YN[2] = { CONSOLE_COLOR_RED "No" CONSOLE_ENDCOLOR,
									CONSOLE_COLOR_GREEN "Yes" CONSOLE_ENDCOLOR };
		
		if (argc > 3)
		{
			puts("Too many arguments.");
			PrintEpochHelp(argv[0], "status");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			return FAILURE;
		}
		
		/*Send the activation code.*/
		if (argc == 3)
		{
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_LSOBJS, argv[2]);
		}
		else
		{
			strncpy(OutBuf, MEMBUS_CODE_LSOBJS, strlen(MEMBUS_CODE_LSOBJS) + 1);
		}
		
		MemBus_Write(OutBuf, false);
		
		while (!MemBus_BinRead(InBuf, MEMBUS_MSGSIZE, false)) usleep(100);

		if (!strcmp(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_LSOBJS, InBuf))
		{
			puts(argc == 2 ? "No objects found!" : "Specified object not found.");
			ShutdownMemBus(false);
			return FAILURE;
		}
		
		while (strcmp(InBuf, MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_LSOBJS) != 0)
		{
			Bool FoundRL = false;
			unsigned PID = 0;
			Bool Started = false, Running = false, Enabled = false, PivotRoot = false, Persistent = false, Exec = false;
			enum _StopMode StopMode;
			unsigned char TermSignal = 0, ReloadCommandSignal = 0, *BinWorker = NULL;
			unsigned StartedSince, UserID, GroupID, Inc = 0, StopTimeout;
			Bool HaltCmdOnly = false, IsService = false, AutoRestart = false, NoStopWait = false, NoTrack = false;
			Bool ForceShell = false, RawDescription = false, Fork = false, RunOnce = false, ForkScanOnce = false;
			Bool StartFailIsCritical = false, StopFailIsCritical = false, OptNewline = false;
			char RLExpect[MEMBUS_MSGSIZE], ObjectID[MAX_DESCRIPT_SIZE], ObjectDescription[MAX_DESCRIPT_SIZE];
			
			Worker = InBuf + strlen(MEMBUS_CODE_LSOBJS " ");
			
			/*Version matters.*/
			if (strncmp(Worker, MEMBUS_LSOBJS_VERSION, strlen(MEMBUS_LSOBJS_VERSION)) != 0)
			{
				SpitError("LSOBJS protocol version mismatch. Expected \"" MEMBUS_LSOBJS_VERSION "\".");
				
				while (strcmp(InBuf, MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_LSOBJS) != 0)
				{ /*Don't mess up the membus, let it empty.*/
					while (!MemBus_Read(InBuf, false)) usleep(10);
				}
				
				ShutdownMemBus(false);
				return FAILURE;
			}
			
			Worker += strlen(MEMBUS_LSOBJS_VERSION) + 1;
			
			BinWorker = (void*)Worker;
			
			Started = *BinWorker++;
			Running = *BinWorker++;
			Enabled = *BinWorker++;
			TermSignal = *BinWorker++;
			ReloadCommandSignal = *BinWorker++;

			/*Remove this line when we make use of ReloadCommandSignal.*/
			(void)ReloadCommandSignal;
			
			memcpy(&UserID, BinWorker, sizeof(int));
			memcpy(&GroupID, (BinWorker += sizeof(int)), sizeof(int));
			
			memcpy(&StopMode, (BinWorker += sizeof(int)), sizeof(enum _StopMode));
			memcpy(&PID, (BinWorker += sizeof(enum _StopMode)), sizeof(int));
			
			memcpy(&StartedSince, (BinWorker += sizeof(int)), sizeof(int));
			memcpy(&StopTimeout, BinWorker + sizeof(int), sizeof(int));

			while (!MemBus_BinRead(InBuf, MEMBUS_MSGSIZE, false)) usleep(100);
			
			for (Worker = InBuf, Inc = 0; Worker[Inc] != ' '; ++Inc)
			{ /*Get ObjectID*/
				ObjectID[Inc] = Worker[Inc];
			}
			ObjectID[Inc] = '\0';
			
			Worker += Inc + 1;
			
			/*Get ObjectDescription.*/
			strncpy(ObjectDescription, Worker, strlen(Worker) + 1);
			
			/*Retrieve the options.*/
			while (!MemBus_BinRead(InBuf, MEMBUS_MSGSIZE, false)) usleep(100);
			
			for (Worker = InBuf; *Worker != 0; ++Worker)
			{
				if (*(unsigned char*)Worker >= COPT_MAX) continue; /*If we don't understand.*/

				switch (*(unsigned char*)Worker)
				{
					case COPT_HALTONLY:
						HaltCmdOnly = true;
						break;
					case COPT_PERSISTENT:
						Persistent = true;
						break;
					case COPT_FORK:
						Fork = true;
						break;
					case COPT_FORKSCANONCE:
						ForkScanOnce = true;
						break;
					case COPT_SERVICE:
						IsService = true;
						break;
					case COPT_RAWDESCRIPTION:
						RawDescription = true;
						break;
					case COPT_AUTORESTART:
						AutoRestart = true;
						break;
					case COPT_FORCESHELL:
						ForceShell = true;
						break;
					case COPT_NOSTOPWAIT:
						NoStopWait = true;
						break;
					case COPT_NOTRACK:
						NoTrack = true;
						break;
					case COPT_STARTFAILCRITICAL:
						StartFailIsCritical = true;
						break;
					case COPT_STOPFAILCRITICAL:
						StopFailIsCritical = true;
						break;
					case COPT_EXEC:
						Exec = true;
						break;
					case COPT_PIVOTROOT:
						PivotRoot = true;
						break; 
					case COPT_RUNONCE:
						RunOnce = true;
						break;
					default:
						break;
				}
			}
		
		
			printf("ObjectID: %s\nObjectDescription: %s\nEnabled: %s | Started: %s | Running: %s | Stop mode: ",
					ObjectID, ObjectDescription, YN[Enabled],
					HaltCmdOnly || PivotRoot || Exec ? CONSOLE_COLOR_YELLOW "N/A" CONSOLE_ENDCOLOR : YN[Started],
					HaltCmdOnly || PivotRoot || Exec ? CONSOLE_COLOR_YELLOW "N/A" CONSOLE_ENDCOLOR : YN[Running]);
			
			if (StopMode == STOP_COMMAND) printf("Command");
			else if (StopMode == STOP_NONE) printf("None");
			else if (StopMode == STOP_PID) printf("PID");
			else if (StopMode == STOP_PIDFILE) printf("PID File");
			
			if (Running)
			{
				printf(" | PID: %u\n", PID);
			}
			else
			{
				putchar('\n');
			}
			
			if (Started)
			{
				time_t SS = (time_t)StartedSince, CTime = time(NULL);
				struct tm TStruct;
				char TimeBuf[64] = { '\0' };
				unsigned Offset = (CTime - StartedSince) / 60;
				localtime_r(&SS, &TStruct);
				
				asctime_r(&TStruct, TimeBuf);
				
				TimeBuf[strlen(TimeBuf) - 1] = '\0'; /*Nuke newline.*/
				printf("Started since %s, for total of %u mins.\n", TimeBuf, Offset);
			}
			
			if (IsService || AutoRestart || HaltCmdOnly || Persistent || Fork || StopTimeout != 10 || NoTrack ||
				ForceShell || RawDescription || NoStopWait || PivotRoot || RunOnce || TermSignal != SIGTERM || Exec ||
				StartFailIsCritical || StopFailIsCritical)
			{
				printf("Options:");
				
				if (IsService) printf(" SERVICE");
				if (AutoRestart) printf(" AUTORESTART");
				if (HaltCmdOnly) printf(" HALTONLY");
				if (Persistent) printf(" PERSISTENT");
				if (ForceShell) printf(" FORCESHELL");
				if (Fork)
				{
					if (ForkScanOnce) printf(" FORKN");
					else printf(" FORK");
				}
				if (RawDescription) printf(" RAWDESCRIPTION");
				if (TermSignal != SIGTERM) printf(" TERMSIGNAL=%u", TermSignal);
				if (NoStopWait) printf(" NOSTOPWAIT");
				if (PivotRoot) printf(" PIVOT");
				if (Exec) printf(" EXEC");
				if (RunOnce) printf(" RUNONCE");
				if (NoTrack) printf(" NOTRACK");
				if (StartFailIsCritical) printf( "STARTFAILCRITICAL");
				if (StopFailIsCritical) printf( "STOPFAILCRITICAL");
				if (StopTimeout != 10) printf(" STOPTIMEOUT=%u", StopTimeout);
				
				OptNewline = true;
			}

			/*Get exit status mappings.*/
			while (!MemBus_BinRead(InBuf, MEMBUS_MSGSIZE, false)) usleep(100);

			BinWorker = (void*)(InBuf + sizeof MEMBUS_CODE_LSOBJS " MXS");
			Inc = *BinWorker++; /*Get the count.*/
			
			if (Inc > 0) OptNewline = true;
			
			for (; Inc; --Inc)
			{
				const char *Stringy = NULL;
				unsigned char Value = *BinWorker++, ExitStatus = *BinWorker++;
				
				if (Value == SUCCESS) Stringy = "SUCCESS";
				else if (Value == WARNING) Stringy = "WARNING";
				else if (Value == FAILURE) Stringy = "FAILURE";
				else Stringy = "<BAD>";
				
				printf(" MAPEXITSTATUS=%d,%s", ExitStatus, Stringy);
			}
			
			if (OptNewline) putchar('\n');
			
			snprintf(RLExpect, sizeof RLExpect, "%s %s %s", MEMBUS_CODE_LSOBJS, MEMBUS_LSOBJS_VERSION, ObjectID);
			
			/*Done with this, now read runlevels.*/
			while (!MemBus_BinRead(InBuf, MEMBUS_MSGSIZE, false)) usleep(100);
			
			while (!strncmp(InBuf, RLExpect, strlen(RLExpect)))
			{ /*Also causes the next object to be read.*/
				if (!FoundRL && !HaltCmdOnly)
				{
					printf("Runlevels:");
					FoundRL = true;
				}
				
				Worker = InBuf + strlen(MEMBUS_CODE_LSOBJS " " MEMBUS_LSOBJS_VERSION " ") + strlen(ObjectID) + 1;
				
				if (!HaltCmdOnly)
				{
					printf(" %s", Worker);
				}
				
				while (!MemBus_BinRead(InBuf, MEMBUS_MSGSIZE, false)) usleep(100);
			}
			
			if (FoundRL) putchar('\n');
			
			if (UserID || GroupID)
			{
				struct passwd *UserStruct = getpwuid(UserID);
				struct group *GroupStruct = getgrgid(GroupID);
				
				if (UserStruct) printf("User: %s\n", UserStruct->pw_name);
				if (GroupStruct && GroupID != 0) printf("Group: %s\n", GroupStruct->gr_name);
			}
			
			if (argc == 2)
			{
				puts("-------");
			}
		}
		
		ShutdownMemBus(false);
		return SUCCESS;
	}
	else if (ArgIs("runlevel"))
	{
		char InBuf[MEMBUS_MSGSIZE];
		ReturnCode RV = SUCCESS;
		
		if (argc > 3)
		{
			puts("Too many arguments.");
			PrintEpochHelp(argv[0], "runlevel");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			return FAILURE;
		}
		
		if (argc == 2)
		{
			MemBus_Write(MEMBUS_CODE_GETRL, false);
			
			while (!MemBus_Read(InBuf, false)) usleep(1000);
			
			if (!strcmp(MEMBUS_CODE_BADPARAM " " MEMBUS_CODE_GETRL, InBuf))
			{
				SpitError("We are being told that MEMBUS_CODE_GETRL is not valid.\n"
						"This is a bug. Please report to Epoch.");
				RV = FAILURE;
			}
			else if (!strncmp(MEMBUS_CODE_GETRL " ", InBuf, strlen(MEMBUS_CODE_GETRL " ")))
			{
				printf("Current runlevel is \"%s\".\n", InBuf + strlen(MEMBUS_CODE_GETRL " "));
				RV = SUCCESS;
			
			}
		}
		else
		{
			char OutBuf[MEMBUS_MSGSIZE];
			char PossibleResponses[3][MEMBUS_MSGSIZE] = { { '\0' } };
			
			
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_RUNLEVEL, argv[2]);
			
			snprintf(PossibleResponses[0], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, OutBuf);
			snprintf(PossibleResponses[1], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_FAILURE, OutBuf);
			snprintf(PossibleResponses[2], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_BADPARAM, OutBuf);
			
			
			MemBus_Write(OutBuf, false);
			
			while (!MemBus_Read(InBuf, false)) usleep(1000);
			
			if (!strcmp(PossibleResponses[0], InBuf))
			{
				RV = SUCCESS;
			}
			else if (!strcmp(PossibleResponses[1], InBuf))
			{
				RV = FAILURE;
				fprintf(stderr, "Unable to switch to runlevel %s.\n", argv[2]);
			}
			else if (!strcmp(PossibleResponses[2], InBuf))
			{
				RV = FAILURE;
				SpitError("We are being told we sent bad data over the membus.\nThis is a bug. Please report.");
			}
			else
			{
				RV = FAILURE;
				SpitError("We have received a corrupted response over the membus.\nThis is a bug. Please report.");
			}
		}
		
		ShutdownMemBus(false);
		return RV;
	}
	else if (ArgIs("setcad"))
	{
		const char *MCode = NULL, *ReportLump = NULL;
		ReturnCode RetVal = SUCCESS;
		
		if (argc != 3)
		{
			if (argc > 3)
			{
				puts("Too many arguments.\n");
			}
			else
			{
				puts("Too few arguments.\n");
			}
			
			PrintEpochHelp(argv[0], "setcad");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			return FAILURE;
		}
		
		CArg = argv[2];
		
		if (ArgIs("on"))
		{
			MCode = MEMBUS_CODE_CADON;
			ReportLump = "enable";
		}
		else if (ArgIs("off"))
		{
			MCode = MEMBUS_CODE_CADOFF;
			ReportLump = "disable";
		}
		else
		{
			fprintf(stderr, "%s\n", "Bad parameter. Valid values are on and off.");
			return FAILURE;
		}
		
		if (SendPowerControl(MCode))
		{
			printf("Ctrl-Alt-Del instant reboot has been %s%c.\n", ReportLump, 'd');
			RetVal = SUCCESS;
		}
		else
		{
			fprintf(stderr, CONSOLE_COLOR_RED "* " CONSOLE_ENDCOLOR "Failed to %s Ctrl-Alt-Del instant reboot!\n", ReportLump);
			RetVal = FAILURE;
		}
		
		ShutdownMemBus(false);
		return RetVal;
	}
	else if (ArgIs("enable") || ArgIs("disable"))
	{
		ReturnCode RV = SUCCESS;
		Bool Enabling = ArgIs("enable");
		char TOut[MAX_LINE_SIZE];
		unsigned Inc = 2;
		
		if (argc < 3)
		{

			puts("Too few arguments.\n");
			
			PrintEpochHelp(argv[0], "enable");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			
			return FAILURE;
		}
		
		/*Iterate through all specified objects.*/
		for (Inc = 2; Inc < argc; ++Inc)
		{
			snprintf(TOut, sizeof TOut, (Enabling ? "Enabling %s" : "Disabling %s"), argv[Inc]);
			BeginStatusReport(TOut);
			
			RV = ObjControl(argv[Inc], (Enabling ? MEMBUS_CODE_OBJENABLE : MEMBUS_CODE_OBJDISABLE));
			CompleteStatusReport(TOut, RV, false);
		}
		
		ShutdownMemBus(false);
		return RV;
	}
	else if (ArgIs("start") || ArgIs("stop") || ArgIs("restart"))
	{
		ReturnCode RV = SUCCESS;
		short StartMode = 0;
		enum { START = 1, STOP, RESTART };
		char TOut[MAX_LINE_SIZE];
		unsigned Inc = 2;

		if (argc < 3)
		{
			puts("Too few arguments.\n");
			
			PrintEpochHelp(argv[0], "start");
			return FAILURE;
		}

		if (!InitMemBus(false))
		{
			
			return FAILURE;
		}
		
		if (ArgIs("start"))
		{
			StartMode = START;
		}
		else if (ArgIs("stop"))
		{
			StartMode = STOP;
		}
		else
		{
			StartMode = RESTART;
		}
		
		/*Iterate through all provided arguments.*/
		for (Inc = 2; Inc < argc; ++Inc)
		{
			if (StartMode < RESTART)
			{
				const char *ActionString = StartMode == START ? "Starting" : "Stopping";
				
				snprintf(TOut, sizeof TOut, "%s %s", ActionString, argv[Inc]);
				BeginStatusReport(TOut);
				
				RV = ObjControl(argv[Inc], (StartMode == START ? MEMBUS_CODE_OBJSTART : MEMBUS_CODE_OBJSTOP));
				
				CompleteStatusReport(TOut, RV, false);
			}
			else
			{
				snprintf(TOut, sizeof TOut, "Stopping %s", argv[Inc]);
				
				BeginStatusReport(TOut);
				RV = ObjControl(argv[Inc], MEMBUS_CODE_OBJSTOP);
				CompleteStatusReport(TOut, RV, false);
				
				if (!RV)
				{ /*Stop failed so...*/
					continue;
				}
				
				snprintf(TOut, sizeof TOut, "Starting %s", argv[Inc]);
				
				BeginStatusReport(TOut);
				RV = ObjControl(argv[Inc], MEMBUS_CODE_OBJSTART);
				CompleteStatusReport(TOut, RV, false);
			}	
		}
		
		/*Now that we're done, shut down the membus.*/
		ShutdownMemBus(false);
		return SUCCESS;
	}
	else if (ArgIs("reload"))
	{
		ReturnCode RV = SUCCESS;
		char InBuf[MEMBUS_MSGSIZE], OutBuf[MEMBUS_MSGSIZE];
		char PossibleResponses[4][MEMBUS_MSGSIZE];
		char StatusBuf[MAX_LINE_SIZE];
		unsigned Inc = 2;
		
		if (argc < 3)
		{
			puts("Too few arguments.\n");
			
			PrintEpochHelp(argv[0], "reload");
			return FAILURE;
		}
		
		if (!InitMemBus(false))
		{
			return FAILURE;
		}
		
		/*Iterate through all objects they specified.*/
		for (Inc = 2; Inc < argc; ++Inc)
		{
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_OBJRELOAD, argv[Inc]);
			
			snprintf(PossibleResponses[0], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, OutBuf);
			snprintf(PossibleResponses[1], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_WARNING, OutBuf);
			snprintf(PossibleResponses[2], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_FAILURE, OutBuf);
			snprintf(PossibleResponses[3], MEMBUS_MSGSIZE, "%s %s", MEMBUS_CODE_BADPARAM, OutBuf);
			
			snprintf(StatusBuf, MAX_LINE_SIZE, "Reloading %s", argv[Inc]);
			BeginStatusReport(StatusBuf);
			
			MemBus_Write(OutBuf, false);
			
			while (!MemBus_Read(InBuf, false)) usleep(1000);
			
			if (!strcmp(InBuf, PossibleResponses[0]))
			{
				RV = SUCCESS;
			}
			else if (!strcmp(InBuf, PossibleResponses[1]))
			{
				RV = WARNING;
			}
			else if (!strcmp(InBuf, PossibleResponses[2]))
			{
				RV = FAILURE;
			}
			else if (!strcmp(InBuf, PossibleResponses[3]))
			{
				CompleteStatusReport(StatusBuf, (RV = FAILURE), false);
				SpitError("We are being told that we sent a bad parameter over membus.\n"
							"This is probably a bug. Please report to Epoch!");
				ShutdownMemBus(false);
				return FAILURE;
			}
			else
			{
				CompleteStatusReport(StatusBuf, (RV = FAILURE), false);
				SpitError("Bad parameter received over membus! This is probably a bug.\n"
							"Please report to Epoch!");
				ShutdownMemBus(false);
				return FAILURE;
			}
			
			CompleteStatusReport(StatusBuf, RV, false);
		}
		
		ShutdownMemBus(false);
		
		return RV;
	}
	else if (ArgIs("getpid"))
	{
		ReturnCode RV = SUCCESS;
		char InBuf[MEMBUS_MSGSIZE];
		char OutBuf[MEMBUS_MSGSIZE];
		char PossibleResponses[3][MEMBUS_MSGSIZE];
		
		if (argc != 3)
		{
			if (argc > 3)
			{
				puts("Too many arguments.\n");
			}
			else
			{
				puts("Too few arguments.\n");
			}
			
			PrintEpochHelp(argv[0], "getpid");
			return FAILURE;
		}
		
		if (!InitMemBus(false)) return FAILURE;
		
		snprintf(OutBuf, sizeof InBuf, "%s %s", MEMBUS_CODE_SENDPID, argv[2]);
		
		snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s ", MEMBUS_CODE_SENDPID, argv[2]);
		snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s", MEMBUS_CODE_FAILURE, OutBuf);
		snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s", MEMBUS_CODE_BADPARAM, OutBuf);
		
		MemBus_Write(OutBuf, false);
		
		while (!MemBus_Read(InBuf, false)) usleep(1000);
		
		if (!strncmp(InBuf, PossibleResponses[0], strlen(PossibleResponses[0])))
		{
			const char *TextPID = InBuf + strlen(PossibleResponses[0]);
			
			printf("PID for object %s: %s\n", argv[2], TextPID);
		}
		else if (!strcmp(PossibleResponses[1], InBuf))
		{
			fprintf(stderr, CONSOLE_COLOR_RED "Unable to retrieve PID for object %s" CONSOLE_ENDCOLOR "\n", argv[2]);
			RV = FAILURE;
		}
		else if (!strcmp(PossibleResponses[2], InBuf))
		{
			SpitError("We are being told that MEMBUS_CODE_SENDPID is not understood. Please report this to Epoch.");
			RV = FAILURE;
		}
		else
		{
			SpitError("Bad response received over membus. Please report this to Epoch.");
			RV = FAILURE;
		}
		
		ShutdownMemBus(false);
		return RV;
	}
	else if (ArgIs("kill"))
	{
		char InBuf[MEMBUS_MSGSIZE], OutBuf[MEMBUS_MSGSIZE];
		char PossibleResponses[3][MEMBUS_MSGSIZE];
		ReturnCode RV = SUCCESS;
		
		if (argc != 3)
		{
			if (argc > 3)
			{
				puts("Too many arguments.\n");
			}
			else
			{
				puts("Too few arguments.\n");
			}
			
			PrintEpochHelp(argv[0], "kill");
			return FAILURE;
		}
		
		if (!InitMemBus(false)) return FAILURE;
		
		snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_KILLOBJ, argv[2]);
		
		snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s", MEMBUS_CODE_ACKNOWLEDGED, OutBuf);
		snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s", MEMBUS_CODE_FAILURE, OutBuf);
		snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s", MEMBUS_CODE_BADPARAM, OutBuf);
		
		MemBus_Write(OutBuf, false);
		
		while (!MemBus_Read(InBuf, false)) usleep(1000);
		
		if (!strcmp(InBuf, PossibleResponses[0]))
		{
			printf("Object %s successfully killed.\n", argv[2]);
		}
		else if (!strcmp(InBuf, PossibleResponses[1]))
		{
			fprintf(stderr, CONSOLE_COLOR_RED "* " CONSOLE_ENDCOLOR "Unable to kill object %s.\n", argv[2]);
			RV = FAILURE;
		}
		else if (!strcmp(InBuf, PossibleResponses[2]))
		{
			SpitError("We are being told that MEMBUS_CODE_KILLOBJ is not understood.\n"
						"Please report to Epoch.");
			RV = FAILURE;
		}
		else
		{
			SpitError("Bad response received over membus. This is likely a bug, please report to Epoch.");
			RV = FAILURE;
		}
		
		ShutdownMemBus(false);
		
		return RV;
	}
	else if (ArgIs("objrl"))
	{
		const char *ObjectID = argv[2], *RL = argv[4];
		char OBuf[MEMBUS_MSGSIZE];
		char IBuf[MEMBUS_MSGSIZE];
		ReturnCode ExitStatus = SUCCESS;
		
		if (argc != 5)
		{
			if (argc > 5)
			{
				puts("Too many arguments.\n");
			}
			else
			{
				puts("Too few arguments.");
			}
			
			PrintEpochHelp(argv[0], "objrl");
			return FAILURE;
		}

		if (!InitMemBus(false))
		{
			
			return FAILURE;
		}
		
		CArg = argv[3];
		
		if (ArgIs("add"))
		{
			snprintf(OBuf, sizeof OBuf, "%s %s %s", MEMBUS_CODE_OBJRLS_ADD, ObjectID, RL);
		}
		else if (ArgIs("del"))
		{
			snprintf(OBuf, sizeof OBuf, "%s %s %s", MEMBUS_CODE_OBJRLS_DEL, ObjectID, RL);
		}
		else if (ArgIs("check"))
		{
			snprintf(OBuf, sizeof OBuf, "%s %s %s", MEMBUS_CODE_OBJRLS_CHECK, ObjectID, RL);
		}
		else
		{
			fprintf(stderr, CONSOLE_COLOR_RED "* " CONSOLE_ENDCOLOR "Invalid runlevel option %s.\n", CArg);
			ShutdownMemBus(false);
			return FAILURE;
		}
		
		if (!MemBus_Write(OBuf, false))
		{
			SpitError("Failed to write to membus.");
			ShutdownMemBus(false);
			return FAILURE;
		}
		
		while (!MemBus_Read(IBuf, false)) usleep(1000);
		
		if (ArgIs("add") || ArgIs("del"))
		{	
			char PossibleResponses[3][MEMBUS_MSGSIZE];
			
			snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s %s %s", MEMBUS_CODE_ACKNOWLEDGED,
					(ArgIs("add") ? MEMBUS_CODE_OBJRLS_ADD : MEMBUS_CODE_OBJRLS_DEL), ObjectID, RL);
			
			snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s %s %s", MEMBUS_CODE_FAILURE,
					(ArgIs("add") ? MEMBUS_CODE_OBJRLS_ADD : MEMBUS_CODE_OBJRLS_DEL), ObjectID, RL);
					
			snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s", MEMBUS_CODE_BADPARAM, OBuf);
			
			if (!strcmp(PossibleResponses[0], IBuf))
			{
				char *PSFormat[2] = { "Object %s added to runlevel %s\n", "Object %s deleted from runlevel %s\n" };
				printf(PSFormat[(ArgIs("add") ? 0 : 1)], ObjectID, RL);
			}
			else if (!strcmp(PossibleResponses[1], IBuf))
			{
				char *PSFormat[2] = { "Unable to add %s to runlevel %s!\n", "Unable to remove %s from runlevel %s!\n" };
				
				fprintf(stderr, PSFormat[(ArgIs("add") ? 0 : 1)], ObjectID, RL);
				ExitStatus = FAILURE;
			}
			else if (!strcmp(PossibleResponses[2], IBuf))
			{
				SpitError("Internal membus error, received BADPARAM upon your request. Please report to Epoch.");
				ExitStatus = FAILURE;
			}
			else
			{
				SpitError("Received unrecognized or corrupted response via membus! Please report to Epoch.");
				ExitStatus = FAILURE;
			}
			
			ShutdownMemBus(false);
			return ExitStatus;
		}
		else if (ArgIs("check"))
		{
			char PossibleResponses[3][MEMBUS_MSGSIZE];
	
			snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s %s ", MEMBUS_CODE_OBJRLS_CHECK, ObjectID, RL);
			snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s", MEMBUS_CODE_FAILURE, OBuf);
			snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s", MEMBUS_CODE_BADPARAM, OBuf);
		
			if (!strncmp(PossibleResponses[0], IBuf, strlen(PossibleResponses[0])))
			{
				char CNumber[2] = { '\0', '\0' };
				const char *CNS = IBuf + strlen(PossibleResponses[0]);
				
				CNumber[0] = *CNS;
				
				if (*CNumber == '0')
				{
					printf(CONSOLE_COLOR_RED "Object %s is NOT enabled for runlevel %s.\n" CONSOLE_ENDCOLOR,
							ObjectID, RL);
				}
				else if (*CNumber == '1')
				{
					printf(CONSOLE_COLOR_GREEN "Object %s is enabled for runlevel %s.\n" CONSOLE_ENDCOLOR,
							ObjectID, RL);
				}
				else if (*CNumber == '2')
				{
					printf(CONSOLE_COLOR_CYAN "Object %s is inherited by the runlevel %s.\n" CONSOLE_ENDCOLOR,
							ObjectID, RL);
				}
				else
				{
					SpitError("Internal error, bad status number received from membus. Please report to Epoch.");
					ExitStatus = FAILURE;
				}
				ShutdownMemBus(false);
				
				return ExitStatus;
			}
			else if (!strcmp(PossibleResponses[1], IBuf))
			{
				fprintf(stderr, CONSOLE_COLOR_RED "* " CONSOLE_ENDCOLOR 
						"Unable to determine if object %s beints to runlevel %s. Does it exist?\n", ObjectID, RL);
				ShutdownMemBus(false);
				return FAILURE;
			}
			else if (!strcmp(PossibleResponses[2], IBuf))
			{
				SpitError("We are being told that we sent a bad signal over the membus. "
							"This is a bug, please report to epoch.");
				ShutdownMemBus(false);
				return FAILURE;
			}
			else
			{
				SpitError("Received unrecognized or corrupted response via membus! Please report to Epoch.");
				ShutdownMemBus(false);
				return FAILURE;
			}
		}
	}
	else
	{
		fprintf(stderr, "Bad command %s.\n", CArg);
		PrintEpochHelp(argv[0], NULL);
		ShutdownMemBus(false);
		return FAILURE;
	}
	
	return SUCCESS;
}

static void SetDefaultProcessTitle(int argc, char **argv)
{
	unsigned Inc = 1;
	
	for (; Inc < argc; ++Inc)
	{
		memset(argv[Inc], 0, strlen(argv[Inc]));
		argv[Inc] = NULL;
	}
	
	strncpy(argv[0], "init", strlen(argv[0]));
}

#ifndef NOMAINFUNC
int main(int argc, char **argv)
{ /*Lotsa sloppy CLI processing here.*/
	/**Turn off buffering for stdout and stderr.**/
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	/*Set up signal handling.*/
	signal(SIGSEGV, SigHandler);
	signal(SIGILL, SigHandler);
	signal(SIGFPE, SigHandler);
	signal(SIGABRT, SigHandler);
	signal(SIGINT, SigHandler); /*For reboots and closing client membus correctly.*/
	
	if (argv[0] == NULL)
	{
		SpitError("main(): argv[0] is NULL. Why?");
		return 1;
	}
	
	/*Determines if we are init booting up.*/
	if (getpid() == 1 || (argc == 2 && (CmdIs("epoch") || CmdIs("init")) && !strcmp(argv[1], "--init")) ||
		(argc == 2 && !strcmp(argv[0], "!rxd") && !strcmp(argv[1], "REEXEC")))
	{
		if (getuid() != 0)
		{
			fprintf(stderr, "Can't init as non-root.\n");
			_exit(1);
		}
		AreInit = true;
	}
	
	if (AreInit)
	{ /*Just us, as init. That means, begin bootup.*/
		const char *TRunlevel = NULL, *TConfigFile = getenv("epochconfig");

		if (TConfigFile != NULL)
		{ /*Someone specified a config file from disk?*/
			snprintf(ConfigFile, MAX_LINE_SIZE, "%s", TConfigFile);
		} /**We leave this above the check for reexec so the reexecuted version can pull this in.**/
		
		/**Check if we are resuming from a reexec.**/
		if (argc == 2 && !strcmp(argv[0], "!rxd") && !strcmp(argv[1], "REEXEC"))
		{
			const char *RecoverType = getenv("EPOCHRXDMEMBUS");
			
			if (RecoverType) unsetenv("EPOCHRXDMEMBUS");
			
			SetDefaultProcessTitle(argc, argv);
			RecoverFromReexec(RecoverType != NULL);	
		}
		else if (argc > 1)
		{
			short ArgCount = (short)argc, Inc = 1;
			const char *Arguments[] = { "shell" }; /*I'm sick of repeating myself with literals.*/
			
			for (; Inc < ArgCount; ++Inc)
			{
				if (!strcmp(argv[Inc], Arguments[0]))
				{
					puts(CONSOLE_COLOR_GREEN "Now launching a simple shell as per your request." CONSOLE_ENDCOLOR);
					EmergencyShell(); /*Drop everything we're doing and start an emergency shell.*/
				}
			}
		}
		
		signal(SIGUSR2, SigHandler); /**If we receive this, we reexecute. Mostly in case something is wrong.**/
		
		/*Need we set a default runlevel?*/
		if ((TRunlevel = getenv("runlevel")) != NULL)
		{ /*Sets the default runlevel we use on bootup.*/
			snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", TRunlevel);
		}
		
		SetDefaultProcessTitle(argc, argv);
		
		/*Now that args are set, boot.*/		
		LaunchBootup();
	}
	
	/**Beyond here we check for argv[0] being one thing or the other.**/
	else if (CmdIs("poweroff") || CmdIs("reboot") || CmdIs("halt"))
	{
		Bool RVal;
		
		/*Start membus.*/
		if (argc == 1 && !InitMemBus(false))
		{ /*Don't initialize the membus if we could be doing "-f".*/
			
			return 1;
		}
		
		RVal = !ProcessGenericHalt(argc, argv);
		
		ShutdownMemBus(false);
		
		return (int)RVal;
	}
	else if (CmdIs("epoch")) /*Our main management program.*/
	{	
		return !HandleEpochCommand(argc, argv);
	}
	else if (CmdIs("init"))
	{ /*This is a bit int winded here, however, it's better than devoting a function for it.*/
		if (argc == 2)
		{
			char TmpBuf[MEMBUS_MSGSIZE];
			char MembusResponse[MEMBUS_MSGSIZE];
			char PossibleResponses[3][MEMBUS_MSGSIZE];
			
			if (strlen(argv[1]) >= MEMBUS_MSGSIZE)
			{
				SpitError("Runlevel name too int. Please specify a runlevel with a sane name.");
				return 1;
			}
			
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_RUNLEVEL, argv[1]);
			snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s", MEMBUS_CODE_ACKNOWLEDGED, TmpBuf);
			snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s", MEMBUS_CODE_FAILURE, TmpBuf);
			snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s", MEMBUS_CODE_BADPARAM, TmpBuf);
			
			if (!InitMemBus(false))
			{
				SpitError("Failed to communicate with Epoch init, membus is down.");
				return 1;
			}
			
			if (!MemBus_Write(TmpBuf, false))
			{				
				SpitError("Failed to change runlevels, failed to write to membus after establishing connection.\n"
							"Is Epoch the running boot system?");		
				ShutdownMemBus(false);
				
				return 1;
			}
			
			while (!MemBus_Read(MembusResponse, false)) usleep(1000);
			
			if (!strcmp(MembusResponse, PossibleResponses[0]))
			{
				ShutdownMemBus(false);
				
				return 0;
			}
			else if (!strcmp(MembusResponse, PossibleResponses[1]))
			{
				fprintf(stderr, CONSOLE_COLOR_RED "* " CONSOLE_ENDCOLOR "Failed to change runlevel to \"%s\".\n", argv[1]);
				ShutdownMemBus(false);
				
				return 1;
			}
			else if (!strcmp(MembusResponse, PossibleResponses[2]))
			{
				ShutdownMemBus(false);
				SpitError("We are being told that MEMBUS_CODE_RUNLEVEL is not understood.\n"
						"This is bad. Please report to Epoch.");
				
				return 1;
			}
			else
			{
				SpitError("Invalid response provided over membus.");
				ShutdownMemBus(false);
				
				return 1;
			}
		}
		else
		{
			SmallError("Too many arguments. Specify one argument to set the runlevel.");
			return 1;
		}
	}
	else if (CmdIs("killall5"))
	{
		const char *CArg = argv[1];
		
		if (argc == 2)
		{
			if (*CArg == '-')
			{
				++CArg;
			}
			
			if (AllNumeric(CArg))
			{
				return !EmulKillall5(atoi(CArg));
			}
			else
			{
				SmallError("Bad signal number. Please specify an integer signal number.\n"
							"Pass no arguments to assume signal 15.");
				
				return 1;
			}
		}
		else if (argc == 1)
		{
			return !EmulKillall5(SIGTERM);
		}
		else
		{
			SmallError("Too many arguments. Syntax is killall5 -signum where signum\n"
						"is the integer signal number to send.");
			return 1;
		}
		
	}
	else if (CmdIs("wall"))
	{
		if (argc == 2)
		{
			EmulWall(argv[1], true);
			return 0;
		}
		else if (argc == 3 && !strcmp(argv[1], "-n"))
		{
			EmulWall(argv[2], false);
			return 0;
		}
		else
		{
			puts("Usage: wall [-n] message");
			return 1;
		}
	}
	else if (CmdIs("shutdown"))
	{
		return !EmulShutdown(argc, (const char**)argv);

	}
	else
	{
		SmallError("Unrecognized applet name.");
		return 1;
	}
	
	return 0;
}
#endif
