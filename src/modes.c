/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file holds functions for the various things we can do, depending on argv[0].
 * Most are to be called by main() at some point or another.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <dirent.h>
#include "epoch.h"

rStatus SendPowerControl(const char *MembusCode)
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
		SpitError("Invalid MEMBUS_CODE passed to SendPowerControl().");
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

rStatus ObjControl(const char *ObjectID, const char *MemBusSignal)
{ /*Start and stop or disable services.*/
	char RemoteResponse[MEMBUS_SIZE/2 - 1];
	char OutMsg[MEMBUS_SIZE/2 - 1];
	char PossibleResponses[4][MEMBUS_SIZE/2 - 1];
	unsigned long cCounter = 0;
	
	snprintf(OutMsg, sizeof OutMsg, "%s %s", MemBusSignal, ObjectID);
	
	if (!MemBus_Write(OutMsg, false))
	{
		return FAILURE;
	}
	
	while (!MemBus_Read(RemoteResponse, false))
	{
		usleep(100000); /*0.1 secs*/
		++cCounter;
		
		if (cCounter == 200)
		{ /*Extra newline because we probably are going to print in a progress report.*/
			SpitError("\nFailed to get reply via membus.");
			return FAILURE;
		}
	}
	
	snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s %s",
		MEMBUS_CODE_ACKNOWLEDGED, MemBusSignal, ObjectID);
		
	snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s %s",
		MEMBUS_CODE_FAILURE, MemBusSignal, ObjectID);
		
	snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s",
		MEMBUS_CODE_BADPARAM, OutMsg);

	snprintf(PossibleResponses[3], sizeof PossibleResponses[3], "%s %s %s",
		MEMBUS_CODE_WARNING, MemBusSignal, ObjectID);
		
	if (!strcmp(RemoteResponse, PossibleResponses[0]))
	{
		return SUCCESS;
	}
	else if (!strcmp(RemoteResponse, PossibleResponses[1]))
	{
		return FAILURE;
	}
	else if (!strcmp(RemoteResponse, PossibleResponses[3]))
	{
		return WARNING;
	}
	else if (!strcmp(RemoteResponse, PossibleResponses[2]))
	{
		SpitError("\nWe are being told that we sent a bad parameter.");
		return FAILURE;
	}
	else
	{
		SpitError("\nReceived invalid reply from membus.");
		return FAILURE;
	}
}

Bool AskObjectStarted(const char *ObjectID)
{ /*Just request if the object is running or not.*/
	char RemoteResponse[MEMBUS_SIZE/2 - 1];
	char OutMsg[MEMBUS_SIZE/2 - 1];
	char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
	unsigned long cCounter = 0;
	
	snprintf(OutMsg, sizeof OutMsg, "%s %s", MEMBUS_CODE_STATUS, ObjectID);
	
	if (!MemBus_Write(OutMsg, false))
	{
		return -1; /*Yes, you can do that with the Bool type. It's just a char.*/
	}
	
	while (!MemBus_Read(RemoteResponse, false))
	{ /*Third function, I really am just copying and pasting this part.
		* Hey, this is exactly what I was going to write anyways! */
		usleep(100000); /*0.1 secs*/
		++cCounter;
		
		if (cCounter == 200)
		{ /*Extra newline because we probably are going to print in a progress report.*/
			SpitError("\nFailed to get reply via membus.");
			return -1;
		}
	}
	
	snprintf(PossibleResponses[0], sizeof PossibleResponses[0], "%s %s %s",
		MEMBUS_CODE_STATUS, ObjectID, "0");
	snprintf(PossibleResponses[1], sizeof PossibleResponses[1], "%s %s %s",
		MEMBUS_CODE_STATUS, ObjectID, "1");
	snprintf(PossibleResponses[2], sizeof PossibleResponses[2], "%s %s %s",
		MEMBUS_CODE_FAILURE, MEMBUS_CODE_STATUS, ObjectID);
		
	if (!strcmp(RemoteResponse, PossibleResponses[0]))
	{
		return false;
	}
	else if (!strcmp(RemoteResponse, PossibleResponses[1]))
	{
		return true;
	}
	else if (!strcmp(RemoteResponse, PossibleResponses[2]))
	{ /*Not sure why I do this when the statement below returns the same.*/
		return -1;
	}
	else
	{
		return -1;
	}
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

void EmulWall(const char *InStream, Bool ShowUser)
{ /*We not only use this as a CLI applet, we use it to notify of impending shutdown too.*/
	FILE *Descriptor = NULL;
	char OutBuf[8192];
	char HMS[3][16];
	char MDY[3][16];
	char OurUser[256];
	char OurHostname[256];
	char FName[128] = "/dev/tty1";
	unsigned long Inc = 0;
	
	if (getuid() != 0)
	{ /*Not root?*/
		SpitWarning("You are not root. Only sending to ttys you have privileges on.");
	}
	
	GetCurrentTime(HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2]);
	
	snprintf(OutBuf, 64, "\007\n%s[%s:%s:%s | %s/%s/%s]%s ", CONSOLE_COLOR_RED, HMS[0], HMS[1], HMS[2],
		MDY[0], MDY[1], MDY[2], CONSOLE_ENDCOLOR);
	
	if (ShowUser)
	{
		if (getlogin_r(OurUser, sizeof OurUser) != 0)
		{
			snprintf(OurUser, sizeof OurUser, "%s", "(unknown)");
		}
		
		if (gethostname(OurHostname, sizeof OurHostname) != 0)
		{
			snprintf(OurHostname, sizeof OurHostname, "%s", "(unknown)");
		}
		
		/*I really enjoy pulling stuff off like the line below.*/
		snprintf(&OutBuf[strlen(OutBuf)], sizeof OutBuf - strlen(OutBuf), "Broadcast message from %s@%s: ", OurUser, OurHostname);
		
	}
	else
	{
		snprintf(&OutBuf[strlen(OutBuf)], sizeof OutBuf - strlen(OutBuf), "%s", "Broadcast message: ");
	}
	
	snprintf(&OutBuf[strlen(OutBuf)], sizeof OutBuf - strlen(OutBuf), "\n%s\n\n", InStream);
	
	/*Now write to the ttys.*/
	for (Inc = 2; (Descriptor = fopen(FName, "r")); ++Inc) /*See, we use fopen() as a way to check if the file exists.*/
	{ /*Your eyes bleeding yet?*/
		fclose(Descriptor);
		
		Descriptor = fopen(FName, "w");
		fwrite(OutBuf, 1, strlen(OutBuf), Descriptor);
		fflush(NULL);
		fclose(Descriptor);
		Descriptor = NULL;
		
		snprintf(FName, sizeof FName, "/dev/tty%lu", Inc);
	}
	
	snprintf(FName, sizeof FName, "%s", "/dev/pts/0");
	
	for (Inc = 1; (Descriptor = fopen(FName, "r")); ++Inc)
	{
		fclose(Descriptor);
		
		Descriptor = fopen(FName, "w");
		fwrite(OutBuf, 1, strlen(OutBuf), Descriptor);
		fflush(NULL);
		fclose(Descriptor);
		
		Descriptor = NULL;
		
		snprintf(FName, sizeof FName, "/dev/pts/%lu", Inc);
	}
}

rStatus EmulShutdown(long ArgumentCount, const char **ArgStream)
{ /*Eyesore, but it works.*/
	const char **TPtr = ArgStream + 1; /*Skip past the equivalent of argv[0].*/
	unsigned long TargetHr = 0, TargetMin = 0;
	const char *THalt = NULL;
	char PossibleResponses[3][MEMBUS_SIZE/2 - 1];
	char TmpBuf[MEMBUS_SIZE/2 - 1], InRecv[MEMBUS_SIZE/2 - 1], TimeFormat[32];
	short Inc = 0;
	short TimeIsSet = 0, HaltModeSet = 0;
	Bool AbortingShutdown = false, ImmediateHalt = false;
	
	if (getuid() != 0)
	{
		fprintf(stderr, "%s", "Unable to comply with shutdown request. You are not root.\n");
		return FAILURE;
	}
	
	for (; Inc != (ArgumentCount - 1); ++TPtr, ++Inc)
	{
		if (!strcmp(*TPtr, "-h") || !strcmp(*TPtr, "--halt") || !strcmp(*TPtr, "-H"))
		{
			THalt = MEMBUS_CODE_HALT;
			++HaltModeSet;
			continue;
		}
		else if (!strcmp(*TPtr, "-R") || !strcmp(*TPtr, "-r") || !strcmp(*TPtr, "--reboot"))
		{
			THalt = MEMBUS_CODE_REBOOT;
			++HaltModeSet;
			continue;
		}
		else if (!strcmp(*TPtr, "-p") || !strcmp(*TPtr, "-P") || !strcmp(*TPtr, "--poweroff"))
		{
			THalt = MEMBUS_CODE_POWEROFF;
			++HaltModeSet;
			continue;
		}
		else if (!strcmp(*TPtr, "-c") || !strcmp(*TPtr, "--cancel"))
		{
			AbortingShutdown = true;
			snprintf(TmpBuf, sizeof TmpBuf, "%s", MEMBUS_CODE_ABORTHALT);
			break;
		}
		else if (strstr(*TPtr, ":") && **TPtr != '-')
		{
			struct _HaltParams TempParams;
			
			if (sscanf(*TPtr, "%lu:%lu", &TargetHr, &TargetMin) != 2)
			{
				puts("Bad time format. Please enter in the format of \"hh:mm\"");
				return FAILURE;
			}
			
			DateDiff(TargetHr, TargetMin, &TempParams.TargetMonth, &TempParams.TargetDay, &TempParams.TargetYear);
			
			snprintf(TimeFormat, sizeof TimeFormat, "%lu:%lu:%d %lu/%lu/%lu",
					TargetHr, TargetMin, 0, TempParams.TargetMonth, TempParams.TargetDay, TempParams.TargetYear);
					
			++TimeIsSet;
		}
		else if (**TPtr == '+' && isdigit(*(*TPtr + 1)))
		{
			struct _HaltParams TempParams;
			const char *TArg = *TPtr + 1; /*Targ manure!*/
			time_t TTime;
			struct tm *TimeP;
			
			MinsToDate(atoi(TArg), &TempParams.TargetHour, &TempParams.TargetMin, &TempParams.TargetMonth,
						&TempParams.TargetDay, &TempParams.TargetYear);
						
			time(&TTime); /*Get this for the second.*/
			TimeP = localtime(&TTime);
			
			snprintf(TimeFormat, sizeof TimeFormat, "%lu:%lu:%d %lu/%lu/%lu",
					TempParams.TargetHour, TempParams.TargetMin, TimeP->tm_sec, TempParams.TargetMonth,
					TempParams.TargetDay, TempParams.TargetYear);
					
			++TimeIsSet;
		}
		else if (!strcmp(*TPtr, "now"))
		{
			ImmediateHalt = true;
			++TimeIsSet;
		}
		else if (!strcmp(*TPtr, "--help"))
		{
			const char *HelpMsg =
			"Usage: shutdown -hrpc [12:00/+10/now] -c\n\n"
			"-h -H --halt: Halt the system, don't power down.\n"
			"-p -P --poweroff: Power down the system.\n"
			"-r -R --reboot: Reboot the system.\n"
			"-c --cancel: Cancel a pending shutdown.\n\n"
			"Specify time in hh:mm, +m, or \"now\".\n";
			
			puts(HelpMsg);
			return SUCCESS;
		}
			
		else
		{
			fprintf(stderr, "Invalid argument %s. See --help for usage.\n", *TPtr);
			return FAILURE;
		}

	}
	
	if (!AbortingShutdown)
	{
		if (HaltModeSet == 0)
		{
			fprintf(stderr, "%s\n", "You must specify one of -hrp.");
			return FAILURE;
		}
		
		if (HaltModeSet > 1)
		{
			fprintf(stderr, "%s\n", "Please specify only ONE of -hrp.");
			return FAILURE;
		}
		
		if (!TimeIsSet)
		{
			fprintf(stderr, "%s\n", "You must specify a time in the format of hh:mm: or +m.");
			return FAILURE;
		}
		
		if (TimeIsSet > 1)
		{
			fprintf(stderr, "%s\n", "Multiple time arguments specified. Please specify only one.");
			return FAILURE;
		}
		
		if (!ImmediateHalt)
		{
			snprintf(TmpBuf, sizeof TmpBuf, "%s %s", THalt, TimeFormat);
		}
	}
	
	if (ImmediateHalt)
	{
		snprintf(TmpBuf, sizeof TmpBuf, "%s", THalt);
	}
	
	snprintf(PossibleResponses[0], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_ACKNOWLEDGED, TmpBuf);
	snprintf(PossibleResponses[1], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_FAILURE, TmpBuf);
	snprintf(PossibleResponses[2], MEMBUS_SIZE/2 - 1, "%s %s", MEMBUS_CODE_BADPARAM, TmpBuf);
	
	if (!InitMemBus(false))
	{
		SpitError("Failed to connect to membus.");
		return FAILURE;
	}
	
	if (!MemBus_Write(TmpBuf, false))
	{
		SpitError("Failed to write to membus.");
		ShutdownMemBus(false);
		return FAILURE;
	}
	
	while (!MemBus_Read(InRecv, false)) usleep(1000); /*Wait for a response.*/
	
	if (!ShutdownMemBus(false))
	{
		SpitError("Failed to shut down membus! This could spell serious issues.");
	}
	
	if (!strcmp(InRecv, PossibleResponses[0]))
	{
		return SUCCESS;
	}
	else if (!strcmp(InRecv, PossibleResponses[1]))
	{
		if (!strcmp(TmpBuf, MEMBUS_CODE_ABORTHALT))
		{
			fprintf(stderr, "%s\n", "Failed to abort shutdown. Is a shutdown scheduled?");
		}
		else
		{
			fprintf(stderr, "%s\n", "Failed to schedule shutdown.\nIs another already scheduled? Use shutdown -c to cancel it.");
		}
		return FAILURE;
	}
	else if (!strcmp(InRecv, PossibleResponses[2]))
	{
		SpitError("We are being told that we sent a bad parameter over the membus!"
					"Please report this to Epoch, as it's likely a bug.");
		return FAILURE;
	}
	else
	{
		SpitError("Invalid response received from membus! Please report this to Epoch, as it's likely a bug.");
		return FAILURE;
	}
}
