/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**CLI parsing, etc. main() is here.**/

#include <stdio.h>
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
				printf("%s\n", SuccessMsg);
			}
			
		}
	}
	return SUCCESS;
}

int main(int argc, char **argv)
{ /*Lotsa sloppy CLI processing here.*/
	
	/*Figure out what we are.*/
	if (argv[0] == NULL)
	{
		SpitError("main(): argv[0] is NULL. Why?");
		return 1;
	}
	
	if (CmdIs("poweroff") || CmdIs("reboot") || CmdIs("halt"))
	{
		return !ProcessGenericHalt(argc, argv);
	}
	
	if (CmdIs("init"))
	{ /*This is a bit long winded here, however, it's better than devoting a function for it.*/
		if (argc == 1)
		{ /*Just us, as init. That means, begin bootup.*/
			LaunchBootup();
		}
		else if (argv[1] != NULL && argc == 2)
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
	else
	{
		SpitError("Unrecognized applet name.");
		return 1;
	}
	
	return 0;
}
	
