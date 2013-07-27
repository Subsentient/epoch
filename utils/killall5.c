/*This code is part of Mauri. Mauri is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**Version whatever. In development.**/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include "../mauri.h"

int main(int argc, char **argv)
{
	unsigned char InSignal;
	char *InArg;
	DIR *ProcDir;
	struct dirent *CurDir;
	unsigned long CurPID, OurPID, Inc;
	FILE *TempDescriptor;
	char TmpBuf[1024], SessionID[8192], SessionID_Targ[8192], TChar;
	
	
	if (argc > 2) /*Hmm, looks like we are being passed more than one argument.*/
	{
		SpitWarning("Please specify a signal number such as -9,\n"
						"or nothing to default to -15.");
		return 1;
	}
	else if (argc == 1) /*They are just running us with no arguments. Assume signal 15.*/
	{
		InSignal = 15;
	}
	else
	{ /*Parse the argument.*/
		if (!strncmp("-", argv[1], 1))
		{ /*If they used a dash for the argument..*/
			InArg = argv[1] + 1;
		}
		else
		{
			InArg = argv[1];
		}
		
		InSignal = atoi(InArg);
		
		if (InSignal > OSCTL_SIGNAL_STOP || InSignal <= 0)
		{
			SpitWarning("Bad argument provided. Please enter a valid signal number.");
			return 1;
		}
	}

	OurPID = getpid(); /*We need this so we don't kill ourselves.*/
	
	/*Get our Session ID in ASCII, because it's often too big to fit in a 32 bit integer.
	 * Anything in our session ID must live so that our shell lives.*/
	snprintf(TmpBuf, 1024, "/proc/%lu/sessionid", OurPID);
	
	if (!(TempDescriptor = fopen(TmpBuf, "r")))
	{
		SpitError("Failed to read session ID file for ourselves. Aborting.");
		return 1;
	}
	
	for (Inc = 0; (TChar = getc(TempDescriptor)) != EOF; ++Inc)
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
				SpitError("Failed to read session ID file for process %lu. Aborting.");
				return 1;
			}
			
			/*Copy in the contents of the file sessionid, using EOF to know when to stop. NOW SHUT UP ABOUT THE LOOPS!
			 * I can't really do this with fread() because these files report zero length!*/
			for (Inc = 0; (TChar = getc(TempDescriptor)) != EOF; ++Inc)
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
	
	return 0;
}
