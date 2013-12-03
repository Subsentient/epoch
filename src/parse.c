/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <pthread.h>
#include "epoch.h"

/**Globals**/

/*We store the current runlevel here.*/
char CurRunlevel[MAX_DESCRIPT_SIZE] = { '\0' };
volatile unsigned long RunningChildCount = 0; /*How many child processes are running?
									* I promised myself I wouldn't use this code.*/
struct _CTask CurrentTask = { NULL, 0 }; /*We save this for each linear task, so we can kill the process if it becomes unresponsive.*/
volatile BootMode CurrentBootMode = BOOT_NEUTRAL;
Bool ShellEnabled = USE_SHELL_BY_DEFAULT; /*If we use shells.*/

/**Function forward declarations.**/

static rStatus ExecuteConfigObject(ObjTable *InObj, const char *CurCmd);

/**Actual functions.**/

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
		return false;
	}
}	

static rStatus ExecuteConfigObject(ObjTable *InObj, const char *CurCmd)
{ /*Not making static because this is probably going to be useful for other stuff.*/
	pid_t LaunchPID;
	const char *ShellPath = "sh"; /*We try not to use absolute paths here, because some distros don't have normal layouts,
											*And I'm sure they would rather see a warning than have it just botch up.*/
	rStatus ExitStatus = FAILURE; /*We failed unless we succeeded.*/
	Bool ShellDissolves = true, ForceShell = InObj->Opts.ForceShell;
	int RawExitStatus, Inc = 0;
	sigset_t SigMaker[2];
	
#ifdef NOMMU
#define ForkFunc() fork()
#else
#define ForkFunc() vfork()
#endif
	
	/*Check how we should handle PIDs for each shell. In order to get the PID, exit status,
	* and support shell commands, we need to jump through a bunch of hoops.*/
	if (ShellEnabled)
	{
		if (!FileUsable(ENVVAR_SHELL))
		{
			SpitWarning("Cannot find and/or read shell binary \"" ENVVAR_SHELL "\". Disabling shell support.");
			ShellEnabled = false;
		}
		else if (FileUsable("/bin/bash"))
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
	}
	/**Here be where we execute commands.---------------**/
	
	/*We need to block all signals until we have executed the process.*/
	sigemptyset(&SigMaker[0]);
	
	for (; Inc < NSIG; ++Inc)
	{
		sigaddset(&SigMaker[0], Inc);
	}
	SigMaker[1] = SigMaker[0];
	
	pthread_sigmask(SIG_BLOCK, &SigMaker[0], NULL);
	
	/**Actually do the (v)fork().**/
	LaunchPID = ForkFunc();
	
	if (LaunchPID < 0)
	{
		SpitError("Failed to call vfork(). This is a critical error.");
		EmergencyShell();
	}
	
	if (LaunchPID > 0)
	{
			++RunningChildCount; /*I have a feeling that when the stars align,
								* this variable will be accessed by two threads at once
								* and crash to the ground. I hope I'm wrong.*/

			CurrentTask.Node = InObj;
			CurrentTask.PID = LaunchPID;
			
			pthread_sigmask(SIG_UNBLOCK, &SigMaker[1], NULL); /*Unblock now that (v)fork() is complete.*/
	}
	
	if (LaunchPID == 0) /**Child process code.**/
	{ /*Child does all this.*/
		char TmpBuf[1024];
		int Inc = 0;
		sigset_t Sig2;
		
		sigemptyset(&Sig2);
		
		for (; Inc < NSIG; ++Inc)
		{
			sigaddset(&Sig2, Inc);
			signal(Inc, SIG_DFL); /*Set all the signal handlers to default while we're at it.*/
		}
		
		pthread_sigmask(SIG_UNBLOCK, &Sig2, NULL); /*Unblock signals.*/
		
		/*Change our session id.*/
		setsid();
		
		if (ShellEnabled && (strpbrk(CurCmd, "&^$#@!()*%{}`~+=-|\\<>?;:'[]\"\t") != NULL || ForceShell))
		{
			execlp(ShellPath, "sh", "-c", CurCmd, NULL); /*I bet you think that this is going to return the PID of sh. No.*/
		}
		else
		{ /*don't worry about the heap stuff, exec() takes care of it you know.*/
			char **ArgV = NULL;
			unsigned long NumSpaces = 1, Inc = 0, cOffset = 0, Inc2 = 0;
			char NCmd[MAX_LINE_SIZE], *Worker = NCmd;
			
			strncpy(NCmd, CurCmd, strlen(CurCmd) + 1);
			
			while ((Worker = WhitespaceArg(Worker))) ++NumSpaces;
			
			for (Worker = NCmd; Worker[strlen(Worker) - 1] == ' '; --NumSpaces)
			{
				Worker[strlen(Worker) - 1] = '\0';
			}
			
			ArgV = malloc(sizeof(char*) * NumSpaces + 1);
			
			for (Inc = 0; Inc < NumSpaces; ++Inc)
			{
				ArgV[Inc] = malloc(strlen(NCmd) + 1);
				
				for (Inc2 = 0; Worker[Inc2 + cOffset] != ' ' && Worker[Inc2 + cOffset] != '\0'; ++Inc2)
				{
					ArgV[Inc][Inc2] = Worker[Inc2 + cOffset];
				}
				ArgV[Inc][Inc2] = '\0';
				
				cOffset += Inc2 + 1;
			}
			
			ArgV[NumSpaces] = NULL;
			
			execvp(ArgV[0], ArgV);
			
		}
		/*We still around to talk about it? We were supposed to be imaged with the new command!*/
		
		snprintf(TmpBuf, 1024, "Failed to execute %s: execlp() failure.", InObj->ObjectID);
		SpitError(TmpBuf);
		return -1;
	}
	
	/**Parent code resumes.**/

	waitpid(LaunchPID, &RawExitStatus, 0); /*Wait for the process to exit.*/
	--RunningChildCount; /*We're done, so say so.*/
	
	CurrentTask.Node = NULL;
	CurrentTask.PID = 0; /*Set back to zero for the next one.*/
	
	if (CurCmd != InObj->ObjectPrestartCommand)
	{
		InObj->ObjectPID = LaunchPID; /*Save our PID.*/
		
		if (!ShellDissolves)
		{
			++InObj->ObjectPID; /*This probably won't always work, but 99.9999999% of the time, yes, it will.*/
		}
		if (CurCmd == InObj->ObjectStartCommand && InObj->Opts.IsService)
		{ /*If we specify that this is a service, one up the PID again.*/
			++InObj->ObjectPID;
		}
		
		/*Check if the PID we found is accurate and update it if not. This method is very,
		 * very accurate compared to the buggy morass above.*/
		AdvancedPIDFind(InObj, true);
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

rStatus ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode, Bool PrintStatus)
{
	char PrintOutStream[1024];
	rStatus ExitStatus = FAILURE;
	
	if (IsStartingMode && *CurObj->ObjectStartCommand == '\0')
	{ /*Don't bother with it, if it has no command.
		For starting objects, this should not happen unless we set the option HALTONLY.*/
		return SUCCESS;
	}
	
	if (PrintStatus)
	{/*Copy in the description to be printed to the console.*/
		if (CurObj->Opts.RawDescription)
		{
			snprintf(PrintOutStream, 1024, "%s", CurObj->ObjectDescription);
		}
		else if (!IsStartingMode && CurObj->Opts.HaltCmdOnly)
		{
			snprintf(PrintOutStream, 1024, "%s %s", "Starting", CurObj->ObjectDescription);
		}
		else
		{
			snprintf(PrintOutStream, 1024, "%s %s", (IsStartingMode ? "Starting" : "Stopping"), CurObj->ObjectDescription);
		}
		
		if (IsStartingMode && CurObj->Opts.HaltCmdOnly)
		{
			PerformStatusReport(PrintOutStream, FAILURE, true);
		}
	}
	
	if (IsStartingMode && CurObj->Opts.HaltCmdOnly) return FAILURE;
	
	if (IsStartingMode)
	{		
		rStatus PrestartExitStatus = SUCCESS;
		unsigned long Counter = 0;
		
		if (PrintStatus)
		{
			printf("%s", PrintOutStream);
		}
		
		fflush(NULL); /*Things tend to get clogged up when we don't flush.*/
		
		if (*CurObj->ObjectPrestartCommand != '\0')
		{
			PrestartExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectPrestartCommand);
		}
		
		ExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectStartCommand);
		
		if (!PrestartExitStatus && ExitStatus)
		{
			ExitStatus = WARNING;
		}
		
		/*Wait for a PID file to appear if we specified one. This prevents autorestart hell.*/
		if (CurObj->Opts.HasPIDFile)
		{
			while (!FileUsable(CurObj->ObjectPIDFile))
			{ /*Wait ten seconds total but check every 0.0001 seconds.*/
				usleep(100);
				
				++Counter;
				
				if (Counter >= 100000)
				{
					ExitStatus = WARNING;
				}
			}
		}
		
		CurObj->Started = (ExitStatus ? true : false); /*Mark the process dead or alive.*/
		
		if (PrintStatus)
		{
			PerformStatusReport(PrintOutStream, ExitStatus, true);
		}
	}
	else
	{	
		Bool LastAutoRestartState = CurObj->Opts.AutoRestart;
		/*We need to do this so objects that are stopped have no chance of restarting themselves.*/
		CurObj->Opts.AutoRestart = false;
		
		switch (CurObj->Opts.StopMode)
		{
			case STOP_COMMAND:
				if (PrintStatus)
				{
					printf("%s", PrintOutStream);
					fflush(NULL);
				}
				
				ExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectStopCommand);
				
				if (ExitStatus)
				{
					CurObj->ObjectPID = 0;
					CurObj->Started = false;
				}
				
				if (PrintStatus)
				{
					PerformStatusReport(PrintOutStream, ExitStatus, true);
				}
				break;
			case STOP_INVALID:
				break;
			case STOP_NONE:
				CurObj->Started = false; /*Just say we did it even if nothing to do.*/
				ExitStatus = SUCCESS;
				CurObj->ObjectPID = 0;
				break;
			case STOP_PID:
			{
				if (PrintStatus)
				{
					printf("%s", PrintOutStream);
					fflush(NULL);
				}
				
				if (!CurObj->ObjectPID)
				{
					ExitStatus = FAILURE;
					if (PrintStatus)
					{
						PerformStatusReport(PrintOutStream, ExitStatus, true);
					}
					break;
				}
				
				if (kill(CurObj->ObjectPID, SIGTERM) == 0)
				{ /*Just send SIGTERM.*/
					unsigned char TInc = 0;
							
					/*Give it ten seconds to terminate on it's own.*/
					for (; TInc < 200 && kill(CurObj->ObjectPID, 0) == 0; ++TInc)
					{ /*Two hundred is ten seconds here.*/
						
						/*If we are not in a thread, we need to wait for the process ourselves,
						because nobody else will do it for us.*/
						waitpid(CurObj->ObjectPID, NULL, WNOHANG);
						
						usleep(50000);
					}
					
					if (TInc < 200)
					{
						ExitStatus = SUCCESS;
					}
					else
					{
						ExitStatus = FAILURE;
					}
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				if (ExitStatus)
				{
					CurObj->ObjectPID = 0;
					CurObj->Started = false;
				}
				
				if (PrintStatus)
				{
					PerformStatusReport(PrintOutStream, ExitStatus, true);
				}
				
				break;
			}
			case STOP_PIDFILE:
			{
				unsigned long TruePID = 0;
				
				if (PrintStatus)
				{
					printf("%s", PrintOutStream);
					fflush(NULL);
				}
				
				if (!(TruePID = ReadPIDFile(CurObj)))
				{ /*This might be good with goto, but... no.*/
					ExitStatus = FAILURE;
					if (PrintStatus)
					{
						PerformStatusReport(PrintOutStream, ExitStatus, true);
					}
					break;
				}
				
				/*Now we can actually kill the process ID.*/
				if (kill(TruePID, SIGTERM) == 0)
				{
					unsigned char TInc = 0;				
					/*Give it ten seconds to terminate on it's own.*/
					for (; TInc < 200 && kill(TruePID, 0) == 0; ++TInc)
					{ /*Two hundred is ten seconds here.*/
						
						/*If we are not in a thread, we need to wait for the process ourselves,
						because nobody else will do it for us.*/
						waitpid(TruePID, NULL, WNOHANG);

						usleep(50000);
					}
					
					if (TInc < 200)
					{
						ExitStatus = SUCCESS;
					}
					else
					{
						ExitStatus = FAILURE;
					}
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				if (ExitStatus)
				{
					CurObj->Started = false;
					CurObj->ObjectPID = 0;
				}
				
				if (PrintStatus)
				{
					PerformStatusReport(PrintOutStream, ExitStatus, true);
				}
				
				break;
			}
		}
		
		/*Now that the object is stopped, we should reset the autorestart to it's previous state.*/
		CurObj->Opts.AutoRestart = LastAutoRestartState;
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
	
	CurrentBootMode = (IsStartingMode ? BOOT_BOOTUP : BOOT_SHUTDOWN);
	
	for (; Inc <= MaxPriority; ++Inc)
	{
		if (!(CurObj = GetObjectByPriority(IsStartingMode ? CurRunlevel : NULL, IsStartingMode, Inc)))
		{ /*Probably set to zero or something, but we don't care if we have a gap in the priority system.*/
			continue;
		}
		
		if (!CurObj->Enabled && (IsStartingMode || CurObj->Opts.HaltCmdOnly))
		{ /*Stop even disabled objects, but not disabled HALTONLY objects.*/
			continue;
		}
		
		if (IsStartingMode && CurObj->Opts.HaltCmdOnly)
		{
			continue;
		}
		
		if ((IsStartingMode ? !CurObj->Started : CurObj->Started))
		{
			ProcessConfigObject(CurObj, IsStartingMode, true);
		}
	}
	
	CurrentBootMode = BOOT_NEUTRAL;
	
	return SUCCESS;
}

rStatus ProcessReloadCommand(ObjTable *CurObj, Bool PrintStatus)
{
	rStatus RetVal = FAILURE;
	char StatusReportBuf[MAX_DESCRIPT_SIZE];
	
	if (!CurObj->ObjectReloadCommand[0])
	{
		return FAILURE;
	}
	
	if (PrintStatus)
	{
		snprintf(StatusReportBuf, MAX_DESCRIPT_SIZE, "Reloading %s", CurObj->ObjectReloadCommand);
		printf("%s", StatusReportBuf);
	}
	
	RetVal = ExecuteConfigObject(CurObj, CurObj->ObjectReloadCommand);
	
	if (PrintStatus)
	{
		PerformStatusReport(StatusReportBuf, RetVal, true);
	}
	
	return RetVal;
}

rStatus SwitchRunlevels(const char *Runlevel)
{
	unsigned long NumInRunlevel = 0, CurPriority = 1, MaxPriority;
	ObjTable *TObj = ObjectTable;
	/*Check the runlevel has objects first.*/
	
	for (; TObj->Next != NULL; TObj = TObj->Next)
	{ /*I think a while loop would look much better, but if I did that,
		* I'd get folks asking "why didn't you use a for loop?", so here!*/
		if (!TObj->Opts.HaltCmdOnly && ObjRL_CheckRunlevel(Runlevel, TObj, true) &&
			TObj->Enabled && TObj->ObjectStartPriority > 0)
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
		
		if (TObj && TObj->Started && TObj->Opts.CanStop && !TObj->Opts.HaltCmdOnly &&
			!ObjRL_CheckRunlevel(Runlevel, TObj, true))
		{
			ProcessConfigObject(TObj, false, true);
		}
	}
	
	/*Good to go, so change us to the new runlevel.*/
	snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", Runlevel);
	
	/*Now start the things that ARE meant for our runlevel.*/
	for (CurPriority = 1, MaxPriority = GetHighestPriority(true);
		CurPriority <= MaxPriority; ++CurPriority)
	{
		TObj = GetObjectByPriority(CurRunlevel, true, CurPriority);
		
		if (TObj && TObj->Enabled && !TObj->Started)
		{
			ProcessConfigObject(TObj, true, true);
		}
	}
	
	return SUCCESS;
}
