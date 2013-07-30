/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "epoch.h"

rStatus ExecuteConfigObject(ObjTable *InObj, Bool IsStartingMode)
{ /*Not making static because this is probably going to be useful for other stuff.*/
	FILE *CommandDescriptor;
	char PrintOutStream[1024];
	rStatus ExitStatus = 0;
	
	snprintf(PrintOutStream, 1024, "%s %s", (IsStartingMode ? "Starting" : "Stopping"), InObj->ObjectName);
	
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

