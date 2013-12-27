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
#include <time.h>
#include "epoch.h"

/**Globals**/

/*We store the current runlevel here.*/
char CurRunlevel[MAX_DESCRIPT_SIZE] = { '\0' };
struct _CTask CurrentTask = { NULL }; /*We save this for each linear task, so we can kill the process if it becomes unresponsive.*/
volatile BootMode CurrentBootMode = BOOT_NEUTRAL;

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
#ifdef NOMMU
#define ForkFunc() fork()
#else
#define ForkFunc() vfork()
#endif

	pid_t LaunchPID;
	rStatus ExitStatus = FAILURE; /*We failed unless we succeeded.*/
	int RawExitStatus, Inc = 0;
	sigset_t SigMaker[2];	
#ifndef NOSHELL
	Bool ShellEnabled = true; /*If we use shells.*/
	Bool ShellDissolves = SHELLDISSOLVES;
	Bool ForceShell = InObj->Opts.ForceShell;
	static Bool DidWarn = false;
	const char *ShellPath = "/bin/sh";
	
	/*Check how we should handle PIDs for each shell. In order to get the PID, exit status,
	* and support shell commands, we need to jump through a bunch of hoops.*/
	if (ShellEnabled)
	{
		if (FileUsable(SHELLPATH))
		{ /*Try our specified shell first.*/
			ShellPath = SHELLPATH;
		}
		else if (FileUsable("/bin/bash"))
		{
			ShellDissolves = true;
			ShellPath = "/bin/bash";
		}
		else if (FileUsable("/bin/dash"))
		{
			ShellPath = "/bin/dash";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/zsh"))
		{
			ShellPath = "/bin/zsh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/csh"))
		{
			ShellPath = "/bin/csh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/tcsh"))
		{
			ShellPath = "/bin/tcsh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/ksh"))
		{
			ShellPath = "/bin/ksh";
			ShellDissolves = true;
		}
		else if (FileUsable("/bin/busybox"))
		{ /*This is one of those weird shells that still does the old practice of creating a child for -c.
			* We can deal with the likes of them. Small chance that for shells like this, another PID could jump in front
			* and we could end up storing the wrong one. Very small, but possible.*/
			ShellPath = "/bin/busybox";
			ShellDissolves = false;
		}
		else /*Found no other shells. Assume fossil, spit warning.*/
		{
			const char *Errs[2] = { ("Cannot find any functioning shell. /bin/sh is not available.\n"
									 CONSOLE_COLOR_YELLOW "** Disabling shell support! **" CONSOLE_ENDCOLOR),
									("No known shell found. Using \"/bin/sh\".\n"
									"Best if you install one of these: bash, dash, csh, zsh, or busybox.\n") };
			if (!DidWarn)
			{
				
				if (!FileUsable("/bin/sh"))
				{
					SpitWarning(Errs[0]);
					WriteLogLine(Errs[0], true);
					
					ShellEnabled = false; /*Disable shell support.*/
				}
				else
				{
					ShellDissolves = true; /*Most do.*/
					SpitWarning(Errs[1]);
					WriteLogLine(Errs[1], true);
				}
				
				DidWarn = true;
			}
		}
		
		if (!DidWarn && strcmp(ShellPath, ENVVAR_SHELL) != 0)
		{ /*Only happens if we are using a known shell, even if it's not ours.*/
			char ErrBuf[MAX_LINE_SIZE];
			
			snprintf(ErrBuf, MAX_LINE_SIZE, "\"" ENVVAR_SHELL "\" cannot be read. Using \"%s\" instead.", ShellPath);
			
			/*Just write to log, because this happens.*/
			WriteLogLine(ErrBuf, true);
			SpitWarning(ErrBuf);
			DidWarn = true;
		}
	}
#endif /*NOSHELL*/
	/**Here be where we execute commands.---------------**/
	
	/*We need to block all signals until we have executed the process.*/
	sigemptyset(&SigMaker[0]);
	
	for (; Inc < NSIG; ++Inc)
	{
		sigaddset(&SigMaker[0], Inc);
	}
	SigMaker[1] = SigMaker[0];
	
	sigprocmask(SIG_BLOCK, &SigMaker[0], NULL);
	
	/**Actually do the (v)fork().**/
	LaunchPID = ForkFunc();
	
	if (LaunchPID < 0)
	{
		SpitError("Failed to call vfork(). This is a critical error.");
		EmergencyShell();
	}
	
	if (LaunchPID > 0)
	{
			CurrentTask.Node = InObj;
			CurrentTask.TaskName = InObj->ObjectID;
			CurrentTask.PID = LaunchPID;
			CurrentTask.Set = true;
			
			sigprocmask(SIG_UNBLOCK, &SigMaker[1], NULL); /*Unblock now that (v)fork() is complete.*/
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
		
		sigprocmask(SIG_UNBLOCK, &Sig2, NULL); /*Unblock signals.*/
		
		/*Change our session id.*/
		setsid();
		
#ifndef NOSHELL
		if (ShellEnabled && (strpbrk(CurCmd, "&^$#@!()*%{}`~+|\\<>?;:'[]\"\t") != NULL || ForceShell))
		{
			execlp(ShellPath, "sh", "-c", CurCmd, NULL); /*I bet you think that this is going to return the PID of sh. No.*/
			
			snprintf(TmpBuf, 1024, "Failed to execute %s: execlp() failure launching \"" SHELLPATH "\".", InObj->ObjectID);
			SpitError(TmpBuf);
			exit(1); /*Makes sure that we report failure. This is a problem.*/
		}
		else
#endif
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
			
			/*In this case, it could be a file not found, in which case, just have the child, us, exit gracefully.*/
			exit(1);
			
		}
		/*We still around to talk about it? We were supposed to be imaged with the new command!*/
	}
	
	/**Parent code resumes.**/
	waitpid(LaunchPID, &RawExitStatus, 0); /*Wait for the process to exit.*/

	CurrentTask.Set = false;
	CurrentTask.Node = NULL;
	CurrentTask.TaskName = NULL;
	CurrentTask.PID = 0; /*Set back to zero for the next one.*/
	
	if (CurCmd == InObj->ObjectStartCommand)
	{
		InObj->ObjectPID = LaunchPID; /*Save our PID.*/
#ifndef NOSHELL		
		if (!ShellDissolves)
		{
			++InObj->ObjectPID; /*This probably won't always work, but 99.9999999% of the time, yes, it will.*/
		}
#endif
		if (InObj->Opts.IsService)
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
		if (ExitStatus && CurObj->Opts.HasPIDFile)
		{
			CurrentTask.Node = (void*)&Counter;
			CurrentTask.TaskName = CurObj->ObjectID;
			CurrentTask.PID = 0;
			CurrentTask.Set = true;
			
			while (!FileUsable(CurObj->ObjectPIDFile))
			{ /*Wait ten seconds total but check every 0.0001 seconds.*/
				usleep(100);
				
				++Counter;
				
				if (Counter == 100000)
				{
					char OutBuf[MAX_LINE_SIZE];
					
					snprintf(OutBuf, sizeof OutBuf,CONSOLE_COLOR_YELLOW "WARNING: " CONSOLE_ENDCOLOR
								"Object %s was successfully started%s,\n"
								"but it's PID file did not appear within ten seconds of start.\n"
								"Please verify that \"%s\" exists and whether this object is starting properly.",
								CurObj->ObjectID, (ExitStatus == WARNING ? ", but with a warning" : ""),
								CurObj->ObjectPIDFile);
								
					WriteLogLine(OutBuf, true);
					ExitStatus = WARNING;
					break;
				}
				else if (Counter > 100000)
				{
					break;
				}
			}
			
			CurrentTask.Set = false;
			CurrentTask.Node = NULL;
			CurrentTask.TaskName = NULL;
			CurrentTask.PID = 0;
		}
		
		CurObj->Started = (ExitStatus ? true : false); /*Mark the process dead or alive.*/
		
		if (ExitStatus)
		{
			CurObj->StartedSince = time(NULL);
		}
		
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
			{
				unsigned long Inc = 0;
				
				if (PrintStatus)
				{
					printf("%s", PrintOutStream);
					fflush(NULL);
				}
				
				ExitStatus = ExecuteConfigObject(CurObj, CurObj->ObjectStopCommand);
				
				if (!CurObj->Opts.NoStopWait)
				{
					unsigned long CurPID = 0;
					
					CurrentTask.Node = (void*)&Inc;
					CurrentTask.PID = 0;
					CurrentTask.TaskName = CurObj->ObjectID;
					CurrentTask.Set = true;
					
					for (; ObjectProcessRunning(CurObj) && Inc < 100000; ++Inc)
					{ /*Sleep for ten seconds.*/
						CurPID = CurObj->Opts.HasPIDFile ? ReadPIDFile(CurObj) : CurObj->ObjectPID;
						
						if (!CurPID) break; /*No PID? No point.*/
						
						waitpid(CurPID, NULL, WNOHANG);
						
						usleep(100);
					}
					
					if (Inc >= 100000)
					{
						char TmpBuf[MAX_LINE_SIZE];
						
						if (Inc == 100000) /*Genuine timeout, otherwise we killed it ourselves.*/
						{
							snprintf(TmpBuf, MAX_LINE_SIZE, "Object %s reports to have been stopped %s,\n"
									"but the process is still running as PID %lu.\n"
									"Add 'ObjectOptions NOSTOPWAIT' to this object's section\n"
									"in epoch.conf to silence this warning.", CurObj->ObjectID,
									(ExitStatus == WARNING ? "(with a warning)" : "successfully"),
									(CurObj->Opts.HasPIDFile ? ReadPIDFile(CurObj) : CurObj->ObjectPID));
							WriteLogLine(TmpBuf, true);
						}
						ExitStatus = WARNING;
					}
					
					CurrentTask.Set = false;
					CurrentTask.Node = NULL;
					CurrentTask.TaskName = NULL;
					CurrentTask.PID = 0;
				}
					
				if (ExitStatus)
				{
					CurObj->ObjectPID = 0;
					CurObj->Started = false;
					CurObj->StartedSince = 0;
				}
				
				if (PrintStatus)
				{
					PerformStatusReport(PrintOutStream, ExitStatus, true);
				}
				break;
			}
			case STOP_INVALID:
				break;
			case STOP_NONE:
				CurObj->Started = false; /*Just say we did it even if nothing to do.*/
				CurObj->StartedSince = 0;
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
				
				if (kill(CurObj->ObjectPID, CurObj->TermSignal) == 0)
				{ /*Just send SIGTERM.*/
					if (!CurObj->Opts.NoStopWait)
					{
						unsigned long TInc = 0;
						
						CurrentTask.Node = (void*)&TInc;
						CurrentTask.PID = 0;
						CurrentTask.TaskName = CurObj->ObjectID;
						CurrentTask.Set = true;
								
						/*Give it ten seconds to terminate on it's own.*/
						for (; kill(CurObj->ObjectPID, 0) == 0 && TInc < 200; ++TInc)
						{ /*Two hundred is ten seconds here.*/
							
							waitpid(CurObj->ObjectPID, NULL, WNOHANG); /*We must harvest the PID since we have occupied the primary loop.*/
							
							usleep(50000);
						}
						
						if (TInc < 200)
						{
							ExitStatus = SUCCESS;
						}
						else if (TInc > 200)
						{ /*Means we were killed via CTRL-ALT-DEL.*/
							ExitStatus = WARNING;
						}
						else if (TInc == 200)
						{
							ExitStatus = FAILURE;
						}
						
						CurrentTask.Set = false;
						CurrentTask.Node = NULL;
						CurrentTask.TaskName = NULL;
						CurrentTask.PID = 0;
					}
					else
					{ /*Just quit and say everything's fine.*/
						ExitStatus = SUCCESS;
					}
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				if (ExitStatus)
				{
					CurObj->ObjectPID = 0;
					CurObj->StartedSince = 0;
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
				if (kill(TruePID, CurObj->TermSignal) == 0)
				{
					if (!CurObj->Opts.NoStopWait)
					{ /*If we're free to wait for a PID to stop, do so.*/
						unsigned long TInc = 0;			
							
						CurrentTask.Node = (void*)&TInc;
						CurrentTask.PID = 0;
						CurrentTask.TaskName = CurObj->ObjectID;
						CurrentTask.Set = true;
						
						/*Give it ten seconds to terminate on it's own.*/
						for (; kill(TruePID, 0) == 0 && TInc < 200; ++TInc)
						{ /*Two hundred is ten seconds here.*/
							
							waitpid(TruePID, NULL, WNOHANG); /*We must harvest the PID since we have occupied the primary loop.*/
							
							usleep(50000);
						}
						
						if (TInc < 200)
						{
							ExitStatus = SUCCESS;
						}
						else if (TInc > 200)
						{ /*Means we were killed via CTRL-ALT-DEL.*/
							ExitStatus = WARNING;
						}
						else if (TInc == 200)
						{
							ExitStatus = FAILURE;
						}
						
						CurrentTask.Set = false;
						CurrentTask.Node = NULL;
						CurrentTask.TaskName = NULL;
						CurrentTask.PID = 0;
					}
					else
					{
						ExitStatus = SUCCESS;
					}
				}
				else
				{
					ExitStatus = FAILURE;
				}
				
				if (ExitStatus)
				{
					CurObj->Started = false;
					CurObj->StartedSince = 0;
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
	ObjTable *LastNode = NULL;
	
	if (!MaxPriority && IsStartingMode)
	{
		SpitError("All objects have a priority of zero!");
		return FAILURE;
	}
	
	CurrentBootMode = (IsStartingMode ? BOOT_BOOTUP : BOOT_SHUTDOWN);
	
	for (; Inc <= MaxPriority; ++Inc)
	{
		for (LastNode = NULL;
			(CurObj = GetObjectByPriority((IsStartingMode ? CurRunlevel : NULL), LastNode, IsStartingMode, Inc));
			LastNode = CurObj)
		{ /*Probably set to zero or something, but we don't care if we have a gap in the priority system.*/
		
			if (CurObj == (void*)-1)
			{
				return FAILURE;
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
		snprintf(StatusReportBuf, MAX_DESCRIPT_SIZE, "Reloading %s", CurObj->ObjectID);
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
	ObjTable *LastNode = NULL;
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
		for (LastNode = NULL; (TObj = GetObjectByPriority(CurRunlevel, LastNode, false, CurPriority)); LastNode = TObj)
		{
			if (TObj == (void*)-1)
			{
				return FAILURE;
			}
			
			if (TObj->Started && TObj->Opts.CanStop && !TObj->Opts.HaltCmdOnly &&
				!ObjRL_CheckRunlevel(Runlevel, TObj, true))
			{
				ProcessConfigObject(TObj, false, true);
			}
		}
	}
	
	/*Good to go, so change us to the new runlevel.*/
	snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", Runlevel);
	MaxPriority = GetHighestPriority(true);
	
	/*Now start the things that ARE meant for our runlevel.*/
	for (CurPriority = 1; CurPriority <= MaxPriority; ++CurPriority)
	{
		for (LastNode = NULL; (TObj = GetObjectByPriority(CurRunlevel, LastNode, true, CurPriority)); LastNode = TObj)
		{
			if (TObj == (void*)-1)
			{
				return FAILURE;
			}
			
			if (TObj->Enabled && !TObj->Started)
			{
				ProcessConfigObject(TObj, true, true);
			}
		}
	}
	
	return SUCCESS;
}
