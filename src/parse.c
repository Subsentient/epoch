/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include "epoch.h"

/*We store the current runlevel here.*/
char CurRunlevel[MAX_DESCRIPT_SIZE] = "default";

rStatus ExecuteConfigObject(ObjTable *InObj, Bool IsStartingMode)
{ /*Not making static because this is probably going to be useful for other stuff.*/
	FILE *CommandDescriptor;
	char PrintOutStream[1024];
	rStatus ExitStatus = 0;
	
	snprintf(PrintOutStream, 1024, "%s %s", (IsStartingMode ? "Starting" : "Stopping"), InObj->ObjectName);
	
	printf(PrintOutStream);
	
	CommandDescriptor = popen((IsStartingMode ? InObj->ObjectStartCommand : InObj->ObjectStopCommand), "r");
	
	switch (WEXITSTATUS(pclose(CommandDescriptor)))
	{ /*FIXME: Make this do more later.*/
		case 128: /*Bad exit parameter*/
		case -1: /*Out of range for exit status. Probably shows as an unsigned value on some machines anyways.*/
			ExitStatus = WARNING;
			break;
		case 0:
			ExitStatus = SUCCESS;
			break;
		default:
			ExitStatus = FAILURE;
			break;
	}
	
	PrintStatusReport(PrintOutStream, ExitStatus); /*Show Done, Failure. Warning, etc.*/
	
	return ExitStatus;
}

/*This function does what it sounds like. It's not the entire boot sequence, we gotta display a message and stuff.*/
rStatus RunAllObjects(Bool IsStartingMode)
{
	unsigned long MaxPriority = GetHighestPriority(IsStartingMode);
	unsigned long Inc = 1; /*One to skip zero.*/
	ObjTable *CurObj = NULL;
	
	if (!MaxPriority && IsStartingMode)
	{
		SpitError("All objects have a priority of zero!");
		return FAILURE;
	}
	
	for (; Inc <= MaxPriority; ++Inc)
	{
		if (!(CurObj = GetObjectByPriority(CurRunlevel, IsStartingMode, Inc)))
		{ /*Probably set to zero or something, but we don't care if we have a gap in the priority system.*/
			continue;
		}
		
		if (IsStartingMode)
		{
			ExecuteConfigObject(CurObj, IsStartingMode); /*Don't bother with return value here.*/
		}
		else
		{
			switch (CurObj->StopMode)
			{
				case STOP_COMMAND:
					ExecuteConfigObject(CurObj, IsStartingMode);
					break;
				case STOP_NONE:
					break;
				case STOP_PID:
					kill(CurObj->ObjectPID, OSCTL_SIGNAL_TERM); /*Just send SIGTERM.*/
					break;
			}
		}
	}
	
	return SUCCESS;
}
