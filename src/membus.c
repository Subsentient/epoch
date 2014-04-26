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
#include <time.h>
#include "epoch.h"

/*Memory bus uhh, static globals.*/

struct _MemBusInterface MemBus;

Bool BusRunning;
signed long MemBusKey = MEMKEY;
int MemDescriptor;

rStatus InitMemBus(Bool ServerSide)
{ /*Fire up the memory bus.*/
	char CheckCode = 0;
	unsigned long Inc = 0;

	if (BusRunning) return SUCCESS;
	
	memset(&MemBus, 0, sizeof(struct _MemBusInterface));
	
	if ((MemDescriptor = shmget((key_t)MemBusKey, MEMBUS_SIZE + sizeof(long) * 2, (ServerSide ? (IPC_CREAT | 0660) : 0660))) < 0)
	{
		if (ServerSide) SpitError("InitMemBus(): Failed to allocate memory bus."); /*should probably use perror*/
		else SpitError("InitMemBus(): Failed to connect to memory bus. Permissions?");
		return FAILURE;
	}
	
	if ((MemBus.Root = shmat(MemDescriptor, NULL, 0)) == (void*)-1)
	{
		SpitError("InitMemBus(): Failed to attach memory bus to MemBus.Root");
		memset(&MemBus, 0, sizeof(struct _MemBusInterface));
		return FAILURE;
	}
	
	/*Status.*/
	MemBus.LockPID = MemBus.Root;
	MemBus.LockTime = (unsigned long*) ((char*)MemBus.Root + sizeof(long));
	
	/*Server side.*/
	MemBus.Server.Status = (unsigned char*)MemBus.Root + sizeof(long) * 2;
	MemBus.Server.BinMessage = MemBus.Server.Status + 1;
	MemBus.Server.Message = (char*)MemBus.Server.BinMessage;
	
	/*Client side.*/
	MemBus.Client.Status = (unsigned char*)MemBus.Root + sizeof(long) * 2 + MEMBUS_SIZE/2;
	MemBus.Client.BinMessage = MemBus.Client.Status + 1;
	MemBus.Client.Message = (char*)MemBus.Client.BinMessage;
	
	if (ServerSide) /*Don't nuke messages on startup if we aren't init.*/
	{
		memset((void*)MemBus.Root, 0, MEMBUS_SIZE); /*Zero it out just to be neat. Probably don't really need this.*/
		
		*MemBus.Server.Status = MEMBUS_NOMSG; /*Set to no message by default.*/
	}
	else
	{ /*Client side stuff.*/
		for (; *MemBus.Server.Status != MEMBUS_NOMSG && *MemBus.Server.Status != MEMBUS_MSG; ++Inc)
		{ /*Wait for server-side to finish setting up it's half, if it was just starting up itself.*/
			if (Inc == 100000) /*Ten secs.*/
			{
				SmallError("Cannot connect to Epoch over MemBus, stream corrupted. Aborting MemBus initialization.");
				BusRunning = false;
				memset(&MemBus, 0, sizeof(struct _MemBusInterface));
				
				return FAILURE;
			}
			
			usleep(100);
		}
		
		/*Check the lock.*/
		if (*MemBus.LockPID != 0 && *MemBus.LockPID != getpid())
		{
			SmallError("Another client is currently connected to the membus. Cannot continue!");
			BusRunning = false;
			memset(&MemBus, 0, sizeof(struct _MemBusInterface));

			return FAILURE;
		}
		
		CheckCode = *MemBus.Server.Status = (*MemBus.Server.Status == MEMBUS_MSG ? MEMBUS_CHECKALIVE_MSG : MEMBUS_CHECKALIVE_NOMSG); /*Ask server-side if they're alive.*/
		
		for (Inc = 0; *MemBus.Server.Status == CheckCode; ++Inc)
		{ /*Wait ten seconds for server-side to respond.*/
			if (Inc == 100000)
			{ /*Ten seconds.*/
				SmallError("Cannot connect to Epoch over MemBus, timeout expired. Aborting MemBus initialization.");
				
				BusRunning = false;
				memset(&MemBus, 0, sizeof(struct _MemBusInterface));

				return FAILURE;
			}
			
			usleep(100);
		}
		
		/*Acquire the lock.*/
		*MemBus.LockPID = getpid();
		*MemBus.LockTime = time(NULL);

		*MemBus.Client.Status = MEMBUS_NOMSG;
	}
	/*Either the server side is alive, or we ARE the server side.*/
	BusRunning = true;
	
	return SUCCESS;
}

unsigned long MemBus_BinWrite(const void *InStream_, unsigned long DataSize, Bool ServerSide)
{ /*Copies binary data of length DataSize to the membus.*/
	const char *InStream = InStream_;
	unsigned char *BusData = NULL, *BusStatus = NULL;
	unsigned long Inc = 0;
	unsigned short WaitCount = 0;
	
	if (ServerSide)
	{
		BusStatus = MemBus.Client.Status;
	}
	else
	{
		BusStatus = MemBus.Server.Status;
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
	
	for (; Inc < DataSize && Inc < MEMBUS_MSGSIZE; ++Inc)
	{
		BusData[Inc] = InStream[Inc];
	}
	
	*BusStatus = MEMBUS_MSG;
	
	return Inc; /*Return number of bytes written.*/
}

unsigned long MemBus_BinRead(void *OutStream_, unsigned long MaxOutSize, Bool ServerSide)
{
	unsigned char *BusStatus = NULL, *BusData = NULL;
	unsigned char *OutStream = OutStream_;
	unsigned long Inc = 0;
	
	if (ServerSide)
	{
		BusStatus = MemBus.Server.Status;
	}
	else
	{
		BusStatus = MemBus.Client.Status;
	}
	
	BusData = BusStatus + 1;
	
	if (*BusStatus != MEMBUS_MSG)
	{
		return 0;
	}
	
	for (; Inc < MaxOutSize && Inc < MEMBUS_MSGSIZE; ++Inc)
	{
		OutStream[Inc] = BusData[Inc];
	}
	
	*BusStatus = MEMBUS_NOMSG;
	
	return Inc;
}
	
rStatus MemBus_Write(const char *InStream, Bool ServerSide)
{
	unsigned char *BusStatus = NULL;
	char *BusData = NULL;
	unsigned short WaitCount = 0;
	
	if (ServerSide)
	{
		BusStatus = MemBus.Client.Status; /*This isn't a typo, we write to the opposite side.*/
	}
	else
	{
		BusStatus = MemBus.Server.Status;
	}
	
	BusData = (char*)BusStatus + 1; /*Our actual data goes one byte after the status byte.*/
	
	while (*BusStatus != MEMBUS_NOMSG) /*Wait for them to finish eating their last message.*/
	{
		usleep(1000); /*0.001 seconds.*/
		++WaitCount;
		
		if (WaitCount == 10000)
		{ /*Been 10 seconds! Does it take that long to copy a string?*/
			return FAILURE;
		}
	}
	
	snprintf((char*)BusData, MEMBUS_MSGSIZE, "%s", InStream);
	
	*BusStatus = MEMBUS_MSG; /*Now we sent it.*/
	
	return SUCCESS;
}

Bool MemBus_Read(char *OutStream, Bool ServerSide)
{
	unsigned char *BusStatus = NULL;
	char *BusData = NULL;
	
	if (ServerSide)
	{
		BusStatus = MemBus.Server.Status;
	}
	else
	{
		BusStatus = MemBus.Client.Status;
	}
	
	BusData = (char*)BusStatus + 1;
		
	if (*BusStatus != MEMBUS_MSG)
	{ /*No data? Quit.*/
		return false;
	}
	
	snprintf(OutStream, MEMBUS_MSGSIZE, "%s", BusData);
	
	*BusStatus = MEMBUS_NOMSG; /*Set back to NOMSG once we got the message.*/

	return true;
}

Bool HandleMemBusPings(void)
{ /*If we are pinged, we must initialize the client side immediately.*/
	if (!BusRunning) return false;

	switch (*MemBus.Server.Status)
	{
		case MEMBUS_CHECKALIVE_MSG:
			*MemBus.Server.Status = MEMBUS_MSG;
			return true;
			break;
		case MEMBUS_CHECKALIVE_NOMSG:
			*MemBus.Server.Status = MEMBUS_NOMSG;
			return true;
			break;
		default:
			break;
	}
	
	return false;
}

Bool CheckMemBusIntegrity(void)
{
	if (!BusRunning) return true;
	
	if (*MemBus.LockPID == 0) return true;
	
	if (*MemBus.LockTime + 60 < time(NULL))
	{ /*Anything after a minute needs to be disconnected.*/
		*MemBus.Server.Status = MEMBUS_NOMSG;
		*MemBus.Server.Message = '\0';
		*MemBus.Client.Status = MEMBUS_NOMSG;
		*MemBus.Client.Message = '\0';
		*MemBus.LockTime = 0;
		*MemBus.LockPID = 0;
		return false;
	}
	
	return true;	
}
	
void ParseMemBus(void)
{ /*This function handles EVERYTHING passed to us via membus. It's truly vast.*/
#define BusDataIs(x) !strncmp(x, BusData, strlen(x))
	char BusData[MEMBUS_MSGSIZE];

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
		char TmpBuf[MEMBUS_MSGSIZE], *MCode = MEMBUS_CODE_FAILURE;
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
	else if (BusDataIs(MEMBUS_CODE_LSOBJS))
	{ /*Done for mostly third party stuff.*/
		char OutBuf[MEMBUS_MSGSIZE];
		unsigned char *BinWorker = (void*)OutBuf;
		ObjTable *Worker = ObjectTable;
		unsigned long TPID = 0;
		const unsigned long Length = strlen(MEMBUS_CODE_LSOBJS " " MEMBUS_LSOBJS_VERSION) + 1;
		
		for (; Worker->Next; Worker = Worker->Next)
		{
			const struct _RLTree *RLWorker = Worker->ObjectRunlevels;
			
			
			if (strlen(BusData) > strlen(MEMBUS_CODE_LSOBJS) &&
				strcmp(BusData + strlen(MEMBUS_CODE_LSOBJS " "), Worker->ObjectID) != 0)
			{ /*Allow for getting status of just one object.*/
					continue;
			}
			
			if (!Worker->Opts.HasPIDFile || !(TPID = ReadPIDFile(Worker)))
			{
				TPID = Worker->ObjectPID;
			}
			
			/*We need a version for this protocol, because relevant options can change with updates.
			 * Not all options are here, because some are not really useful.*/
						
			strncpy(OutBuf, MEMBUS_CODE_LSOBJS " " MEMBUS_LSOBJS_VERSION, Length);
			
			BinWorker = (unsigned char*)OutBuf + Length;
			
			*BinWorker++ = (Worker->Started && !Worker->Opts.HaltCmdOnly);
			*BinWorker++ = ObjectProcessRunning(Worker);
			*BinWorker++ = Worker->Enabled;
			*BinWorker++ = Worker->TermSignal;
			*BinWorker++ = Worker->ReloadCommandSignal;
			
			memcpy(BinWorker, &Worker->UserID, sizeof(long));
			memcpy((BinWorker += sizeof(long)), &Worker->GroupID, sizeof(long));
			
			memcpy((BinWorker += sizeof(long)), &Worker->Opts.StopMode, sizeof(enum _StopMode));
			memcpy((BinWorker += sizeof(enum _StopMode)), &TPID, sizeof(long));
			
			memcpy((BinWorker += sizeof(long)), &Worker->StartedSince, sizeof(long));
			memcpy(BinWorker + sizeof(long), &Worker->Opts.StopTimeout, sizeof(long));
			
			
			MemBus_BinWrite(OutBuf, MEMBUS_MSGSIZE, true);
			
			snprintf(OutBuf, sizeof OutBuf, "%s %s", Worker->ObjectID, Worker->ObjectDescription);
			
			MemBus_Write(OutBuf, true);
			
			*OutBuf = '\0';
			
			BinWorker = (void*)OutBuf;
			/*Write it over binary now.*/
			if (Worker->Opts.RawDescription) *BinWorker++ = COPT_RAWDESCRIPTION;
			if (Worker->Opts.HaltCmdOnly) *BinWorker++ = COPT_HALTONLY;
			if (Worker->Opts.Persistent) *BinWorker++ = COPT_PERSISTENT;
			if (Worker->Opts.Fork) *BinWorker++ = COPT_FORK;
			if (Worker->Opts.IsService) *BinWorker++ = COPT_SERVICE;
			if (Worker->Opts.AutoRestart) *BinWorker++ = COPT_AUTORESTART;
			if (Worker->Opts.ForceShell) *BinWorker++ = COPT_FORCESHELL;
			if (Worker->Opts.NoStopWait) *BinWorker++ = COPT_NOSTOPWAIT;
			if (Worker->Opts.Exec) *BinWorker++ = COPT_EXEC;
			if (Worker->Opts.PivotRoot) *BinWorker++ = COPT_PIVOTROOT;
			if (Worker->Opts.RunOnce) *BinWorker++ = COPT_RUNONCE;
			*BinWorker = 0;
			
			MemBus_BinWrite(OutBuf, MEMBUS_MSGSIZE, true);
			
			if (RLWorker)
			{
				for (; RLWorker->Next; RLWorker = RLWorker->Next)
				{ /*Send all runlevels.*/
					snprintf(OutBuf, sizeof OutBuf, "%s %s %s %s", MEMBUS_CODE_LSOBJS,
							MEMBUS_LSOBJS_VERSION, Worker->ObjectID, RLWorker->RL);

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
		char TmpBuf[MEMBUS_MSGSIZE];
		
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
		char TmpBuf[MEMBUS_MSGSIZE];
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
		DidWork = EditConfigValue(CurObj->ConfigFile, TWorker, "ObjectEnabled", EnablingThis ? "true" : "false");
		
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
		char TmpBuf[MEMBUS_MSGSIZE];
		
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
		char *Worker = NULL;
		char ObjectID[MAX_DESCRIPT_SIZE], SpecRunlevel[MAX_DESCRIPT_SIZE];
		enum ObjRLS { OBJRLS_CHECK, OBJRLS_ADD, OBJRLS_DEL } Mode;
		int LOffset = 0, Inc = 0;
		char OutBuf[MEMBUS_MSGSIZE] = { '\0' };
		ObjTable *CurObj = NULL;
		char *RunlevelText = NULL;
		unsigned long RequiredRLTLength = 0;
		struct _RLTree *RLWorker = NULL;
		
		if (BusDataIs(MEMBUS_CODE_OBJRLS_CHECK)) LOffset = sizeof MEMBUS_CODE_OBJRLS_CHECK " " - 1, Mode = OBJRLS_CHECK;
		else if (BusDataIs(MEMBUS_CODE_OBJRLS_ADD)) LOffset = sizeof MEMBUS_CODE_OBJRLS_ADD " " - 1, Mode = OBJRLS_ADD;
		else if (BusDataIs(MEMBUS_CODE_OBJRLS_DEL)) LOffset = sizeof MEMBUS_CODE_OBJRLS_DEL " " - 1, Mode = OBJRLS_DEL;
		
		Worker = BusData + LOffset;
		
		for (; Worker[Inc] != ' ' && Worker[Inc] != '\0' && Inc < sizeof ObjectID - 1; ++Inc) 
		{ /*Get the object ID.*/
			ObjectID[Inc] = Worker[Inc];
		}
		ObjectID[Inc] = '\0';
		
		if ((Worker += Inc) == '\0')
		{ /*Malformed.*/
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
			MemBus_Write(OutBuf, true);
			return;
		}
		++Worker; /*Past the space.*/
		
		for (Inc = 0; Worker[Inc] != '\0' && Inc < sizeof SpecRunlevel - 1; ++Inc) 
		{ /*Get the runlevel.*/
			SpecRunlevel[Inc] = Worker[Inc];
		}
		SpecRunlevel[Inc] = '\0';
		
		if (!(CurObj = LookupObjectInTable(ObjectID)))
		{ /*Object sent to us doesn't exist.*/
			snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
			MemBus_Write(OutBuf, true);
			return;
		}
		
		/*Now process the commands.*/
		switch (Mode)
		{
			case OBJRLS_CHECK:
				snprintf(OutBuf, sizeof OutBuf, MEMBUS_CODE_OBJRLS_CHECK " %s %s %d",
						ObjectID, SpecRunlevel, ObjRL_CheckRunlevel(SpecRunlevel, CurObj, true));
				MemBus_Write(OutBuf, true);
				return;
			case OBJRLS_ADD:
				/*Add the runlevel in memory.*/
				if (!ObjRL_CheckRunlevel(SpecRunlevel, CurObj, false))
				{
					ObjRL_AddRunlevel(SpecRunlevel, CurObj);
				}
				else
				{
					snprintf(OutBuf, sizeof OutBuf, MEMBUS_CODE_FAILURE " %s", BusData);
					MemBus_Write(OutBuf, true);
				}
				break;
			case OBJRLS_DEL:
				if (!ObjRL_DelRunlevel(SpecRunlevel, CurObj))
				{
					snprintf(OutBuf, sizeof OutBuf, "%s %s", MEMBUS_CODE_FAILURE, BusData);
					MemBus_Write(OutBuf, true);
					return;
				}
				break;
			default:
				break;
		}
		
		/*File editing. We already returned if we were just checking, so I won't handle that here.*/
		if (CurObj->ObjectRunlevels)
		{
			for (RLWorker = CurObj->ObjectRunlevels; RLWorker->Next; RLWorker = RLWorker->Next)
			{
				RequiredRLTLength += strlen(RLWorker->RL) + 2;
			}
			++RequiredRLTLength; /*For the null terminator.*/
			
			*(RunlevelText = malloc(RequiredRLTLength)) = '\0'; /*I'm only going to say this once.
			* This is a Linux program. In Linux programs, most of the time, even if something is wrong,
			* malloc will NEVER return NULL. I will not dirty up my code with a hundred thousand
			* checks for a value that will never come to pass.*/
			
			for (RLWorker = CurObj->ObjectRunlevels; RLWorker->Next; RLWorker = RLWorker->Next)
			{
				strncat(RunlevelText, RLWorker->RL, 1);
				strncat(RunlevelText, " ", 1);
			}
			
			/*Get rid of the space at the end.*/
			RunlevelText[strlen(RunlevelText) - 1] = '\0';
		}
		
		if (Mode == OBJRLS_ADD)
		{
			if (!EditConfigValue(CurObj->ConfigFile, CurObj->ObjectID, "ObjectRunlevels", RunlevelText))
			{
				const int Length = MAX_LINE_SIZE + strlen(RunlevelText);
				char *TrickyBuf = malloc(Length);
				/*Special trick to attempt to add the ObjectRunlevels attribute.*/
				snprintf(TrickyBuf, Length, "%s\n\tObjectRunlevels=%s",
						CurObj->Enabled ? "true" : "false", RunlevelText);
						
				if (!EditConfigValue(CurObj->ConfigFile, CurObj->ObjectID, "ObjectEnabled", TrickyBuf))
				{ /*Darn, we can't even do it the sneaky way!*/
					free(TrickyBuf);
					free(RunlevelText);
					snprintf(OutBuf, sizeof OutBuf, MEMBUS_CODE_FAILURE " %s", BusData);
					MemBus_Write(OutBuf, true);
					return;
				}
				free(TrickyBuf);
			}
			
		}
		else
		{ /*OBJRLS_DEL*/
			/*Delete the line.*/
			if (!EditConfigValue(CurObj->ConfigFile, CurObj->ObjectID, "ObjectRunlevels",
								CurObj->ObjectRunlevels ? RunlevelText : NULL))
			{ /*If we pass NULL to EditConfigValue(), it means we want to DELETE that line.*/
				if (RunlevelText) free(RunlevelText);
				snprintf(OutBuf, sizeof OutBuf, MEMBUS_CODE_FAILURE " %s", BusData);
				MemBus_Write(OutBuf, true);
				return;
			}
		
		}
		
		if (RunlevelText) free(RunlevelText), RunlevelText = NULL;
		
		snprintf(OutBuf, sizeof OutBuf, MEMBUS_CODE_ACKNOWLEDGED " %s", BusData);
		MemBus_Write(OutBuf, true);
		return;
	}
	/*Power functions that close everything first.*/
	else if (BusDataIs(MEMBUS_CODE_HALT) || BusDataIs(MEMBUS_CODE_POWEROFF) || BusDataIs(MEMBUS_CODE_REBOOT))
	{
	unsigned long LOffset = 0, Signal = OSCTL_LINUX_REBOOT;
		char *TWorker = NULL, *MSig = NULL;
		char TmpBuf[MEMBUS_MSGSIZE];
		
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
			
			while (!MemBus_Read(TmpBuf, true)) usleep(100); /*Wait to be told they received it.*/
			
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
			
			++HaltParams.JobID;
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
		char TmpBuf[MEMBUS_MSGSIZE];
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
		char TmpBuf[MEMBUS_MSGSIZE];
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
			
			if (TmpObj->ObjectReloadCommand == NULL && TmpObj->ReloadCommandSignal == 0)
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
		
		/**We set this so when we come back we'll know if we are doing a regular reexec.**/
		setenv("EPOCHRXDMEMBUS", "1", true);
		
		ReexecuteEpoch();
	}
	/*Something we don't understand. Send BADPARAM.*/
	else
	{
		char TmpBuf[MEMBUS_MSGSIZE];
		
		snprintf(TmpBuf, sizeof TmpBuf, "%s %s", MEMBUS_CODE_BADPARAM, BusData);
		
		MemBus_Write(TmpBuf, true);
	}
}

rStatus ShutdownMemBus(Bool ServerSide)
{	
	if (!BusRunning || !MemBus.Root)
	{
		return SUCCESS;
	}
	
	*MemBus.Client.Status = MEMBUS_NOMSG;
	
	if (ServerSide)
	{
		*MemBus.Server.Status = MEMBUS_NOMSG;
	
		if (shmctl(MemDescriptor, IPC_RMID, NULL) == -1)
		{
			SpitWarning("ShutdownMemBus(): Unable to deallocate membus.");
			return FAILURE;
		}
	}
	else
	{ /*Release the client lock.*/
		*MemBus.LockPID = 0;
		*MemBus.LockTime = 0;
	}
	
	if (shmdt(MemBus.Root) != 0)
	{
		SpitWarning("ShutdownMemBus(): Unable to detach membus.");
		return FAILURE;
	}
	
	BusRunning = false;
	return SUCCESS;
}
