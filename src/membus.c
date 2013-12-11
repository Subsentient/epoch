/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

/**This file is responsible for the
 * shared memory communication system
 * and it's extremely simple protocol,
 * called the "membus".
 * **/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/reboot.h>
#include "epoch.h"

/*Memory bus uhh, static globals.*/
volatile char *MemData = NULL;
volatile Bool BusRunning = false;
volatile signed long MemBusKey = MEMKEY;
volatile int MemDescriptor = 0;

rStatus InitMemBus(Bool ServerSide)
{ /*Fire up the memory bus.*/
	char CheckCode = 0;
	unsigned long Inc = 0;
	
	
	if (BusRunning) return SUCCESS;
	
	if ((MemDescriptor = shmget((key_t)MemBusKey, MEMBUS_SIZE, (ServerSide ? (IPC_CREAT | 0660) : 0660))) < 0)
	{
		if (ServerSide) SpitError("InitMemBus(): Failed to allocate memory bus."); /*should probably use perror*/
		else SpitError("InitMemBus(): Failed to connect to memory bus. Permissions?");
		return FAILURE;
	}
	
	if ((MemData = shmat(MemDescriptor, NULL, 0)) == (void*)-1)
	{
		SpitError("InitMemBus(): Failed to attach memory bus to char *MemData.");
		MemData = NULL;
		return FAILURE;
	}
	
	if (ServerSide) /*Don't nuke messages on startup if we aren't init.*/
	{
		memset((void*)MemData, 0, MEMBUS_SIZE); /*Zero it out just to be neat. Probably don't really need this.*/
		
		*MemData = MEMBUS_NOMSG; /*Set to no message by default.*/
	}
	else
	{ /*Client side stuff.*/
		for (; *MemData != MEMBUS_NOMSG && *MemData != MEMBUS_MSG; ++Inc)
		{ /*Wait for server-side to finish setting up it's half, if it was just starting up itself.*/
			if (Inc == 100000) /*Ten secs.*/
			{
				SmallError("Cannot connect to Epoch over MemBus, stream corrupted. Aborting MemBus initialization.");
				BusRunning = false;
				
				return FAILURE;
			}
			
			usleep(100);
		}
		
		CheckCode = *MemData = (*MemData == MEMBUS_MSG ? MEMBUS_CHECKALIVE_MSG : MEMBUS_CHECKALIVE_NOMSG); /*Ask server-side if they're alive.*/
		
		for (Inc = 0; *MemData == CheckCode; ++Inc)
		{ /*Wait ten seconds for server-side to respond.*/
			if (Inc == 100000)
			{ /*Ten seconds.*/
				SmallError("Cannot connect to Epoch over MemBus, timeout expired. Aborting MemBus initialization.");
				
				BusRunning = false;
				
				return FAILURE;
			}
			
			usleep(100);
		}
		
		*(MemData + (MEMBUS_SIZE/2)) = MEMBUS_NOMSG; /*Initialize client side.*/
	}
	/*Either the server side is alive, or we ARE the server side.*/
	BusRunning = true;
	
	return SUCCESS;
}

unsigned long MemBus_BinWrite(const void *InStream_, unsigned long DataSize, Bool ServerSide)
{ /*Copies binary data of length DataSize to the membus.*/
	const char *InStream = InStream_;
	volatile char *BusData = NULL, *BusStatus = NULL;
	unsigned long Inc = 0;
	unsigned short WaitCount = 0;
	
	if (ServerSide)
	{
		BusStatus = &MemData[MEMBUS_SIZE/2];
	}
	else
	{
		BusStatus = MemData;
	}
	
	BusData = BusStatus + 1;
	
	while (*BusStatus != MEMBUS_NOMSG) /*Wait ten secs for their last message to process.*/
	{
		usleep(1000); /*0.001 seconds.*/
		++WaitCount;
		
		if (WaitCount == 10000)
		{
			return 0;
		}
	}
	
	for (; Inc < DataSize && Inc < MEMBUS_SIZE/2 - 1; ++Inc)
	{
		BusData[Inc] = InStream[Inc];
	}
	
	*BusStatus = MEMBUS_MSG;
	
	return Inc; /*Return number of bytes written.*/
}

unsigned long MemBus_BinRead(void *OutStream_, unsigned long MaxOutSize, Bool ServerSide)
{
	volatile char *BusStatus = NULL, *BusData = NULL;
	char *OutStream = OutStream_;
	unsigned long Inc = 0;
	
	if (ServerSide)
	{
		BusStatus = MemData;
	}
	else
	{
		BusStatus = &MemData[MEMBUS_SIZE/2];
	}
	
	BusData = BusStatus + 1;
	
	if (*BusStatus != MEMBUS_MSG)
	{
		return 0;
	}
	
	for (; Inc < MaxOutSize && Inc < MEMBUS_SIZE/2 - 1; ++Inc)
	{
		OutStream[Inc] = BusData[Inc];
	}
	
	*BusStatus = MEMBUS_NOMSG;
	
	return Inc;
}
	
rStatus MemBus_Write(const char *InStream, Bool ServerSide)
{
	volatile char *BusStatus = NULL;
	volatile char *BusData = NULL;
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
		usleep(1000); /*0.001 seconds.*/
		++WaitCount;
		
		if (WaitCount == 10000)
		{ /*Been 10 seconds! Does it take that long to copy a string?*/
			return FAILURE;
		}
	}
	
	snprintf((char*)BusData, MEMBUS_SIZE/2 - 1, "%s", InStream);
	
	*BusStatus = MEMBUS_MSG; /*Now we sent it.*/
	
	return SUCCESS;
}

Bool MemBus_Read(char *OutStream, Bool ServerSide)
{
	volatile char *BusStatus = NULL;
	volatile char *BusData = NULL;
	
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
	
	snprintf(OutStream, MEMBUS_SIZE/2 - 1, "%s", BusData);
	
	*BusStatus = MEMBUS_NOMSG; /*Set back to NOMSG once we got the message.*/

	return true;
}

Bool HandleMemBusPings(void)
{
	if (!BusRunning) return false;
	
	switch (*MemData)
	{
		case MEMBUS_CHECKALIVE_MSG:
			*MemData = MEMBUS_MSG;
			return true;
			break;
		case MEMBUS_CHECKALIVE_NOMSG:
			*MemData = MEMBUS_NOMSG;
			return true;
			break;
		default:
			break;
	}
	
	return false;
}

void ParseMemBus(void)
{ /*This function handles EVERYTHING passed to us via membus. It's truly vast.*/
#define BusDataIs(x) !strncmp(x, BusData, strlen(x))
	char BusData[MEMBUS_SIZE/2];

	if (!BusRunning) return;
	
	if (!MemBus_Read(BusData, true))
	{
		return;
	}
	
	/*If we got a signal over the membus.*/
	if (BusDataIs(MEMBUS_CODE_RESET))
	{
		if (ReloadConfig())
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
		char TmpBuf[MEMBUS_SIZE/2 - 1], *MCode = MEMBUS_CODE_FAILURE;
		rStatus DidWork;
		
		if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
		{ /*No argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);
			
			return;
		}
		
		if (CurObj)
		{ /*If we ask to start a HaltCmdOnly command, run the stop command instead, because that's all that we use.*/
			DidWork = ProcessConfigObject(CurObj, (BusDataIs(MEMBUS_CODE_OBJSTART) && !CurObj->Opts.HaltCmdOnly), false);
			
			snprintf(TmpBuf, sizeof TmpBuf, "Manual %s of object %s %s%s", (BusDataIs(MEMBUS_CODE_OBJSTART) ? "start" : "stop"),
					CurObj->ObjectID, (DidWork ? "succeeded" : "failed"), ((DidWork == WARNING) ? " with a warning" : ""));
			WriteLogLine(TmpBuf, true);
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
			
			return;
		}
		
		if (CurObj)
		{
			char TmpBuf[MEMBUS_SIZE/2 - 1];
			/*Don't let HaltCmdOnly objects be reported as started, because they always look like that anyways.*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s %d %d %d", MEMBUS_CODE_STATUS, TWorker,
					CurObj->Started && !CurObj->Opts.HaltCmdOnly, ObjectProcessRunning(CurObj), CurObj->Enabled);
			
			MemBus_Write(TmpBuf, true);
		}
		else
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
			
			MemBus_Write(TmpBuf, true);
		}
	}
	else if (BusDataIs(MEMBUS_CODE_LSOBJS))
	{ /*Done for mostly third party stuff.*/
		char OutBuf[MEMBUS_SIZE/2 - 1];
		ObjTable *Worker = ObjectTable;
		unsigned long TPID = 0;
		
		for (; Worker->Next; Worker = Worker->Next)
		{
			const struct _RLTree *RLWorker = Worker->ObjectRunlevels;
			
			if (strlen(BusData) > strlen(MEMBUS_CODE_LSOBJS) && strcmp(BusData + strlen(MEMBUS_CODE_LSOBJS " "), Worker->ObjectID) != 0)
			{
				continue;
			}
			
			if (!Worker->Opts.HasPIDFile || !(TPID = ReadPIDFile(Worker)))
			{
				TPID = Worker->ObjectPID;
			}
			
			/*We need a version for this protocol, because relevant options can change with updates.
			 * Not all options are here, because some are not really useful.*/
			snprintf(OutBuf, sizeof OutBuf, "%s %s %s %lu %s %lu %d %d %d %d %d %d %d %d %d %d %d %lu",
					MEMBUS_CODE_LSOBJS, MEMBUS_LSOBJS_VERSION, Worker->ObjectID,
					(unsigned long)strlen(Worker->ObjectDescription),
					Worker->ObjectDescription, TPID, (Worker->Started && !Worker->Opts.HaltCmdOnly),
					ObjectProcessRunning(Worker), Worker->Enabled, Worker->Opts.CanStop,
					Worker->Opts.HaltCmdOnly, Worker->Opts.IsService, Worker->Opts.AutoRestart,
					Worker->Opts.ForceShell, Worker->Opts.RawDescription, Worker->Opts.StopMode,
					Worker->TermSignal, Worker->StartedSince);
			
			MemBus_Write(OutBuf, true);
			
			if (RLWorker)
			{
				for (; RLWorker->Next; RLWorker = RLWorker->Next)
				{ /*Send all runlevels.*/
					snprintf(OutBuf, sizeof OutBuf, "%s %s %s %s", MEMBUS_CODE_LSOBJS,
							MEMBUS_LSOBJS_VERSION, Worker->ObjectID, RLWorker->RL);
					puts(OutBuf);
					MemBus_Write(OutBuf, true);
				}
			}
		}
		
		/*This says we are done.*/
		MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_LSOBJS, true);
		return;
	}					
	else if (BusDataIs(MEMBUS_CODE_GETRL))
	{
		char TmpBuf[MEMBUS_SIZE/2 - 1];
		
		snprintf(TmpBuf, sizeof TmpBuf, MEMBUS_CODE_GETRL " %s", CurRunlevel);
		MemBus_Write(TmpBuf, true);
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
			
			return;
		}
		
		if (!CurObj)
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s", MEMBUS_CODE_FAILURE, OurSignal, TWorker);
			MemBus_Write(TmpBuf, true);
			
			return;
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
		
		if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
		{ /*No argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);
			
			return;
		}
		
		if (ObjRL_ValidRunlevel(TWorker))
		{
			/*Tell them everything is OK, because we don't want to wait the whole time for the runlevel to start up.*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s", MEMBUS_CODE_ACKNOWLEDGED, MEMBUS_CODE_RUNLEVEL, TWorker);
			MemBus_Write(TmpBuf, true);
		}
		else
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s", MEMBUS_CODE_FAILURE, MEMBUS_CODE_RUNLEVEL, TWorker);
			MemBus_Write(TmpBuf, true);
			return;
		}
		
		snprintf(TmpBuf, sizeof TmpBuf, CONSOLE_COLOR_CYAN "Changing runlevel to \"%s\"...\n" CONSOLE_ENDCOLOR, TWorker);
		WriteLogLine(TmpBuf, true);
		
		printf("%s", TmpBuf);
		fflush(stdout);
		
		if (!SwitchRunlevels(TWorker)) /*Switch to it.*/
		{
			char TmpBuf[1024];
			
			snprintf(TmpBuf, sizeof TmpBuf, "Failed to switch to runlevel \"%s\".", TWorker);
			SpitError(TmpBuf);
			WriteLogLine(TmpBuf, true);
		}
		else
		{
			snprintf(TmpBuf, sizeof TmpBuf, CONSOLE_COLOR_GREEN "Switched to runlevel \"%s\"." CONSOLE_ENDCOLOR, TWorker);
			puts(TmpBuf);
			WriteLogLine(TmpBuf, true);
		}
	}
	else if (BusDataIs(MEMBUS_CODE_OBJRLS))
	{
		unsigned long LOffset;
		char *TWorker = NULL;
		ObjTable *CurObj = NULL;
		char TmpBuf[MEMBUS_SIZE/2 - 1];
		char TRL[MAX_DESCRIPT_SIZE];
		char TID[MAX_DESCRIPT_SIZE];
		unsigned long Inc = 0;
		
		if (BusDataIs(MEMBUS_CODE_OBJRLS_CHECK)) LOffset = strlen(MEMBUS_CODE_OBJRLS_CHECK " ");
		else if (BusDataIs(MEMBUS_CODE_OBJRLS_ADD)) LOffset = strlen(MEMBUS_CODE_OBJRLS_ADD " ");
		else if (BusDataIs(MEMBUS_CODE_OBJRLS_DEL)) LOffset = strlen(MEMBUS_CODE_OBJRLS_DEL " ");
		else
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);
			return;
		}
		
		TWorker = BusData + LOffset;
		
		if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
		{ /*No argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);

			return;
		}
		
		for (; TWorker[Inc] != ' ' && TWorker[Inc] != '\0'; ++Inc)
		{
			TID[Inc] = TWorker[Inc];
		}
		TID[Inc] = '\0';
		
		if ((TWorker = strstr(TWorker, " ")) == NULL)
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);

			return;
		}
		++TWorker;
		
		snprintf(TRL, sizeof TRL, "%s", TWorker);
		
		if ((CurObj = LookupObjectInTable(TID)))
		{
			if (CurObj->Opts.HaltCmdOnly)
			{ /*These objects have no runlevels.*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
				MemBus_Write(TmpBuf, true);
				return;
			}
			
			if (BusDataIs(MEMBUS_CODE_OBJRLS_CHECK))
			{
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s %s %d", MEMBUS_CODE_OBJRLS_CHECK, TID, TRL,
						ObjRL_CheckRunlevel(TRL, CurObj, true));
			}
			else if (BusDataIs(MEMBUS_CODE_OBJRLS_ADD) || BusDataIs(MEMBUS_CODE_OBJRLS_DEL))
			{
				char *RLStream = malloc(MAX_DESCRIPT_SIZE + 1);
				struct _RLTree *ObjRLS = CurObj->ObjectRunlevels;
				
				if (BusDataIs(MEMBUS_CODE_OBJRLS_ADD))
				{
					if (!ObjRL_CheckRunlevel(TRL, CurObj, false))
					{
						ObjRL_AddRunlevel(TRL, CurObj);
					}
				}
				else
				{
					unsigned long TInc = 0;
					
					/*Count number of entries.*/
					for (; ObjRLS->Next != NULL; ++TInc) ObjRLS = ObjRLS->Next;
					
					if (TInc == 1 || !ObjRL_DelRunlevel(TRL, CurObj))
					{
						snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
						MemBus_Write(TmpBuf, true);
						free(RLStream);
						return;
					}
					ObjRLS = CurObj->ObjectRunlevels;
					if (!ObjRLS->Next)
					{
						ObjRLS->Next = malloc(sizeof(struct _RLTree));
						ObjRLS->Next->Prev = ObjRLS;
						ObjRLS->Next->Next = NULL;
					}
				}
				
				*RLStream = '\0';

				for (; ObjRLS->Next != NULL; ObjRLS = ObjRLS->Next)
				{
					strncat(RLStream, ObjRLS->RL, MAX_DESCRIPT_SIZE);
						
					if (ObjRLS->Next->Next != NULL)
					{
						strncat(RLStream, " ", 1);
						RLStream = realloc(RLStream, strlen(RLStream) + MAX_DESCRIPT_SIZE);
					}
				}
				
				if (!EditConfigValue(CurObj->ObjectID, "ObjectRunlevels", RLStream))
				{
					snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
					MemBus_Write(TmpBuf, true);
					free(RLStream);
					return;
				}
				
				free(RLStream);
				
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, BusData);
				
			}
			
			MemBus_Write(TmpBuf, true);
		}
		else
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
			MemBus_Write(TmpBuf, true);
		}
	}
	/*Power functions that close everything first.*/
	else if (BusDataIs(MEMBUS_CODE_HALT) || BusDataIs(MEMBUS_CODE_POWEROFF) || BusDataIs(MEMBUS_CODE_REBOOT))
	{
		unsigned long LOffset = 0, Signal;
		char *TWorker = NULL, *MSig = NULL;
		char TmpBuf[MEMBUS_SIZE/2 - 1];
		
		if (BusDataIs(MEMBUS_CODE_HALT))
		{
			LOffset = strlen(MEMBUS_CODE_HALT " ");
			Signal = OSCTL_LINUX_HALT;
			MSig = MEMBUS_CODE_HALT;
			
		}
		else if (BusDataIs(MEMBUS_CODE_POWEROFF))
		{
			LOffset = strlen(MEMBUS_CODE_POWEROFF " ");
			Signal = OSCTL_LINUX_POWEROFF;
			MSig = MEMBUS_CODE_POWEROFF;
		}
		else if (BusDataIs(MEMBUS_CODE_REBOOT))
		{
			LOffset = strlen(MEMBUS_CODE_REBOOT " ");
			Signal = OSCTL_LINUX_REBOOT;
			MSig = MEMBUS_CODE_REBOOT;
		}
		
		
		TWorker = BusData + LOffset;
		
		if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
		{ /*No argument? Just do the action.*/
			
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, MSig);
			MemBus_Write(TmpBuf, true);
			
			LaunchShutdown(Signal);

			return;
		}
		
		if (strstr(TWorker, ":") && strstr(TWorker, "/"))
		{
			char MsgBuf[MAX_LINE_SIZE];
			const char *HType = NULL;
			char Hr[16], Min[16];
			
			if (HaltParams.HaltMode != -1)
			{/*Don't let us schedule two shutdowns.*/
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
				
				MemBus_Write(TmpBuf, true);
				return;
			}
				
			if (sscanf(TWorker, "%lu:%lu:%lu %lu/%lu/%lu", &HaltParams.TargetHour, &HaltParams.TargetMin,
				&HaltParams.TargetSec, &HaltParams.TargetMonth, &HaltParams.TargetDay, &HaltParams.TargetYear) != 6)
			{
				SpitError("Invalid time signature for HALT/REBOOT/POWEROFF over membus.\n"
							"Please report to Epoch. This is probably a bug.");

				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
				MemBus_Write(TmpBuf, true);
				
				return;
			}
			
			HaltParams.HaltMode = Signal;

			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, BusData);
			MemBus_Write(TmpBuf, true);

			if (Signal == OSCTL_LINUX_HALT) HType = "halt";
			else if (Signal == OSCTL_LINUX_POWEROFF) HType = "poweroff";
			else if (Signal == OSCTL_LINUX_REBOOT) HType = "reboot";

			snprintf(Hr, 16, (HaltParams.TargetHour >= 10) ? "%ld" : "0%ld", HaltParams.TargetHour);
			snprintf(Min, 16, (HaltParams.TargetMin >= 10) ? "%ld" : "0%ld", HaltParams.TargetMin);
			
			snprintf(MsgBuf, sizeof MsgBuf, "System is going down for %s at %s:%s %ld/%ld/%ld!",
				HType, Hr, Min, HaltParams.TargetMonth, HaltParams.TargetDay, HaltParams.TargetYear);
					
			EmulWall(MsgBuf, false);
			return;
		}
		else
		{
				SpitError("Time signature doesn't even contain a semicolon and a slash!\n"
						"This is probably a bug, please report to Epoch.");

				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
				MemBus_Write(TmpBuf, true);
				return;
		}
	}
	else if (BusDataIs(MEMBUS_CODE_ABORTHALT))
	{
		char MsgBuf[MAX_LINE_SIZE];
		char Hr[16], Min[16];
			
		snprintf(Hr, 16, (HaltParams.TargetHour >= 10) ? "%ld" : "0%ld", HaltParams.TargetHour);
		snprintf(Min, 16, (HaltParams.TargetMin >= 10) ? "%ld" : "0%ld", HaltParams.TargetMin);
		
		if (HaltParams.HaltMode != -1)
		{
			HaltParams.HaltMode = -1; /*-1 does the real cancellation.*/
		}
		else
		{ /*Nothing scheduled?*/
			MemBus_Write(MEMBUS_CODE_FAILURE " " MEMBUS_CODE_ABORTHALT, true);
			return;
		}
		
		snprintf(MsgBuf, sizeof MsgBuf, "%s %s:%s %ld/%ld/%ld %s", "The shutdown scheduled for",
				Hr, Min, HaltParams.TargetMonth, HaltParams.TargetDay, HaltParams.TargetYear,
				"has been aborted.");

		EmulWall(MsgBuf, false);

		MemBus_Write(MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_ABORTHALT, true);
		return;
	}
	/*Ctrl-Alt-Del control.*/
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
	else if (BusDataIs(MEMBUS_CODE_SENDPID))
	{
		char TmpBuf[MEMBUS_SIZE/2 - 1];
		unsigned long LOffset = strlen(MEMBUS_CODE_SENDPID " ");
		const char *TWorker = BusData + LOffset;
		const ObjTable *TmpObj = NULL;
		
		if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
		{ /*No argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);
	
			return;
		}
		
		if (!(TmpObj = LookupObjectInTable(TWorker)) || !TmpObj->Started)
		{ /*Bad argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
			MemBus_Write(TmpBuf, true);
			
			return;
		}
		
		snprintf(TmpBuf, sizeof TmpBuf, "%s %s %lu", MEMBUS_CODE_SENDPID, TWorker,
				(TmpObj->Opts.HasPIDFile ? ReadPIDFile(TmpObj) : TmpObj->ObjectPID));
		MemBus_Write(TmpBuf, true);
	}
	else if (BusDataIs(MEMBUS_CODE_KILLOBJ) || BusDataIs(MEMBUS_CODE_OBJRELOAD))
	{
		char TmpBuf[MEMBUS_SIZE/2 - 1];
		unsigned long LOffset = (BusDataIs(MEMBUS_CODE_KILLOBJ) ? strlen(MEMBUS_CODE_KILLOBJ " ")
								: strlen(MEMBUS_CODE_OBJRELOAD " "));
		const char *TWorker = BusData + LOffset;
		ObjTable *TmpObj = NULL;
		
		if (LOffset >= strlen(BusData) || BusData[LOffset] == ' ')
		{ /*No argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(TmpBuf, true);
			
			return;
		}
		
		if (!(TmpObj = LookupObjectInTable(TWorker)) || !TmpObj->Started)
		{ /*Bad argument?*/
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
			MemBus_Write(TmpBuf, true);
			
			return;
		}
		
		if (BusDataIs(MEMBUS_CODE_KILLOBJ))
		{
			/*Attempt to send SIGKILL to the PID.*/
			if (!TmpObj->ObjectPID || 
				kill((TmpObj->Opts.HasPIDFile ? ReadPIDFile(TmpObj) : TmpObj->ObjectPID), SIGKILL) != 0)
			{
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
			}
			else
			{
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, BusData);
				TmpObj->Started = false; /*Mark it as stopped now that it's dead.*/
				TmpObj->ObjectPID = 0; /*Erase the PID.*/
				TmpObj->StartedSince = 0;
			}
			MemBus_Write(TmpBuf, true);
		}
		else
		{
			rStatus RV = SUCCESS;
			const char *MCode = MEMBUS_CODE_ACKNOWLEDGED, *RMsg = NULL;
			char LogOut[MAX_LINE_SIZE];
			
			if (TmpObj->ObjectReloadCommand[0] == 0)
			{
				snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
				MemBus_Write(TmpBuf, true);
				return;
			}
			
			RV = ProcessReloadCommand(TmpObj, false);
			
			switch (RV)
			{
				case SUCCESS:
					MCode = MEMBUS_CODE_ACKNOWLEDGED;
					RMsg = "succeeded";
					break;
				case WARNING:
					MCode = MEMBUS_CODE_WARNING;
					RMsg = "succeeded with a warning";
					break;
				case FAILURE:
					MCode = MEMBUS_CODE_FAILURE;
					RMsg = "failed";
					break;
				default:
					break;
			}
			
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MCode, BusData);
			
			MemBus_Write(TmpBuf, true);
			
			snprintf(LogOut, MAX_LINE_SIZE, "Reload of object %s %s.", TWorker, RMsg);
			WriteLogLine(LogOut, true);
		}
	}
	else if (BusDataIs(MEMBUS_CODE_RXD))
	{ /*Restart Epoch from disk, but saves object states and whatnot.
		* Done mainly so we can unmount the filesystem after someone updates /sbin/epoch.*/
		ReexecuteEpoch();
	}
	/*Something we don't understand. Send BADPARAM.*/
	else
	{
		char TmpBuf[MEMBUS_SIZE/2 - 1];
		
		snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
		
		MemBus_Write(TmpBuf, true);
	}
}

rStatus ShutdownMemBus(Bool ServerSide)
{	
	if (!BusRunning || !MemData)
	{
		return SUCCESS;
	}
	
	*(MemData + (MEMBUS_SIZE/2)) = MEMBUS_NOMSG;
	
	if (ServerSide)
	{
		*MemData = MEMBUS_NOMSG;
	
		if (shmctl(MemDescriptor, IPC_RMID, NULL) == -1)
		{
			SpitWarning("ShutdownMemBus(): Unable to deallocate membus.");
			return FAILURE;
		}
	}
	
	if (shmdt((void*)MemData) != 0)
	{
		SpitWarning("ShutdownMemBus(): Unable to detach membus.");
		return FAILURE;
	}
	
	BusRunning = false;
	return SUCCESS;
}
