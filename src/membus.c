/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file is responsible for the
 * shared memory communication system
 * and it's extremely simple protocol,
 * called the "membus".
 * **/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "epoch.h"

/*Memory bus uhh, static globals.*/
char *MemData = NULL;

rStatus InitMemBus(Bool ServerSide)
{ /*Fire up the memory bus.*/
	int MemDescriptor;
	
	if ((MemDescriptor = shmget((key_t)MEMKEY, MEMBUS_SIZE, (ServerSide ? (IPC_CREAT | 0666) : 0666))) < 0)
	{
		SpitError("InitMemBus() Failed to allocate memory bus."); /*should probably use perror*/
		return FAILURE;
	}
	
	if ((MemData = shmat(MemDescriptor, NULL, 0)) == (void*)-1)
	{
		SpitError("InitMemBus() Failed to attach memory bus to char *MemData.");
		MemData = NULL;
		return FAILURE;
	}
	
	if (ServerSide) /*Don't nuke messages on startup if we aren't init.*/
	{
		*MemData = MEMBUS_NOMSG; /*Set to no message by default.*/
		*(MemData + (MEMBUS_SIZE/2)) = MEMBUS_NOMSG;
	}
	
	return SUCCESS;
}

rStatus MemBus_Write(const char *InStream, Bool ServerSide)
{
	char *BusStatus = NULL;
	char *BusData = NULL;
	
	if (ServerSide)
	{
		BusStatus = &MemData[MEMBUS_SIZE/2]; /*Clients get the second half of the block.*/
	}
	else
	{
		BusStatus = &MemData[0];
	}
	
	BusData = BusStatus + 1; /*Our actual data goes one byte after the status byte.*/
	
	if (*BusStatus != MEMBUS_NOMSG) /*We didn't clear the bit.*/
	{
		return FAILURE;
	}
	
	strncpy(BusData, InStream, (MEMBUS_SIZE/2 - 1));
	
	*BusStatus = MEMBUS_MSG; /*Now we sent it.*/
	
	return SUCCESS;
}

Bool MemBus_Read(char *OutStream, Bool ServerSide)
{
	char *BusStatus = NULL;
	char *BusData = NULL;
	
	if (ServerSide)
	{
		BusStatus = &MemData[0];
	}
	else
	{
		BusStatus = &MemData[MEMBUS_SIZE/2];
	}
	
	BusData = BusStatus + 1;
		
	if (*BusStatus != MEMBUS_MSG)
	{ /*No data? Quit.*/
		return false;
	}
	
	strncpy(OutStream, BusData, (MEMBUS_SIZE/2 - 1));
	
	*BusStatus = MEMBUS_NOMSG; /*Set back to NOMSG once we got the message.*/

	return true;
}

void EpochMemBusLoop(void)
{
#define BusDataIs(x) !strncmp(x, BusData, strlen(x))
	Bool Idle = true;
	char BusData[MEMBUS_SIZE/2];
	
	while (Idle)
	{
		usleep(1000);
		
		if (!MemBus_Read(BusData, true))
		{
			continue;
		}
		
		/*If we got a signal over the membus.*/
		if (BusDataIs(MEMBUS_CODE_RESET))
		{
			ShutdownConfig();
			
			if (InitConfig())
			{
				MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_RESET, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_RESET, true);
			}
		}
		else if (BusDataIs(MEMBUS_CODE_OBJSTART))
		{
			char *TWorker = BusData + strlen(MEMBUS_CODE_OBJSTART " ");
			ObjTable *CurObj = LookupObjectInTable(TWorker);
			
			if (CurObj)
			{
				ProcessConfigObject(CurObj, true);
				MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_OBJSTART, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_OBJSTART, true);
			}
		}
		else if (BusDataIs(MEMBUS_CODE_OBJSTOP))
		{
			char *TWorker = BusData + strlen(MEMBUS_CODE_OBJSTOP " ");
			ObjTable *CurObj = LookupObjectInTable(TWorker);
			
			if (CurObj)
			{
				ProcessConfigObject(CurObj, false);
				MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_OBJSTOP, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_OBJSTOP, true);
			}
		}
		else if (BusDataIs(MEMBUS_CODE_STATUS))
		{
			char *TWorker = BusData + strlen(MEMBUS_CODE_STATUS " ");
			ObjTable *CurObj = LookupObjectInTable(TWorker);
			
			if (CurObj)
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, 1024, "%s %s %d", MEMBUS_CODE_STATUS, TWorker, CurObj->Started);
				
				MemBus_Write(TmpBuf, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_STATUS, true);
			}
		}
		
	}
}

rStatus ShutdownMemBus(void)
{
	if (!MemData)
	{
		return SUCCESS;
	}
	
	*MemData = MEMBUS_NOMSG;
	
	if (shmdt(MemData) != 0)
	{
		SpitWarning("Unable to shut down memory bus.");
		return FAILURE;
	}
	else
	{
		return SUCCESS;
	}
}
