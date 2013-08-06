/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include "epoch.h"

/*We store the current runlevel here.*/
char CurRunlevel[MAX_DESCRIPT_SIZE] = "default";

/*Function forward declarations.*/
static rStatus ExecuteConfigObject(ObjTable *InObj, Bool IsStartingMode);

static Bool FileUsable(const char *FileName)
{
	FILE *TS = fopen(FileName, "r");
	
	if (TS)
	{
		fclose(TS);
		return true;
	}
	else
	{
		fclose(TS);
		return false;
	}
}

static rStatus ExecuteConfigObject(ObjTable *InObj, Bool IsStartingMode)
{ /*Not making static because this is probably going to be useful for other stuff.*/
	pid_t LaunchPID;
	const char *CurCmd, *ShellPath = "sh"; /*We try not to use absolute paths here, because some distros don't have normal layouts,
											*And I'm sure they would rather see a warning than have it just botch up.*/
	rStatus ExitStatus = FAILURE; /*We failed unless we succeeded.*/
	Bool ShellDissolves;
	int RawExitStatus;
	
	CurCmd = (IsStartingMode ? InObj->ObjectStartCommand : InObj->ObjectStopCommand);
	
	/*Check how we should handle PIDs for each shell. In order to get the PID, exit status,
	* and support shell commands, we need to jump through a bunch of hoops.
	* PID killing support in this manner, is buggy. That's why we support PIDFILE.*/
	if (FileUsable("/bin/bash"))
	{
		ShellDissolves = true;
		ShellPath = "bash";
	}
	else if (FileUsable("/bin/dash"))
	{
		ShellPath = "dash";
		ShellDissolves = true;
	}
	else if (FileUsable("/bin/zsh"))
	{
		ShellPath = "zsh";
		ShellDissolves = true;
	}
	else if (FileUsable("/bin/csh"))
	{
		ShellPath = "csh";
		ShellDissolves = true;
	}
	else if (FileUsable("/bin/busybox"))
	{ /*This is one of those weird shells that still does the old practice of creating a child for -c.
		* We can deal with the likes of them. Small chance that for shells like this, another PID could jump in front
		* and we could end up storing the wrong one. Very small, but possible.*/
		ShellPath = "busybox";
		ShellDissolves = false;
	}
#ifndef WEIRDSHELLPERMITTED
	else /*Found no other shells. Assume fossil, spit warning.*/
	{
		static Bool DidWarn = false; /*Don't spam this warning.*/
		
		ShellDissolves = false;
		if (!DidWarn)
		{	 
			DidWarn = true;
			SpitWarning("No known shell found. Using /bin/sh.\n"
			"Best if you install one of these: bash, dash, csh, zsh, or busybox.\n"
			"This matters because PID detection is affected by the way shells handle sh -c.");
		}
	}
#endif
	
	/**Here be where we execute commands.---------------**/
	LaunchPID = fork();
	
	if (LaunchPID < 0)
	{
		SpitError("Failed to call fork(). This is a critical error.");
		EmergencyShell();
	}
	
	if (LaunchPID == 0) /**Child process code.**/
	{ /*Child does all this.*/
		char TmpBuf[1024];
		execlp(ShellPath, "sh", "-c", CurCmd, NULL); /*I bet you think that this is going to return the PID of sh. No.*/
		/*We still around to talk about it? We were supposed to be imaged with the new command!*/
		
		snprintf(TmpBuf, 1024, "Failed to execute %s: execlp() failure.", InObj->ObjectID);
		SpitError(TmpBuf);
		EmergencyShell();
	}
	
	/*Get PID*/ /**Parent code resumes.**/
	InObj->ObjectPID = waitpid(LaunchPID, &RawExitStatus, 0); /*Wait for the process to exit.*/
	
	if (!ShellDissolves)
	{
		++InObj->ObjectPID; /*This probably won't always work, but 99.9999999% of the time, yes, it will.*/
	}
	
	/**And back to normalcy after this.------------------**/
	
	switch (WEXITSTATUS(RawExitStatus))
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
	
	return ExitStatus;
}

rStatus ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode)
{
	char PrintOutStream[1024];
	rStatus ExitStatus = SUCCESS;
	
	/*Copy in the description to be printed to the console.*/
	snprintf(PrintOutStream, 1024, "%s %s ", (IsStartingMode ? "Starting" : "Stopping"), CurObj->ObjectName);
	
	if (IsStartingMode)
	{		
		printf(PrintOutStream);
		fflush(NULL); /*Things tend to get clogged up when we don't flush.*/
		
		ExitStatus = ExecuteConfigObject(CurObj, IsStartingMode); /*Don't bother with return value here.*/
		CurObj->Started = (ExitStatus ? true : false); /*Mark the process dead or alive.*/
		PrintStatusReport(PrintOutStream, ExitStatus);
	}
	else
	{		
		switch (CurObj->StopMode)
		{
			case STOP_COMMAND:
				printf(PrintOutStream);
				fflush(NULL);
				
				ExitStatus = ExecuteConfigObject(CurObj, IsStartingMode);
				CurObj->Started = (ExitStatus ? false : true); /*Mark the process dead.*/
				
				PrintStatusReport(PrintOutStream, ExitStatus);
				break;
			case STOP_INVALID:
				break;
			case STOP_NONE:
				CurObj->Started = false; /*Just say we did it even if nothing to do.*/
				break;
			case STOP_PID:
				printf(PrintOutStream);
				fflush(NULL);
				
				if (kill(CurObj->ObjectPID, OSCTL_SIGNAL_TERM) == 0)
				{ /*Just send SIGTERM.*/
					ExitStatus = SUCCESS;
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				CurObj->Started = (ExitStatus ? false : true);
				
				PrintStatusReport(PrintOutStream, ExitStatus);
				
				break;
			case STOP_PIDFILE:
			{
				FILE *Tdesc = fopen(CurObj->ObjectPIDFile, "r");
				unsigned long Inc = 0, TruePID = 0;
				char Buf[MAX_LINE_SIZE], WChar, *TWorker;
				
				printf(PrintOutStream);
				fflush(NULL);
				
				for (; (WChar = getc(Tdesc)) != EOF && Inc < MAX_LINE_SIZE - 1; ++Inc)
				{
					Buf[Inc] = WChar;
				}
				Buf[Inc] = '\0'; /*Stop. Whining. About. The. Loops. I don't want to use stat() here!*/
				
				fclose(Tdesc);
				
				if ((TWorker = strstr(Buf, "\n")) != NULL) /*Nuke the newlines.*/ 
				{
					*TWorker = '\0';
				}
				
				if (isdigit(Buf[0]))
				{
					TruePID = atoi(Buf);
				}
				else
				{
					char TmpBuf[1024];
					
					snprintf(TmpBuf, 1024, "Cannot kill %s: The PID file does not contain purely numeric values.", CurObj->ObjectID);
					SpitError(TmpBuf);
					PrintStatusReport(PrintOutStream, FAILURE);
					
					return WARNING;
				}
				
				/*Now we can actually kill the process ID.*/
				
				if (kill(TruePID, OSCTL_SIGNAL_TERM) == 0)
				{
					ExitStatus = SUCCESS;
				}
				else
				{
					ExitStatus = FAILURE;
				}
				CurObj->Started = (ExitStatus ? false : true);
				PrintStatusReport(PrintOutStream, ExitStatus);
				
				break;
			}
		}
	}
	
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
		
		if (!CurObj->Enabled)
		{
			continue;
		}
		
		if ((IsStartingMode ? !CurObj->Started : CurObj->Started))
		{
			ProcessConfigObject(CurObj, IsStartingMode);
		}
	}
	
	return SUCCESS;
}

rStatus SwitchRunlevels(const char *Runlevel)
{
	unsigned long NumInRunlevel = 0, CurPriority = 1, MaxPriority;
	ObjTable *TObj = ObjectTable;
	/*Check the runlevel has objects first.*/
	
	for (; TObj->Next != NULL; TObj = TObj->Next)
	{ /*I think a while loop would look much better, but if I did that,
		* I'd get folks asking "why didn't you use a for loop?", so here!*/
		if (ObjRL_CheckRunlevel(Runlevel, TObj) && TObj->Enabled && (TObj->ObjectStartPriority > 0 || TObj->ObjectStopPriority > 0))
		{
			++NumInRunlevel;
		}
	}
	
	if (NumInRunlevel == 0)
	{
		return FAILURE;
	}
	
	/*Stop everything not meant for this runlevel.*/
	for (MaxPriority = GetHighestPriority(false); CurPriority <= MaxPriority; ++CurPriority)
	{
		TObj = GetObjectByPriority(CurRunlevel, false, CurPriority);
		
		if (TObj && TObj->Started && TObj->CanStop)
		{
			ProcessConfigObject(TObj, false);
		}
	}
	
	/*Good to go, so change us to the new runlevel.*/
	strncpy(CurRunlevel, Runlevel, MAX_DESCRIPT_SIZE);
	
	/*Now start the things that ARE meant for our runlevel.*/
	for (CurPriority = 1, MaxPriority = GetHighestPriority(true);
		CurPriority <= MaxPriority; ++CurPriority)
	{
		TObj = GetObjectByPriority(CurRunlevel, true, CurPriority);
		
		if (TObj && !TObj->Started)
		{
			ProcessConfigObject(TObj, true);
		}
	}
	
	return SUCCESS;
}
