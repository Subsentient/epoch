/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**CLI parsing, etc. main() is here.**/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/reboot.h>
#include "epoch.h"

#define ArgIs(z) !strcmp(CArg, z)
#define CmdIs(z) __CmdIs(argv[0], z)

/*Forward declarations for static functions.*/
static rStatus ProcessGenericHalt(int argc, char **argv);
static Bool __CmdIs(const char *CArg, const char *InCmd);
static void PrintEpochHelp(const char *RootCommand, const char *InCmd);
static rStatus HandleEpochCommand(int argc, char **argv);
static void SigHandlerForInit(int Signal);
static void SigHandler(int Signal);

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

static void SigHandlerForInit(int Signal)
{
	const char *WallError = NULL;
	void *BTList[25];
	char **BTStrings;
	size_t BTSize;
	char OutMsg[MAX_LINE_SIZE * 2] = { '\0' }, *TWorker = OutMsg;
	
	switch (Signal)
	{
		case SIGINT:
		{ /*Is this a reboot signal?*/
			EmulWall("System is going down for reboot NOW!", false);
			LaunchShutdown(OSCTL_LINUX_REBOOT);
			return;
		}
		case SIGSEGV:
		{
			WallError = "A segmentation fault has occurred in Epoch! Dropping to emergency shell!";
			break;
		}
		case SIGILL:
		{
			WallError = "Epoch has encountered an illegal instruction! Dropping to emergency shell!";
			break;
		}
		case SIGFPE:
		{
			WallError = "Epoch has encountered an arithmetic error! Dropping to emergency shell!";
			break;
		}
		case SIGABRT:
		{
			WallError = "Epoch has received an abort signal! Dropping to emergency shell!";
			break;
		}
		
	}
	
	BTSize = backtrace(BTList, 25);
	BTStrings = backtrace_symbols(BTList, BTSize);

	snprintf(OutMsg, sizeof OutMsg, "%s\n\nBacktrace:\n", WallError);
	
	TWorker += strlen(TWorker);
	for (; BTSize > 0 && *BTStrings != NULL; --BTSize, ++BTStrings, TWorker += strlen(TWorker))
	{
		snprintf(TWorker, sizeof OutMsg - strlen(OutMsg) - 1, "\n%s", *BTStrings);
	}
	
	EmulWall(OutMsg, false);
	
	EmergencyShell();
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
			puts("SIGINT received. Exiting.");
			ShutdownMemBus(false);
			exit(0);
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
	
	SpitError(OutMsg);
	
	exit(1);
}
	
static void PrintEpochHelp(const char *RootCommand, const char *InCmd)
{ /*Used for help for the epoch command.*/
	const char *HelpMsgs[] =
	{ 
		("[poweroff/halt/reboot]:\n\n"
		
		 "Enter poweroff, halt, or reboot to do the obvious."
		),
		
		( "[disable/enable] objectid:\n\n"
		  "Enter disable or enable followed by an object ID to disable or enable\nthat object."
		),
		
		( "[start/stop] objectid:\n\n"
		  "Enter start or stop followed by an object ID to start or stop that object."
		),
		
		( "objrl objectid [del/add/check] runlevel:\n\n"
		
		  "runlevel del and add do pretty much what it sounds like,\n"
		  "and check will tell you if that object is enabled for that runlevel."
		),
		  
		( "status objectid:\n\n"
		
		  "Enter status followed by an object ID to see if that object\nis currently started."
		),
		
		( "setcad [on/off]:\n\n"
		
		  "Sets Ctrl-Alt-Del instant reboot modes. If set to on, striking Ctrl-Alt-Del\n"
		  "at a console will instantly reboot the system without intervention by Epoch.\n"
		  "Otherwise, if set to off, Epoch will perform a normal reboot when Ctrl-Alt-Del\n"
		  "is pressed."
		),
			
		
		( "configreload:\n\n"
		
		  "Enter configreload to reload the configuration file epoch.conf.\nThis is useful for "
		  "when you change epoch.conf\n"
		  "to add or remove services, change runlevels, and more."
		)
	};
	
	enum { HCMD, ENDIS, STAP, OBJRL, STATUS, SETCAD, CONFRL };
	
	
	printf("%s\n\n", VERSIONSTRING);
	
	if (InCmd == NULL)
	{
		short Inc = 0;
		
		puts(CONSOLE_COLOR_RED "Printing all help.\n" CONSOLE_ENDCOLOR "----\n");
		
		for (; Inc <= CONFRL; ++Inc)
		{
			printf("%s %s\n%s----%s\n", RootCommand, HelpMsgs[Inc], CONSOLE_COLOR_RED, CONSOLE_ENDCOLOR);
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
	else if (!strcmp(InCmd, "start") || !strcmp(InCmd, "stop"))
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
				SpitError("Bad argument(s).");
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
	
	/*Help parser.*/
	if (argc >= 2 && ArgIs("help"))
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
	
	
	if (argc == 1)
	{
		PrintEpochHelp(argv[0], NULL);
		return SUCCESS;
	}
	/*One argument?*/
	else if (argc == 2)
	{
		CArg = argv[1];
		
		if (ArgIs("poweroff") || ArgIs("reboot") || ArgIs("halt"))
		{
			Bool RVal;
			
			if (!InitMemBus(false))
			{
				SpitError("main(): Failed to connect to membus.");
				return FAILURE;
			}
			
			RVal = !ProcessGenericHalt(argc, argv);
			
			ShutdownMemBus(false);
			return (int)RVal;
		}
		else if (ArgIs("configreload"))
		{
			char TRecv[MEMBUS_SIZE/2 - 1];
			char TBuf[3][MAX_LINE_SIZE];

			if (!InitMemBus(false))
			{
				SpitError("main(): Failed to connect to membus.");
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
				SpitError("HandleEpochCommand(): Failed to connect to membus.");
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
				printf("Ctrl-Alt-Del instant reboot has been %sd\n.", ReportLump);
				RetVal = SUCCESS;
			}
			else
			{
				fprintf(stderr, CONSOLE_COLOR_RED "Failed to %s Ctrl-Alt-Del instant reboot!\n" CONSOLE_ENDCOLOR, ReportLump);
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
				SpitError("main(): Failed to connect to membus.");
				return FAILURE;
			}
			
			CArg = argv[2];
			snprintf(TOut, sizeof TOut, (Enabling ? "Enabling %s" : "Disabling %s"), CArg);
			printf("%s", TOut);
			fflush(NULL);
			
			RV = ObjControl(CArg, (Enabling ? MEMBUS_CODE_OBJENABLE : MEMBUS_CODE_OBJDISABLE));
			PrintStatusReport(TOut, RV);
			
			ShutdownMemBus(false);
			return !RV;
		}
		else if (ArgIs("start") || ArgIs("stop"))
		{
			rStatus RV = SUCCESS;
			Bool Starting = ArgIs("start");
			char TOut[MAX_LINE_SIZE];
			
			if (!InitMemBus(false))
			{
				SpitError("main(): Failed to connect to membus.");
				return FAILURE;
			}
			
			CArg = argv[2];
			snprintf(TOut, sizeof TOut, (Starting ? "Starting %s" : "Stopping %s"), CArg);
			printf("%s", TOut);
			fflush(NULL);
			
			RV = ObjControl(CArg, (Starting ? MEMBUS_CODE_OBJSTART : MEMBUS_CODE_OBJSTOP));
			PrintStatusReport(TOut, RV);
			
			ShutdownMemBus(false);
			return !RV;
		}
		else if (ArgIs("status"))
		{
			if (!InitMemBus(false))
			{
				SpitError("main(): Failed to connect to membus.");
				return FAILURE;
			}
			
			CArg = argv[2];
			
			if (AskObjectStarted(CArg))
			{
				printf("%s is currently running.\n", CArg);
			}
			else
			{
				printf("%s is stopped.\n", CArg);
			}
			
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
			
			if (!InitMemBus(false))
			{
				SpitError("main(): Failed to connect to membus.");
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
				fprintf(stderr, "Invalid runlevel option %s.\n", CArg);
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
				char PossibleResponses[3][MAX_LINE_SIZE];
				
				snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s %s %s", MEMBUS_CODE_ACKNOWLEDGED,
						(ArgIs("add") ? MEMBUS_CODE_OBJRLS_ADD : MEMBUS_CODE_OBJRLS_DEL), ObjectID, RL);
				
				snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s %s %s", MEMBUS_CODE_FAILURE,
						(ArgIs("add") ? MEMBUS_CODE_OBJRLS_ADD : MEMBUS_CODE_OBJRLS_DEL), ObjectID, RL);
						
				snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s", MEMBUS_CODE_ACKNOWLEDGED, OBuf);
				
				if (!strcmp(PossibleResponses[0], IBuf))
				{
					char *PSFormat[2] = { "Object %s added to runlevel %s\n", "Object %s deleted from runlevel %s\n" };
					printf(PSFormat[(ArgIs("add") ? 0 : 1)], ObjectID, RL);
					ShutdownMemBus(false);
					return SUCCESS;
				}
				else if (!strcmp(PossibleResponses[1], IBuf))
				{
					char *PSFormat[2] = { "Unable to add %s to runlevel %s!\n", "Unable to remove %s from runlevel %s!\n" };
					
					fprintf(stderr, PSFormat[(ArgIs("add") ? 0 : 1)], ObjectID, RL);
					ShutdownMemBus(false);
					
					return FAILURE;
				}
				else if (!strcmp(PossibleResponses[2], IBuf))
				{
					SpitError("Internal membus error, received BADPARAM upon your request. Please report to Epoch.");
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

#ifndef NOMAINFUNC
int main(int argc, char **argv)
{ /*Lotsa sloppy CLI processing here.*/
	/*Figure out what we are.*/
	void (*SigHPtr)(int Signal);

	if (getpid() == 1)
	{
		SigHPtr = &SigHandlerForInit;
	}
	else
	{
		SigHPtr = &SigHandler;
	}

	/*Set up signal handling.*/
	signal(SIGSEGV, SigHPtr);
	signal(SIGILL, SigHPtr);
	signal(SIGFPE, SigHPtr);
	signal(SIGABRT, SigHPtr);
	signal(SIGINT, SigHPtr); /*For reboots and closing client membus correctly.*/
	
	if (argv[0] == NULL)
	{
		SpitError("main(): argv[0] is NULL. Why?");
		return 1;
	}
	
	if (CmdIs("poweroff") || CmdIs("reboot") || CmdIs("halt"))
	{
		Bool RVal;
		
		/*Start membus.*/
		if (argc == 1 && !InitMemBus(false))
		{ /*Don't initialize the membus if we could be doing "-f".*/
			SpitError("main(): Failed to connect to membus.");
			return 1;
		}
		
		RVal = !ProcessGenericHalt(argc, argv);
		
		ShutdownMemBus(false);
		
		return (int)RVal;
	}
	else if (CmdIs("epoch")) /*Our main management program.*/
	{	
		HandleEpochCommand(argc, argv);
	}
	else if (CmdIs("init"))
	{ /*This is a bit long winded here, however, it's better than devoting a function for it.*/
		if (argc == 1)
		{ /*Just us, as init. That means, begin bootup.*/
			if (getpid() == 1)
			{
				LaunchBootup();
			}
			else
			{
				SpitError("Refusing to launch the whole boot sequence if not PID 1.");
				return 1;
			}
		}
		else if (argc == 2)
		{
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			char StatusReport[MAX_DESCRIPT_SIZE + 64];
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
			snprintf(StatusReport, sizeof StatusReport, "Changing runlevel to: %s", argv[1]);
			
			printf("%s", StatusReport);
			
			if (!InitMemBus(false))
			{
				PrintStatusReport(StatusReport, FAILURE);
				
				SpitError("Failed to communicate with Epoch init, membus is down.");
				return 1;
			}
			
			if (!MemBus_Write(TmpBuf, false))
			{
				PrintStatusReport(StatusReport, FAILURE);
				
				SpitError("Failed to change runlevels, failed to write to membus after establishing connection.\n"
							"Is Epoch the running boot system?");		
				ShutdownMemBus(false);
				
				return 1;
			}
			
			while (!MemBus_Read(MembusResponse, false)) usleep(1000);
			
			if (!strcmp(MembusResponse, PossibleResponses[0]))
			{
				PrintStatusReport(StatusReport, SUCCESS);
				ShutdownMemBus(false);
				
				return 0;
			}
			else if (!strcmp(MembusResponse, PossibleResponses[1]))
			{
				PrintStatusReport(StatusReport, FAILURE);
				ShutdownMemBus(false);
				
				return 1;
			}
			else if (!strcmp(MembusResponse, PossibleResponses[2]))
			{
				PrintStatusReport(StatusReport, FAILURE);
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
				SpitError("Bad signal number. Please specify an integer signal number.\nPass no arguments to assume signal 15.");
				
				return 1;
			}
		}
		else if (argc == 1)
		{
			return !EmulKillall5(SIGTERM);
		}
		else
		{
			SpitError("Too many arguments. Syntax is killall5 -signum where signum is the integer signal number to send.");
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
		SpitError("Unrecognized applet name.");
		return 1;
	}
	
	return 0;
}
#endif
