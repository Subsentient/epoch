/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file holds functions for the various things we can do, depending on argv[0].**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "epoch.h"

void RequestShutdown(const char *MembusCode)
{ /*Client side to send a request to halt the system via membus.*/
	char InitsResponse[MEMBUS_SIZE/2 - 1], *PCode[2];
	
	if (!strcmp(MembusCode, MEMBUS_CODE_HALT))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_HALT;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_HALT;
	}
	else if (!strcmp(MembusCode, MEMBUS_CODE_POWEROFF))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_POWEROFF;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_POWEROFF;
	}
	else if (!strcmp(MembusCode, MEMBUS_CODE_REBOOT))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_REBOOT;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_REBOOT;
	}
	
	if (!InitMemBus(false))
	{
		SpitError("Failed to connect to membus.");
		exit(1);
	}
	
	if (!MemBus_Write(MembusCode, false))
	{
		SpitError("Failed to write to membus.");
		exit(1);
	}
	
	while (!MemBus_Read(InitsResponse, false)) usleep(1000);
	
	if (!strcmp(InitsResponse, PCode[0]))
	{
		ShutdownMemBus(false);
		exit(0);
	}
	else if (!strcmp(InitsResponse, PCode[1]))
	{ /*Nothing uses this right now.*/
		SpitError("Unable to halt the system.");
		ShutdownMemBus(false);
		exit(1);
	}
}
