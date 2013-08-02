/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file holds functions for the various things we can do, depending on argv[0].
 * Most are to be called by main() at some point or another.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "epoch.h"

rStatus TellInitToDo(const char *MembusCode)
{ /*Client side to send a request to halt/reboot/power off/disable or enable CAD/etc.*/
	char InitsResponse[MEMBUS_SIZE/2 - 1], *PCode[2], *PErrMsg;
	unsigned long cCounter = 0;
	
	if (!strcmp(MembusCode, MEMBUS_CODE_HALT))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_HALT;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_HALT;
		PErrMsg = "Unable to halt.";
	}
	else if (!strcmp(MembusCode, MEMBUS_CODE_POWEROFF))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_POWEROFF;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_POWEROFF;
		PErrMsg = "Unable to power off.";
	}
	else if (!strcmp(MembusCode, MEMBUS_CODE_REBOOT))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_REBOOT;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_REBOOT;
		PErrMsg = "Unable to reboot.";
	}
	else if (!strcmp(MembusCode, MEMBUS_CODE_CADON))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_CADON;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_CADON;
		PErrMsg = "Unable to enable CTRL-ALT-DEL instant reboot.";
	}
	else if (!strcmp(MembusCode, MEMBUS_CODE_CADOFF))
	{
		PCode[0] = MEMBUS_CODE_ACKNOWLEDGED " " MEMBUS_CODE_CADOFF;
		PCode[1] = MEMBUS_CODE_FAILURE " " MEMBUS_CODE_CADOFF;
		PErrMsg = "Unable to disable CTRL-ALT-DEL instant reboot.";
	}
	else
	{
		SpitError("Invalid MEMBUS_CODE passed to TellInitToDo().");
		return FAILURE;
	}
	
	if (!InitMemBus(false))
	{
		SpitError("Failed to connect to membus.");
		return FAILURE;
	}
	
	if (!MemBus_Write(MembusCode, false))
	{
		SpitError("Failed to write to membus.");
		return FAILURE;
	}
	
	while (!MemBus_Read(InitsResponse, false))
	{
		usleep(100000); /*0.1 secs.*/
		++cCounter;
		
		if (cCounter == 200) /*Twenty seconds.*/
		{
			SpitError("Failed to get reply via membus.");
			return FAILURE;
		}
	}
	
	if (!strcmp(InitsResponse, PCode[0]))
	{
		ShutdownMemBus(false);
		return SUCCESS;
	}
	else if (!strcmp(InitsResponse, PCode[1]))
	{ /*Nothing uses this right now.*/
		SpitError(PErrMsg);
		ShutdownMemBus(false);
		
		return FAILURE;
	}
	
	return SUCCESS;
}

rStatus EmulKillall5(unsigned long InSignal)
{ /*Used as the killall5 utility.*/
	DIR *ProcDir;
	struct dirent *CurDir;
	unsigned long CurPID, OurPID, Inc;
	FILE *TempDescriptor;
	char TmpBuf[1024], SessionID[8192], SessionID_Targ[8192], TChar;


	if (InSignal > OSCTL_SIGNAL_STOP || InSignal == 0) /*Won't be negative since we are unsigned.*/
	{
		SpitError("EmulKillall5() Bad value for unsigned long InSignal.");
	}
	
	OurPID = (unsigned long)getpid(); /*We need this so we don't kill ourselves.*/
	
	/*Get our Session ID in ASCII, because it's often too big to fit in a 32 bit integer.
	 * Anything in our session ID must live so that our shell lives.*/
	snprintf(TmpBuf, 1024, "/proc/%lu/sessionid", OurPID);
	
	if (!(TempDescriptor = fopen(TmpBuf, "r")))
	{
		SpitError("Failed to read session ID file for ourselves. Aborting.");
		return FAILURE;
	}
	
	for (Inc = 0; (TChar = getc(TempDescriptor)) != EOF && Inc < 8192; ++Inc)
	{
		SessionID[Inc] = TChar;
	}
	SessionID[Inc] = '\0';
	
	fclose(TempDescriptor);
	
	/*We get everything from /proc.*/
	ProcDir = opendir("/proc/");
	
	while ((CurDir = readdir(ProcDir)))
	{
		if (isdigit(CurDir->d_name[0]) && CurDir->d_type == 4)
		{			
			CurPID = atoi(CurDir->d_name); /*Convert the new PID to a true number.*/
			
			if (CurPID == 1 || CurPID == OurPID)
			{ /*Don't try to kill init, or us.*/
				continue;
			}
			
			snprintf(TmpBuf, 1024, "/proc/%lu/sessionid", CurPID);
			
			if (!(TempDescriptor = fopen(TmpBuf, "r")))
			{
				snprintf(TmpBuf, 1024, "Failed to read session ID file for process %lu. Aborting.", CurPID);
				
				SpitError(TmpBuf);
				return FAILURE;
			}
			
			/*Copy in the contents of the file sessionid, using EOF to know when to stop. NOW SHUT UP ABOUT THE LOOPS!
			 * I can't really do this with fread() because these files report zero length!*/
			for (Inc = 0; (TChar = getc(TempDescriptor)) != EOF && Inc < 8192; ++Inc)
			{
				SessionID_Targ[Inc] = TChar;
			}
			SessionID_Targ[Inc] = '\0';
			
			fclose(TempDescriptor);
			
			if (!strncmp(SessionID, SessionID_Targ, 8192))
			{ /*It's in our session ID, so don't touch it.*/
				continue;
			}
			
			/*We made it this far, must be safe to nuke this process.*/
			kill(CurPID, InSignal); /*Actually send the kill, stop, whatever signal.*/
		}
	}
	closedir(ProcDir);
	
	return SUCCESS;
}

