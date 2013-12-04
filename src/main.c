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
#include <execinfo.h>
#include <signal.h>
#include <sys/reboot.h>
#include <sys/shm.h>
#include "epoch.h"

#define ArgIs(z) !strcmp(CArg, z)
#define CmdIs(z) __CmdIs(argv[0], z)

/*Forward declarations for static functions.*/
static rStatus ProcessGenericHalt(int argc, char **argv);
static Bool __CmdIs(const char *CArg, const char *InCmd);
static void PrintEpochHelp(const char *RootCommand, const char *InCmd);
static rStatus HandleEpochCommand(int argc, char **argv);
static void SigHandler(int Signal);
static void SetDefaultProcessTitle(int argc, char **argv);

/*
 * Actual functions.
 */
 
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
	void *BTList[25];
	char **BTStrings;
	size_t BTSize;
	char OutMsg[MAX_LINE_SIZE * 2] = { '\0' }, *TWorker = OutMsg;
	
	switch (Signal)
	{
		case SIGINT:
		{
			static unsigned long LastKillAttempt = 0;
			
			if (getpid() == 1)
			{
				if (CurrentTask.Set && CurrentBootMode != BOOT_NEUTRAL
					&& (LastKillAttempt == 0 || CurrentBootMode == BOOT_SHUTDOWN || time(NULL) > (LastKillAttempt + 5)))
				{
					char MsgBuf[MAX_LINE_SIZE];
					rStatus KilledOK = SUCCESS;
					
					snprintf(MsgBuf, sizeof MsgBuf, 
							"\n%sKilling task %s. %s",
							CONSOLE_COLOR_YELLOW, CurrentTask.TaskName, CONSOLE_ENDCOLOR);

					if (CurrentBootMode == BOOT_BOOTUP)
					{
						strncat(MsgBuf, "Press CTRL-ALT-DEL within 5 seconds to reboot.", MAX_LINE_SIZE - strlen(MsgBuf) - 1);
					}
					
					puts(MsgBuf);
					fflush(NULL);
					
					WriteLogLine(MsgBuf, true);
					
					if (CurrentTask.PID == 0)
					{
						unsigned long *TPtr = (void*)CurrentTask.Node;
						
						*TPtr = 100001;
						
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
					fflush(stdout);
					
					WriteLogLine(MsgBuf, true);
					
					LastKillAttempt = time(NULL);
					return;
				}
				else
				{
					LaunchShutdown(OSCTL_LINUX_REBOOT);
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
		
	}
	
	BTSize = backtrace(BTList, 25);
	BTStrings = backtrace_symbols(BTList, BTSize);

	snprintf(OutMsg, sizeof OutMsg, "%s\n\nBacktrace:\n", ErrorM);
	TWorker += strlen(TWorker);
	
	for (; BTSize > 0 && *BTStrings != NULL; --BTSize, ++BTStrings, TWorker += strlen(TWorker))
	{
		snprintf(TWorker, sizeof OutMsg - strlen(OutMsg) - 1, "\n%s", *BTStrings);
	}
	
	if (getpid() == 1)
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
		("[poweroff/halt/reboot]:\n-----\n"
		
		 "Enter poweroff, halt, or reboot to do the obvious."
		),
		
		( "[disable/enable] objectid:\n-----\n"
		  "Enter disable or enable followed by an object ID to disable or enable\nthat object."
		),
		
		( "[start/stop/restart] objectid:\n-----\n"
		  "Enter start, stop, or restart followed by an object ID to control that object."
		),
		
		( "objrl objectid [del/add/check] runlevel:\n-----\n"
		
		  "runlevel del and add do pretty much what it sounds like,\n"
		  "and check will tell you if that object is enabled for that runlevel."
		),
		  
		( "status objectid:\n-----\n"
		
		  "Enter status followed by an object ID to see if that object\nis currently started."
		),
		
		( "setcad [on/off]:\n-----\n"
		
		  "Sets Ctrl-Alt-Del instant reboot modes. If set to on, striking Ctrl-Alt-Del\n"
		  "at a console will instantly reboot the system without intervention by Epoch.\n"
		  "Otherwise, if set to off, Epoch will perform a normal reboot when Ctrl-Alt-Del\n"
		  "is pressed."
		),
			
		
		( "configreload:\n-----\n"
		
		  "Enter configreload to reload the configuration file epoch.conf.\nThis is useful for "
		  "when you change epoch.conf\n"
		  "to add or remove services, change runlevels, and more."
		),
		
		( "reexec:\n-----\n"
		
		  "Enter reeexec to partially restart Epoch from disk.\n"
		  "This is necessary for updating the Epoch binary to prevent\n"
		  "a failure with unmounting the filesystem the binary is on."
		),
		
		( "currentrunlevel:\n-----\n"
		
		  "Enter currentrunlevel to print the system's current runlevel."
		),
		
		( "getpid objectid:\n-----\n"
		
		  "Retrieves the PID Epoch has on record for the given object. If a PID file is specified,\n"
		  "then the PID will be gotten from there."
		),
		
		( "kill objectid:\n-----\n"
		
		  "Sends SIGKILL to the object specified. If a PID file is specified, the PID\n"
		  "will be retrieved from that."
		),
		
		( "version:\n-----\n"
		
		  "Prints the current version of the Epoch Init System."
		)
	};
	
	enum { HCMD, ENDIS, STAP, OBJRL, STATUS, SETCAD, CONFRL, REEXEC, CURRL, GETPID, KILLOBJ, VER, ENUM_MAX };
	
	
	printf("%s\nCompiled %s %s\n\n", VERSIONSTRING, __DATE__, __TIME__);
	
	if (InCmd == NULL)
	{
		short Inc = 0;
		
		puts(CONSOLE_COLOR_RED "Printing all help.\n" CONSOLE_ENDCOLOR "-----\n");
		
		for (; Inc < ENUM_MAX; ++Inc)
		{
			printf("%s %s\n\n", RootCommand, HelpMsgs[Inc]);
		}
	}
	else if (!strcmp(InCmd, "poweroff") || !strcmp(InCmd, "halt") || !strcmp(InCmd, "reboot"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[HCMD]);
		return;
	}
	else if (!strcmp(InCmd, "disable") || !strcmp(InCmd, "enable"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[ENDIS]);
		return;
	}
	else if (!strcmp(InCmd, "start") || !strcmp(InCmd, "stop") || !strcmp(InCmd, "restart"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[STAP]);
		return;
	}
	else if (!strcmp(InCmd, "objrl"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[OBJRL]);
		return;
	}
	else if (!strcmp(InCmd, "status"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[STATUS]);
		return;
	}
	else if (!strcmp(InCmd, "setcad"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[SETCAD]);
		return;
	}
	else if (!strcmp(InCmd, "configreload"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[CONFRL]);
		return;
	}
	else if (!strcmp(InCmd, "reexec"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[REEXEC]);
		return;
	}
	else if (!strcmp(InCmd, "currentrunlevel"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[CURRL]);
		return;
	}
	else if (!strcmp(InCmd, "getpid"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[GETPID]);
		return;
	}
	else if (!strcmp(InCmd, "kill"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[KILLOBJ]);
		return;
	}
	else if (!strcmp(InCmd, "version"))
	{
		printf("%s %s\n\n", RootCommand, HelpMsgs[VER]);
		return;
	}
	else
	{
		fprintf(stderr, "Unknown command name, \"%s\".\n", InCmd);
		return;
	}
}

static rStatus ProcessGenericHalt(int argc, char **argv)
{
	const char *CArg = argv[1];
	signed long OSCode = -1;
	
	/*Figure out what we are.*/
	if (CmdIs("poweroff") || CmdIs("halt") || CmdIs("reboot"))
	{
		char *GCode = NULL, *SuccessMsg = NULL, *FailMsg[2] = { NULL, NULL };
		
		if (CmdIs("poweroff"))
		{
			GCode = MEMBUS_CODE_POWEROFF;
			OSCode = OSCTL_LINUX_POWEROFF;
			SuccessMsg = "Power off in progress.";
			FailMsg[0] = "Failed to request immediate poweroff.";
			FailMsg[1] = "Failed to request poweroff.";
		}
		else if (CmdIs("reboot"))
		{
			GCode = MEMBUS_CODE_REBOOT;
			OSCode = OSCTL_LINUX_REBOOT;
			SuccessMsg = "Reboot in progress.";
			FailMsg[0] = "Failed to request immediate reboot.";
			FailMsg[1] = "Failed to request reboot.";
		}
		else if (CmdIs("halt"))
		{
			GCode = MEMBUS_CODE_HALT;
			OSCode = OSCTL_LINUX_HALT;
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
				fflush(NULL);
			}
			
		}
	}
	return SUCCESS;
}

static rStatus HandleEpochCommand(int argc, char **argv)
{
	const char *CArg = argv[1];
	
	/*Help parser and shutdown commands (for possible -f).*/
	if (argc >= 2)
	{
		if (ArgIs("help"))
		{
			if ((CArg = argv[2]))
			{
				PrintEpochHelp(argv[0], CArg);
			}
			else
			{
				PrintEpochHelp(argv[0], NULL);
			}
		
			return SUCCESS;
		}
		else if (ArgIs("--version") || ArgIs("version") || ArgIs("-v"))
		{
			printf("%s\nCompiled %s %s\n", VERSIONSTRING, __DATE__, __TIME__);
			return SUCCESS;
		}
		else if (ArgIs("poweroff") || ArgIs("reboot") || ArgIs("halt"))
		{
			Bool RVal;
			
			if (!InitMemBus(false))
			{
				
				return FAILURE;
			}
			
			RVal = !ProcessGenericHalt(argc - 1, argv + 1);
			
			ShutdownMemBus(false);
			return (int)RVal;
		}
		else if (ArgIs("reexec"))
		{
			if (!InitMemBus(false))
			{
				return FAILURE;
			}
			
			MemBus_Write(MEMBUS_CODE_RXD, false); /*We don't shut down the MemBus here. No need.*/
			shmdt((void*)MemData);
			puts("Re-executing Epoch.");
			return SUCCESS;
		}
	}
	
	
	if (argc == 1)
	{
		PrintEpochHelp(argv[0], NULL);
		return SUCCESS;
	}
	/*One argument?*/
	else if (argc == 2)
	{
		CArg = argv[1];
		
		if (ArgIs("configreload"))
		{
			char TRecv[MEMBUS_SIZE/2 - 1];
			char TBuf[3][MAX_LINE_SIZE];

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
		else if (ArgIs("currentrunlevel"))
		{
			rStatus RV = SUCCESS;
			char InBuf[MEMBUS_SIZE/2 - 1];
			
			if (!InitMemBus(false))
			{
				
				return FAILURE;
			}
			
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
			
			ShutdownMemBus(false);
			return RV;
		}
		
		else
		{
			fprintf(stderr, "Bad command %s.\n", CArg);
			PrintEpochHelp(argv[0], NULL);
			return FAILURE;
		}
	}
	else if (argc == 3)
	{
		CArg = argv[1];
		
		if (ArgIs("setcad"))
		{
			const char *MCode = NULL, *ReportLump = NULL;
			rStatus RetVal = SUCCESS;
			
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
		if (ArgIs("enable") || ArgIs("disable"))
		{
			rStatus RV = SUCCESS;
			Bool Enabling = ArgIs("enable");
			char TOut[MAX_LINE_SIZE];
			
			if (!InitMemBus(false))
			{
				
				return FAILURE;
			}
			
			CArg = argv[2];
			snprintf(TOut, sizeof TOut, (Enabling ? "Enabling %s" : "Disabling %s"), CArg);
			printf("%s", TOut);
			fflush(NULL);
			
			RV = ObjControl(CArg, (Enabling ? MEMBUS_CODE_OBJENABLE : MEMBUS_CODE_OBJDISABLE));
			PerformStatusReport(TOut, RV, false);
			
			ShutdownMemBus(false);
			return RV;
		}
		else if (ArgIs("start") || ArgIs("stop") || ArgIs("restart"))
		{
			rStatus RV = SUCCESS;
			short StartMode = 0;
			enum { START = 1, STOP, RESTART };
			char TOut[MAX_LINE_SIZE];
			const char *ActionString = NULL;

			if (!InitMemBus(false))
			{
				
				return FAILURE;
			}
			
			if (ArgIs("start"))
			{
				ActionString = "Starting";
				StartMode = START;
			}
			else if (ArgIs("stop"))
			{
				ActionString = "Stopping";
				StartMode = STOP;
			}
			else
			{
				ActionString = "Restarting";
				StartMode = RESTART;
			}
			
			snprintf(TOut, sizeof TOut, "%s %s", ActionString, argv[2]);
			printf("%s", TOut);
			fflush(NULL);
			
			if (StartMode < RESTART)
			{
				RV = ObjControl(argv[2], (StartMode == START ? MEMBUS_CODE_OBJSTART : MEMBUS_CODE_OBJSTOP));
			}
			else
			{
				RV = (ObjControl(argv[2], MEMBUS_CODE_OBJSTOP) && ObjControl(argv[2], MEMBUS_CODE_OBJSTART));
			}
			
			PerformStatusReport(TOut, RV, false);
			
			ShutdownMemBus(false);
			return RV;
		}
		else if (ArgIs("reload"))
		{
			rStatus RV = SUCCESS;
			char InBuf[MEMBUS_SIZE/2 - 1], OutBuf[MEMBUS_SIZE/2 - 1];
			char PossibleResponses[4][MEMBUS_SIZE/2 - 1];
			char StatusBuf[MAX_LINE_SIZE];
			Bool Botched = false;
			
			if (!InitMemBus(false))
			{
				return FAILURE;
			}
			
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_OBJRELOAD, argv[2]);
			
			snprintf(PossibleResponses[0], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, OutBuf);
			snprintf(PossibleResponses[1], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_WARNING, OutBuf);
			snprintf(PossibleResponses[2], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_FAILURE, OutBuf);
			snprintf(PossibleResponses[3], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_BADPARAM, OutBuf);
			
			snprintf(StatusBuf, MAX_LINE_SIZE, "Reloading %s", argv[2]);
			printf("%s", StatusBuf);
			
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
				PerformStatusReport(StatusBuf, (RV = FAILURE), false);
				SpitError("We are being told that we sent a bad parameter over membus.\n"
							"This is probably a bug. Please report to Epoch!");
				Botched = true;
			}
			else
			{
				PerformStatusReport(StatusBuf, (RV = FAILURE), false);
				SpitError("Bad parameter received over membus! This is probably a bug.\n"
							"Please report to Epoch!");
				Botched = true;
			}
			
			if (!Botched)
			{
				PerformStatusReport(StatusBuf, RV, false);
			}
			
			ShutdownMemBus(false);
			
			return RV;
		}
		else if (ArgIs("getpid"))
		{
			rStatus RV = SUCCESS;
			char InBuf[MEMBUS_SIZE/2 - 1];
			char OutBuf[MEMBUS_SIZE/2 - 1];
			char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
			
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
			char InBuf[MEMBUS_SIZE/2 - 1], OutBuf[MEMBUS_SIZE/2 - 1];
			char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
			rStatus RV = SUCCESS;
			
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
		else if (ArgIs("status"))
		{
			Bool Started, Running, Enabled;
			Trinity InVal;
			
			if (!InitMemBus(false))
			{
				
				return FAILURE;
			}
			
			CArg = argv[2];
			
			InVal = AskObjectStatus(CArg);
			
			if (!InVal.Flag)
			{
				printf("Unable to retrieve status of object %s. Does it exist?\n", CArg);
				ShutdownMemBus(false);
				return SUCCESS;
			}
			else if (InVal.Flag == -1)
			{
				SpitError("HandleEpochCommand(): Internal error retrieving status via membus.");
				ShutdownMemBus(false);
				return FAILURE;
			}
			else
			{
				Started = InVal.Val1;
				Running = InVal.Val2;
				Enabled = InVal.Val3;
			}
			
			printf("Status for object %s:\n---\nEnabled on boot: %s\nStarted: %s\nRunning: %s\n", CArg, /*This bit is kinda weird I think, but the output is pretty.*/
					(Enabled ? CONSOLE_COLOR_GREEN "Yes" CONSOLE_ENDCOLOR : CONSOLE_COLOR_RED "No" CONSOLE_ENDCOLOR),
					(Started ? CONSOLE_COLOR_GREEN "Yes" CONSOLE_ENDCOLOR : CONSOLE_COLOR_RED "No" CONSOLE_ENDCOLOR),
					(Running ? CONSOLE_COLOR_GREEN "Yes" CONSOLE_ENDCOLOR  : CONSOLE_COLOR_RED "No" CONSOLE_ENDCOLOR));

			ShutdownMemBus(false);
			return SUCCESS;
		}
		else
		{
			fprintf(stderr, "Bad command %s.\n", argv[1]);
			PrintEpochHelp(argv[0], NULL);
			return FAILURE;
		}
	}
	else if (argc == 5)
	{
		if (ArgIs("objrl"))
		{
			const char *ObjectID = argv[2], *RL = argv[4];
			char OBuf[MEMBUS_SIZE/2 - 1];
			char IBuf[MEMBUS_SIZE/2 - 1];
			rStatus ExitStatus = SUCCESS;
			
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
				char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
				
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
				char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
		
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
							"Unable to determine if object %s belongs to runlevel %s. Does it exist?\n", ObjectID, RL);
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
	}
	else
	{
		fprintf(stderr, "%s\n", "Invalid usage.");
		PrintEpochHelp(argv[0], NULL);
		return FAILURE;
	}
	
	return SUCCESS;
}

static void SetDefaultProcessTitle(int argc, char **argv)
{
	unsigned long Inc = 1;
	
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
	/*Figure out what we are.*/

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
	
	if (getpid() == 1)
	{ /*Just us, as init. That means, begin bootup.*/
		const char *TRunlevel = NULL;
		
		/**Check if we are resuming from a reexec.**/
		if (argc == 2 && !strcmp(argv[0], "!rxd") && !strcmp(argv[1], "REEXEC"))
		{
			SetDefaultProcessTitle(argc, argv);
			RecoverFromReexec();	
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
	{ /*This is a bit long winded here, however, it's better than devoting a function for it.*/
		if (argc == 2)
		{
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			char MembusResponse[MEMBUS_SIZE/2 - 1];
			char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
			
			if (strlen(argv[1]) >= (MEMBUS_SIZE/2 - 1))
			{
				SpitError("Runlevel name too long. Please specify a runlevel with a sane name.");
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
				SmallError(
						"Bad signal number. Please specify an integer signal number.\n"
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
