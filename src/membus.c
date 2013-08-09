/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
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
#include <sys/reboot.h>
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
		memset(MemData, 0, MEMBUS_SIZE); /*Zero it out just to be neat. Probably don't really need this.*/
		
		*MemData = MEMBUS_NOMSG; /*Set to no message by default.*/
		*(MemData + (MEMBUS_SIZE/2)) = MEMBUS_NEWCONNECTION;
	}
	else if (*(MemData + (MEMBUS_SIZE/2)) != MEMBUS_NEWCONNECTION)
	{ /*Snowball's chance in hell that this will not always work.*/
		SpitError("InitMemBus() Bus is not running.");
		return FAILURE;
	}
	else
	{
		*(MemData + (MEMBUS_SIZE/2)) = MEMBUS_NOMSG;
	}
		
	return SUCCESS;
}

rStatus MemBus_Write(const char *InStream, Bool ServerSide)
{
	char *BusStatus = NULL;
	char *BusData = NULL;
	unsigned short WaitCount = 0;
	
	if (ServerSide)
	{
		BusStatus = &MemData[MEMBUS_SIZE/2]; /*Clients get the second half of the block.*/
	}
	else
	{
		BusStatus = &MemData[0];
	}
	
	BusData = BusStatus + 1; /*Our actual data goes one byte after the status byte.*/
	
	while (*BusStatus != MEMBUS_NOMSG) /*Wait for them to finish eating their last message.*/
	{
		usleep(100000); /*0.1 seconds.*/
		++WaitCount;
		
		if (WaitCount == 100)
		{ /*Been 10 seconds! Does it take that long to copy a string?*/
			return FAILURE;
		}
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
		else if (BusDataIs(MEMBUS_CODE_OBJSTART) || BusDataIs(MEMBUS_CODE_OBJSTOP))
		{
			unsigned long LOffset = strlen((BusDataIs(MEMBUS_CODE_OBJSTART) ? MEMBUS_CODE_OBJSTART " " : MEMBUS_CODE_OBJSTOP " "));
			char *TWorker = BusData + LOffset;
			ObjTable *CurObj = LookupObjectInTable(TWorker);
			char TmpBuf[MEMBUS_SIZE/2 - 1], *MCode;
			rStatus DidWork;
			
			if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
			{ /*No argument?*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
				MemBus_Write(TmpBuf, true);
				
				continue;
			}
			
			if (CurObj)
			{
				DidWork = ProcessConfigObject(CurObj, (BusDataIs(MEMBUS_CODE_OBJSTART) ? true : false));
			}
			else
			{
				DidWork = FAILURE;
			}
			
			switch (DidWork)
			{
				case SUCCESS:
					MCode = MEMBUS_CODE_ACKNOWLEDGED;
					break;
				case WARNING:
					MCode = MEMBUS_CODE_WARNING;
					break;
				case FAILURE:
					MCode = MEMBUS_CODE_FAILURE;
					break;
			}
			
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s",
			MCode, (BusDataIs(MEMBUS_CODE_OBJSTART) ? MEMBUS_CODE_OBJSTART : MEMBUS_CODE_OBJSTOP), TWorker);
			
			MemBus_Write(TmpBuf, true);
		}
		else if (BusDataIs(MEMBUS_CODE_STATUS))
		{
			unsigned long LOffset = strlen(MEMBUS_CODE_STATUS " ");
			char *TWorker = BusData + LOffset;
			ObjTable *CurObj = LookupObjectInTable(TWorker);
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			
			if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
			{ /*No argument?*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
				MemBus_Write(TmpBuf, true);
				
				continue;
			}
			
			if (CurObj)
			{
				char TmpBuf[MEMBUS_SIZE/2 - 1];
				
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s %d", MEMBUS_CODE_STATUS, TWorker, CurObj->Started);
				
				MemBus_Write(TmpBuf, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_STATUS, true);
			}
		}
		else if (BusDataIs(MEMBUS_CODE_OBJENABLE) || BusDataIs(MEMBUS_CODE_OBJDISABLE))
		{
			Bool EnablingThis = (BusDataIs(MEMBUS_CODE_OBJENABLE) ? true : false);
			unsigned long LOffset = strlen(EnablingThis ? MEMBUS_CODE_OBJENABLE " " : MEMBUS_CODE_OBJDISABLE " ");
			char *OurSignal = EnablingThis ? MEMBUS_CODE_OBJENABLE : MEMBUS_CODE_OBJDISABLE;
			char *TWorker = BusData + LOffset;
			ObjTable *CurObj = LookupObjectInTable(TWorker);
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			rStatus DidWork = FAILURE;
			
			if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
			{ /*No argument?*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
				MemBus_Write(TmpBuf, true);
				
				continue;
			}
			
			if (!CurObj)
			{
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s", MEMBUS_CODE_FAILURE, OurSignal, TWorker);
				MemBus_Write(TmpBuf, true);
				
				continue;
			}
			
			CurObj->Enabled = (EnablingThis ? true : false);
			DidWork = EditConfigValue(TWorker, "ObjectEnabled", EnablingThis ? "true" : "false");
			
			switch (DidWork)
			{
				case SUCCESS:
					snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, BusData);
					break;
				case WARNING:
					snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_WARNING, BusData);
					break;
				case FAILURE:
					snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
					break;
				default:
					snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
					break;
			}
				
				
			MemBus_Write(TmpBuf, true);
		}
		else if (BusDataIs(MEMBUS_CODE_RUNLEVEL))
		{
			unsigned long LOffset = strlen(MEMBUS_CODE_RUNLEVEL " ");
			char *TWorker = BusData + LOffset;
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			ObjTable *TmpTable = ObjectTable;
			Bool ValidRL = false;
			
			if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
			{ /*No argument?*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
				MemBus_Write(TmpBuf, true);
				
				continue;
			}
			
			for (; TmpTable->Next; TmpTable = TmpTable->Next)
			{ /*Check if anything uses this runlevel at all.*/
				if (ObjRL_CheckRunlevel(TWorker, TmpTable))
				{
					ValidRL = true;
					break;
				}
			}
			
			if (ValidRL)
			{
				/*Tell them everything is OK, because we don't want to wait the whole time for the runlevel to start up.*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s", MEMBUS_CODE_ACKNOWLEDGED, MEMBUS_CODE_RUNLEVEL, TWorker);
				MemBus_Write(TmpBuf, true);
			}
			else
			{
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s", MEMBUS_CODE_FAILURE, MEMBUS_CODE_RUNLEVEL, TWorker);
				MemBus_Write(TmpBuf, true);
				continue;
			}
			
			if (!SwitchRunlevels(TWorker)) /*Switch to it.*/
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, sizeof TmpBuf, "Failed to switch to runlevel \"%s\".", TWorker);
				SpitError(TmpBuf);
			}
		}
		else if (BusDataIs(MEMBUS_CODE_HALT))
		{
			MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_HALT, true);
			LaunchShutdown(OSCTL_LINUX_HALT);
		}
		else if (BusDataIs(MEMBUS_CODE_POWEROFF))
		{
			MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_POWEROFF, true);
			LaunchShutdown(OSCTL_LINUX_POWEROFF);
		}
		else if (BusDataIs(MEMBUS_CODE_REBOOT))
		{
			MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_REBOOT, true);
			LaunchShutdown(OSCTL_LINUX_REBOOT);
		}
		else if (BusDataIs(MEMBUS_CODE_CADOFF))
		{
			if (!reboot(OSCTL_LINUX_DISABLE_CTRLALTDEL))
			{
				MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_CADOFF, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_CADOFF, true);
			}
		}
		else if (BusDataIs(MEMBUS_CODE_CADON))
		{
			if (!reboot(OSCTL_LINUX_ENABLE_CTRLALTDEL))
			{
				MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_CADON, true);
			}
			else
			{
				MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_CADON, true);
			}
		}
		else
		{
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			
			MemBus_Write(TmpBuf, true);
		}
	}
}

rStatus ShutdownMemBus(Bool ServerSide)
{
	if (!ServerSide)
	{ /*We write to our own code.*/
		*(MemData + (MEMBUS_SIZE/2)) = MEMBUS_NEWCONNECTION;
		return SUCCESS;
	}
	
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
