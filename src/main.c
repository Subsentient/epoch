/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**CLI parsing, etc. main() is here.**/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>
#include "epoch.h"

#define ArgIs(z) !strcmp(CArg, z)
#define CmdIs(z) __CmdIs(argv[0], z)

/*Forward declarations for static functions.*/
static rStatus ProcessGenericHalt(int argc, char **argv);
static Bool __CmdIs(const char *CArg, const char *InCmd);

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

static rStatus ProcessGenericHalt(int argc, char **argv)
{
	char *CArg = argv[1];
	
	/*Figure out what we are.*/
	if (argv[0] == NULL)
	{
		SpitError("main(): argv[0] is NULL. Why?");
		return 1;
	}
	
	if (CmdIs("poweroff") || CmdIs("halt") || CmdIs("reboot"))
	{
		char *NCode = NULL, *GCode = NULL, *SuccessMsg = NULL, *FailMsg[2] = { NULL, NULL };
		
		if (CmdIs("poweroff"))
		{
			NCode = MEMBUS_CODE_POWEROFFNOW;
			GCode = MEMBUS_CODE_POWEROFF;
			SuccessMsg = "Power off in progress.";
			FailMsg[0] = "Failed to request immediate poweroff.";
			FailMsg[1] = "Failed to request poweroff.";
		}
		else if (CmdIs("reboot"))
		{
			NCode = MEMBUS_CODE_REBOOTNOW;
			GCode = MEMBUS_CODE_REBOOT;
			SuccessMsg = "Reboot in progress.";
			FailMsg[0] = "Failed to request immediate reboot.";
			FailMsg[1] = "Failed to request reboot.";
		}
		else if (CmdIs("halt"))
		{
			NCode = MEMBUS_CODE_HALTNOW;
			GCode = MEMBUS_CODE_HALT;
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
				if (!TellInitToDo(NCode))
				{
					SpitError(FailMsg[0]);
					return FAILURE;
				}
			}
			else
			{
				SpitError("Bad argument(s).");
				return FAILURE;
			}
		}
		else
		{
			if (!TellInitToDo(GCode))
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

int main(int argc, char **argv)
{ /*Lotsa sloppy CLI processing here.*/
	const char *CArg = argv[1];
	
	/*Figure out what we are.*/
	if (argv[0] == NULL)
	{
		SpitError("main(): argv[0] is NULL. Why?");
		return 1;
	}

	
	if (CmdIs("poweroff") || CmdIs("reboot") || CmdIs("halt"))
	{
		Bool RVal;
		
		/*Start membus.*/
		if (!InitMemBus(false))
		{
			SpitError("Failed to connect to membus.");
			return 1;
		}
		
		RVal = !ProcessGenericHalt(argc, argv);
		
		ShutdownMemBus(false);
		
		return (int)RVal;
	}
	else if (CmdIs("epoch")) /*Our main management program.*/
	{
		if (!InitMemBus(false))
		{
			SpitError("Failed to connect to membus.");
			return 1;
		}
		
		/*One argument?*/
		if (argc == 2)
		{
			CArg = argv[1];
			
			if (ArgIs("poweroff") || ArgIs("reboot") || ArgIs("halt"))
			{
				const Bool RVal = !ProcessGenericHalt(argc, argv);
				
				ShutdownMemBus(false);
				return (int)RVal;
			}
			else if (ArgIs("configreload"))
			{
				char TRecv[MEMBUS_SIZE/2 - 1];
				char TBuf[3][MAX_LINE_SIZE];
				
				if (!MemBus_Write(MEMBUS_CODE_RESET, false))
				{
					SpitError("Failed to write to membus.");
					ShutdownMemBus(false);
					return 1;
				}
				
				while (!MemBus_Read(TRecv, false)) usleep(1000);
				
				snprintf(TBuf[0], sizeof TBuf[0], "%s %s", MEMBUS_CODE_ACKNOWLEDGED, MEMBUS_CODE_RESET);
				snprintf(TBuf[1], sizeof TBuf[1], "%s %s", MEMBUS_CODE_FAILURE, MEMBUS_CODE_RESET);
				snprintf(TBuf[2], sizeof TBuf[2], "%s %s", MEMBUS_CODE_BADPARAM, MEMBUS_CODE_RESET);
				
				if (!strcmp(TBuf[0], TRecv))
				{
					puts("Reload successful.");
					ShutdownMemBus(false);
					
					return 0;
				}
				else if (!strcmp(TBuf[1], TRecv))
				{
					puts("Reload failed!");
					ShutdownMemBus(false);
					
					return 1;
				}
				else if (!strcmp(TBuf[2], TRecv))
				{
					SpitError("We are being told that MEMBUS_CODE_RESET is not a valid signal! Please report to Epoch.");
					ShutdownMemBus(false);
					
					return 1;
				}
				else
				{
					SpitError("Unknown response received! Can't handle this! Report to Epoch please!");
					ShutdownMemBus(false);
					
					return 1;
				}
			}
			
			else
			{
				fprintf(stderr, "Bad command %s.\n", CArg);
				ShutdownMemBus(false);
				return 1;
			}
		}
		else if (argc == 3)
		{
			CArg = argv[1];
			
			if (ArgIs("enable") || ArgIs("disable"))
			{
				rStatus RV = SUCCESS;
				Bool Enabling = ArgIs("enable");
				char TOut[MAX_LINE_SIZE];
				
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
				return 0;
			}
			else
			{
				fprintf(stderr, "Bad command %s.\n", argv[1]);
				ShutdownMemBus(false);
				return 1;
			}
		}
		else if (argc == 5)
		{
			if (ArgIs("objrl"))
			{
				const char *ObjectID = argv[2], *RL = argv[4];
				char OBuf[MEMBUS_SIZE/2 - 1];
				char IBuf[MEMBUS_SIZE/2 - 1];
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
					return 1;
				}
				
				if (!MemBus_Write(OBuf, false))
				{
					SpitError("Failed to write to membus.");
					ShutdownMemBus(false);
					return 1;
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
						return 0;
					}
					else if (!strcmp(PossibleResponses[1], IBuf))
					{
						char *PSFormat[2] = { "Unable to add %s to runlevel %s!\n", "Unable to remove %s from runlevel %s!\n" };
						
						fprintf(stderr, PSFormat[(ArgIs("add") ? 0 : 1)], ObjectID, RL);
						ShutdownMemBus(false);
						
						return 1;
					}
					else if (!strcmp(PossibleResponses[2], IBuf))
					{
						SpitError("Internal membus error, received BADPARAM upon your request. Please report to Epoch.");
						ShutdownMemBus(false);
						
						return 1;
					}
					else
					{
						SpitError("Received unrecognized or corrupted response via membus! Please report to Epoch.");
						ShutdownMemBus(false);
						
						return 1;
					}
				}
			}
			else
			{
				fprintf(stderr, "Bad command %s.\n", CArg);
				ShutdownMemBus(false);
				return 1;
			}
		}
		else
		{
			fprintf(stderr, "%s\n", "Invalid usage.");
			ShutdownMemBus(false);
			return 1;
		}
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
			
			if (isdigit(*CArg))
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
			return !EmulKillall5(OSCTL_SIGNAL_TERM);
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
	else
	{
		SpitError("Unrecognized applet name.");
		return 1;
	}
	
	return 0;
}
