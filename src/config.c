/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file handles the parsing of epoch.conf, our configuration file.
 * It adds everything into the object table.**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>
#include "epoch.h"

/*We want the only interface for this to be LookupObjectInTable().*/
ObjTable *ObjectTable = NULL;

/*Holds the system hostname.*/
char Hostname[MAX_LINE_SIZE] = { '\0' };

/*Function forward declarations for all the statics.*/
static ObjTable *AddObjectToTable(const char *ObjectID);
static char *NextLine(const char *InStream);
static char *NextSpace(const char *InStream);
static rStatus GetLineDelim(const char *InStream, char *OutStream);
static rStatus ScanConfigIntegrity(void);

/*Actual functions.*/
static char *NextLine(const char *InStream)
{
	if (!(InStream = strstr(InStream, "\n")))
	{
		return NULL;
	}

	if (*(InStream + 1) == '\0')
	{ /*End of file.*/
		return NULL;
	}

	++InStream; /*Plus one for the newline. We want to skip past it.*/

	return (char*)InStream;
}

static char *NextSpace(const char *InStream)
{  /*This is used for parsing lines that need to be divided by spaces.*/
	if (!(InStream = strstr(InStream, " ")))
	{
		return NULL;
	}

	if (*(InStream + 1) == '\0')
	{
		return NULL;
	}

	++InStream;

	return (char*)InStream;
}

rStatus InitConfig(void)
{ /*Set aside storage for the table.*/
	FILE *Descriptor = NULL;
	struct stat FileStat;
	char *ConfigStream = NULL, *Worker = NULL;
	ObjTable *CurObj = NULL;
	char DelimCurr[MAX_LINE_SIZE];
	unsigned long LineNum = 1;
	
	/*Get the file size of the config file.*/
	if (stat(CONFIGDIR CONF_NAME, &FileStat) != 0)
	{ /*Failure?*/
		SpitError("Failed to obtain information about configuration file epoch.conf.\nDoes it exist?");
		return FAILURE;
	}
	else
	{ /*No? Use the file size to allocate space in memory, since a char is a byte big.
	* If it's not a byte on your platform, your OS is not UNIX, and Epoch was not designed for you.*/
		ConfigStream = malloc(FileStat.st_size + 1);
	}

	Descriptor = fopen(CONFIGDIR CONF_NAME, "r"); /*Open the configuration file.*/

	/*Read the file into memory. I don't really trust fread(), but oh well.
	 * People will whine if I use a loop instead.*/
	fread(ConfigStream, 1, FileStat.st_size, Descriptor);
	
	ConfigStream[FileStat.st_size] = '\0'; /*Null terminate.*/
	
	fclose(Descriptor); /*Close the file.*/

	Worker = ConfigStream;

	/*Empty file?*/
	if ((*Worker == '\n' && *(Worker + 1) == '\0') || *Worker == '\0')
	{
		SpitError("Seems that epoch.conf is empty or corrupted.");
		free(ConfigStream);
		return FAILURE;
	}

	do /*This loop does most of the parsing.*/
	{
		if (*Worker == '\n')
		{ /*Empty line.*/
			continue;
		}
		else if (*Worker == '#')
		{ /*Line is just a comment.*/
			continue;
		}
		else if (!strncmp(Worker, "DisableCAD", strlen("DisableCAD")))
		{ /*Should we disable instant reboots on CTRL-ALT-DEL?*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute DisableCAD in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}

			if (!strcmp(DelimCurr, "true"))
			{
				DisableCAD = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				DisableCAD = false;
			}
			else
			{
				char TmpBuf[1024];
				
				DisableCAD = true;
				
				snprintf(TmpBuf, 1024, "Bad value %s for attribute DisableCAD at line %lu.\n"
						"Valid values are true and false. Assuming yes.",
						DelimCurr, LineNum);
						
				SpitWarning(TmpBuf);
			}

			continue;
		}
		/*This will mount /dev, /proc, /sys, /dev/pts, and /dev/shm on boot time, upon request.*/
		else if (!strncmp(Worker, "MountVirtual", strlen("MountVirtual")))
		{
			const char *TWorker = DelimCurr;
			unsigned long Inc = 0;
			char CurArg[MAX_DESCRIPT_SIZE];
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute MountVirtual in epoch.conf line %lu.", LineNum);
				SpitWarning(TmpBuf);

				continue;
			}
			
			do
			{
				for (Inc = 0; TWorker[Inc] != ' ' && TWorker[Inc] != '\t' && TWorker[Inc] != '\n' &&
					TWorker[Inc] != '\0' && Inc < (MAX_DESCRIPT_SIZE - 1); ++Inc)
				{
					CurArg[Inc] = TWorker[Inc];
				}
				CurArg[Inc] = '\0';
				
				if (!strcmp("procfs", CurArg))
				{
					AutoMountOpts[0] = true;
				}
				else if (!strcmp("sysfs", CurArg))
				{
					AutoMountOpts[1] = true;
				}
				else if (!strcmp("devfs", CurArg))
				{
					AutoMountOpts[2] = true;
				}
				else if (!strcmp("devpts", CurArg))
				{
					AutoMountOpts[3] = true;
				}
				else if (!strcmp("devshm", CurArg))
				{
					AutoMountOpts[4] = true;
				}
				else
				{
					char TmpBuf[1024];
					
					snprintf(TmpBuf, 1024, "Bad value %s for attribute MountVirtual on line %lu. Ignoring.", CurArg, LineNum);
					
					SpitWarning(TmpBuf);
					
					continue;
				}
					
			} while ((TWorker = NextSpace(TWorker)));
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute MountVirtual in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		/*Now we get into the actual attribute tags.*/
		else if (!strncmp(Worker, "BootBannerText", strlen("BootBannerText")))
		{ /*The text shown at boot up as a kind of greeter, before we start executing objects. Can be disabled, off by default.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute BootBannerText in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*So, they decided to explicitly opt out of banner display. Ok.*/
			{
				BootBanner.BannerText[0] = '\0';
				BootBanner.BannerColor[0] = '\0';
				BootBanner.ShowBanner = false; /*Should already be false, but to prevent possible bugs...*/
				continue;
			}
			snprintf(BootBanner.BannerText, MAX_LINE_SIZE, "%s", DelimCurr);
			
			BootBanner.ShowBanner = true;
			
			if ((strlen(DelimCurr) + 1) >= MAX_DESCRIPT_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute BootBannerText in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			continue;
		}
		else if (!strncmp(Worker, "BootBannerColor", strlen("BootBannerColor")))
		{ /*Color for boot banner.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute BootBannerColor in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*They don't want a color.*/
			{
				BootBanner.BannerColor[0] = '\0';
				continue;
			}
			
			SetBannerColor(DelimCurr); /*Function to be found elsewhere will do this for us, otherwise this loop would be even bigger.*/
			continue;
		}
		else if (!strncmp(Worker, "DefaultRunlevel", strlen("DefaultRunlevel")))
		{
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				char TmpBuf[1024];
				
				snprintf(TmpBuf, 1024, "Attribute DefaultRunlevel cannot be set after an ObjectID attribute; epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute DefaultRunlevel in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}	
			
			snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", DelimCurr);
			
			continue;
		}
		else if (!strncmp(Worker, "Hostname", strlen("Hostname")))
		{
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				char TmpBuf[1024];
				
				snprintf(TmpBuf, 1024, "Attribute Hostname cannot be set after an ObjectID attribute; epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute Hostname in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!strncmp(DelimCurr, "FILE", strlen("FILE")))
			{
				FILE *TDesc;
				unsigned long Inc = 0;
				char TChar;
				const char *TW = DelimCurr;
				
				TW += strlen("FILE");
				
				for (; *TW == ' ' || *TW == '\t'; ++TW);
				
				if (!(TDesc = fopen(TW, "r")))
				{
					char TmpBuf[1024];
					snprintf(TmpBuf, sizeof TmpBuf, "Failed to set hostname from file \"%s\".\n", TW);
					SpitWarning(TmpBuf);
					continue;
				}
				
				for (Inc = 0; (TChar = getc(TDesc)) != EOF && Inc < MAX_LINE_SIZE - 1; ++Inc)
				{
					Hostname[Inc] = TChar;
				}
				Hostname[Inc] = '\0';
				
				fclose(TDesc);
			}
			else
			{	
				snprintf(Hostname, MAX_LINE_SIZE, "%s", DelimCurr);
			}
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute Hostname in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}		
		else if (!strncmp(Worker, "ObjectID", strlen("ObjectID")))
		{ /*ASCII value used to identify this object internally, and also a kind of short name for it.*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectID in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}

			CurObj = AddObjectToTable(DelimCurr); /*Sets this as our current object.*/

			if ((strlen(DelimCurr) + 1) >= MAX_DESCRIPT_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectID in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}

			continue;
		}
		else if (!strncmp(Worker, "ObjectEnabled", strlen("ObjectEnabled")))
		{
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectEnabled comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectEnabled in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "true"))
			{
				CurObj->Enabled = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				CurObj->Enabled = false;
			}
			else
			{
				char TmpBuf[1024];
				
				snprintf(TmpBuf, 1024, "Bad value %s for attribute ObjectEnabled for object %s at line %lu.\n"
						"Valid values are true and false.",
						DelimCurr, CurObj->ObjectID, LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectOptions", strlen("ObjectOptions")))
		{
			const char *TWorker = DelimCurr;
			unsigned long Inc;
			char CurArg[MAX_DESCRIPT_SIZE];
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectOptions comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectOptions in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			do
			{
				
				for (Inc = 0; TWorker[Inc] != ' ' && TWorker[Inc] != '\t' && TWorker[Inc] != '\n'
					&& TWorker[Inc] != '\0' && Inc < (MAX_DESCRIPT_SIZE - 1); ++Inc)
				{
					CurArg[Inc] = TWorker[Inc];
				}
				CurArg[Inc] = '\0';
				
				if (!strcmp(CurArg, "NOWAIT"))
				{
					if (CurObj->Opts.HaltCmdOnly)
					{
						SpitWarning("ObjectOptions value NOWAIT contradicts another's HALTONLY.\nSticking with HALTONLY.");
						CurObj->Opts.NoWait = false;
						continue;
					}
					CurObj->Opts.NoWait = true;
				}
				else if (!strcmp(CurArg, "HALTONLY"))
				{ /*Allow entries that execute on shutdown only.*/
					CurObj->Started = true;
					CurObj->Opts.CanStop = false;
					CurObj->Opts.HaltCmdOnly = true;
				}
				else if (!strcmp(CurArg, "PERSISTENT"))
				{
					CurObj->Opts.CanStop = false;
				}
				else if (!strcmp(CurArg, "RAWDESCRIPTION"))
				{
					CurObj->Opts.RawDescription = true;
				}
				else if (!strcmp(CurArg, "SERVICE"))
				{
					CurObj->Opts.IsService = true;
				}
				else if (!strcmp(CurArg, "AUTORESTART"))
				{
					CurObj->Opts.AutoRestart = true;
				}
				else
				{
					char TmpBuf[1024];
					
					snprintf(TmpBuf, 1024, "Bad value %s for attribute ObjectOptions for object %s at line %lu.\n"
							"Valid values are NOWAIT, PERSISTENT, RAWDESCRIPTION, SERVICE, AUTORESTART,\nand HALTONLY.",
							DelimCurr, CurObj->ObjectID, LineNum);
					SpitError(TmpBuf);
					ShutdownConfig();
					free(ConfigStream);
					return FAILURE;
				}
			} while ((TWorker = NextSpace(TWorker)));
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectOptions in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectDescription", strlen("ObjectDescription")))
		{ /*It's description.*/
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectDescription comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectDescription in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			snprintf(CurObj->ObjectDescription, MAX_DESCRIPT_SIZE, "%s", DelimCurr);
			
			if ((strlen(DelimCurr) + 1) >= MAX_DESCRIPT_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectDescription in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}

			continue;
		}
		else if (!strncmp(Worker, "ObjectStartCommand", strlen("ObjectStartCommand")))
		{ /*What we execute to start it.*/
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartCommand comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartCommand in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			snprintf(CurObj->ObjectStartCommand, MAX_LINE_SIZE, "%s", DelimCurr);
			

			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartCommand in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectStopCommand", strlen("ObjectStopCommand")))
		{ /*If it's "PID", then we know that we need to kill the process ID only. If it's "NONE", well, self explanitory.*/
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStopCommand comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopCommand in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}

			if (!strncmp(DelimCurr, "PID", strlen("PID")))
			{
				CurObj->Opts.StopMode = STOP_PID;
			}
			else if (!strncmp(DelimCurr, "PIDFILE", strlen("PIDFILE")))
			{ /*They want us to kill a PID file on exit.*/
				const char *Worker = DelimCurr;
				
				Worker += strlen("PIDFILE");
				
				while (*Worker == ' ' || *Worker == '\t')
				{ /*Skip past all spaces and tabs.*/
					++Worker;
				}
				
				snprintf(CurObj->ObjectPIDFile, MAX_LINE_SIZE, "%s", Worker);
				
				CurObj->Opts.StopMode = STOP_PIDFILE;
			}
			else if (!strncmp(DelimCurr, "NONE", strlen("NONE")))
			{
				CurObj->Opts.StopMode = STOP_NONE;
			}
			else
			{
				CurObj->Opts.StopMode = STOP_COMMAND;
				snprintf(CurObj->ObjectStopCommand, MAX_LINE_SIZE, "%s", DelimCurr);
			}
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartCommand in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectStartPriority", strlen("ObjectStartPriority")))
		{
			/*The order in which this item is started. If it is disabled in this runlevel, the next object in line is executed, IF
			 * and only IF it is enabled. If not, the one after that and so on.*/
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartPriority comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!AllNumeric(DelimCurr)) /*Make sure we are getting a number, not Shakespeare.*/
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Bad non-integer value for attribute ObjectStartPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			CurObj->ObjectStartPriority = atoi(DelimCurr);
			
			if (strlen(DelimCurr) >= 8)
			{ /*An eight digit number is too high.*/
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStartCommand in epoch.conf line %lu has\n"
						"abnormally high numeric value and may cause malfunctions.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectStopPriority", strlen("ObjectStopPriority")))
		{
			/*Same as above, but used for when the object is being shut down.*/
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStopPriority comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!AllNumeric(DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Bad non-integer value for attribute ObjectStopPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			CurObj->ObjectStopPriority = atoi(DelimCurr);
			
			if (strlen(DelimCurr) >= 8)
			{ /*An eight digit number is too high.*/
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectStopCommand in epoch.conf line %lu has\n"
						"abnormally high numeric value and may cause malfunctions.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectRunlevels", strlen("ObjectRunlevels")))
		{ /*Runlevel.*/
			char *TWorker;
			char TRL[MAX_DESCRIPT_SIZE], *TRL2;
			
			if (!CurObj)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectRunlevels comes before any ObjectID attribute, epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectRunlevels in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				ShutdownConfig();
				free(ConfigStream);
				return FAILURE;
			}
			
			TWorker = DelimCurr;
			
			do
			{
				for (TRL2 = TRL; *TWorker != ' ' && *TWorker != '\t' && *TWorker != '\n' && *TWorker != '\0'; ++TWorker, ++TRL2)
				{
					*TRL2 = *TWorker;
				}
				*TRL2 = '\0';
				
				ObjRL_AddRunlevel(TRL, CurObj);
				
			} while ((TWorker = NextSpace(TWorker)));
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Attribute ObjectRunlevels in epoch.conf line %lu has\n"
						"abnormally long value and may have been truncated.", LineNum);
				SpitWarning(TmpBuf);
			}
			
			continue;

		}
		else
		{ /*No big deal.*/
			char TmpBuf[1024];
			snprintf(TmpBuf, 1024, "Unidentified attribute in epoch.conf on line %lu.", LineNum);
			SpitWarning(TmpBuf);
			
			continue;
		}
	} while (++LineNum, (Worker = NextLine(Worker)));
	
	switch (ScanConfigIntegrity())
	{
		case NOTIFICATION:
		case SUCCESS:
			break;
		case FAILURE:
		{ /*We failed integrity checking.*/
			fprintf(stderr, "%s\n", "Enter \"d\" to dump epoch.conf to console or strike enter to continue.\n->");
			fflush(NULL); /*Have an eerie feeling this will be necessary on some systems.*/
			
			if (getchar() == 'd')
			{
				fprintf(stderr, CONSOLE_COLOR_MAGENTA "Beginning dump of epoch.conf to console.\n" CONSOLE_ENDCOLOR);
				fprintf(stderr, "%s", ConfigStream);
				fflush(NULL);
			}
			else
			{
				puts("Not dumping epoch.conf.");
			}
			
			ShutdownConfig();
			free(ConfigStream);
			
			return FAILURE;
		}
		case WARNING:
		{
			SpitWarning("Noncritical configuration problems exist.\nPlease edit epoch.conf to resolve these.");
			return WARNING;
		}
	}
		
	free(ConfigStream); /*Release ConfigStream, since we only use the object table now.*/

	return SUCCESS;
}

static rStatus GetLineDelim(const char *InStream, char *OutStream)
{
	unsigned long cOffset, Inc = 0;

	/*Jump to the first tab or space. If we get a newline or null, problem.*/
	while (InStream[Inc] != '\t' && InStream[Inc] != ' ' && InStream[Inc] != '\n' && InStream[Inc] != '\0') ++Inc;

	/*Hit a null or newline before tab or space. ***BAD!!!*** */
	if (InStream[Inc] == '\0' || InStream[Inc] == '\n')
	{
		char TmpBuf[1024];
		char ObjectInQuestion[1024];
		unsigned long IncT = 0;

		for (; InStream[IncT] != '\0' && InStream[IncT] != '\n'; ++IncT)
		{
			ObjectInQuestion[IncT] = InStream[IncT];
		}
		ObjectInQuestion[IncT] = '\0';

		snprintf(TmpBuf, 1024, "No parameter for attribute \"%s\" in epoch.conf.", ObjectInQuestion);

		SpitError(TmpBuf);

		return FAILURE;
	}

	/*Continue until we are past all tabs and spaces.*/
	while (InStream[Inc] == ' ' || InStream[Inc] == '\t') ++Inc;

	cOffset = Inc; /*Store this offset.*/

	/*Copy over the argument to the parameter. Quit whining about the loop copy.*/
	for (Inc = 0; InStream[Inc + cOffset] != '\n' && InStream[Inc + cOffset] != '\0' && Inc < MAX_LINE_SIZE - 1; ++Inc)
	{
		OutStream[Inc] = InStream[Inc + cOffset];
	}
	OutStream[Inc] = '\0';

	return SUCCESS;
}

rStatus EditConfigValue(const char *ObjectID, const char *Attribute, const char *Value)
{ /*Looks up the attribute for the passed ID and replaces the value for that attribute.*/
	
	/**Have fun reading this one, boys! Behehehehehh!!!
	 * I'm going to submit this to one of those bad code
	 * archive sites!**/
	char *Worker1, *Worker2, *Worker3;
	char *MasterStream, *HalfTwo;
	char LineWorker[2][MAX_LINE_SIZE];
	FILE *Descriptor;
	struct stat FileStat;
	unsigned long TempVal = 0;
	
	if (stat(CONFIGDIR CONF_NAME, &FileStat) != 0)
	{
		SpitError("EditConfigValue(): Failed to stat " CONFIGDIR CONF_NAME ". Does the file exist?");
		return FAILURE;
	}
	
	if ((Descriptor = fopen(CONFIGDIR CONF_NAME, "r")) == NULL)
	{
		SpitError("EditConfigValue(): Failed to open " CONFIGDIR CONF_NAME ". Are permissions correct?");
		return FAILURE;
	}
	
	MasterStream = malloc(FileStat.st_size + 1);
	
	fread(MasterStream, 1, FileStat.st_size, Descriptor);
	MasterStream[FileStat.st_size] = '\0';
	
	fclose(Descriptor);
	
	Worker1 = MasterStream;
	
	if (!(Worker1 = strstr(Worker1, ObjectID)))
	{
		snprintf(LineWorker[0], MAX_LINE_SIZE, "EditConfigValue(): No ObjectID %s present in epoch.conf.", ObjectID);
		SpitError(LineWorker[0]);
		free(MasterStream);
		return FAILURE;
	}
	
	Worker2 = Worker1;
	
	if ((Worker2 = strstr(Worker2, "ObjectID")))
	{
		*Worker2 = '\0';
	}

	if (!(Worker1 = strstr(Worker1, Attribute)))
	{
		snprintf(LineWorker[0], MAX_LINE_SIZE, "EditConfigValue(): Object %s specifies no %s attribute.", ObjectID, Attribute);
		SpitError(LineWorker[0]);
		free(MasterStream);
		return FAILURE;
	}
	
	if (Worker2) *Worker2 = 'O'; /*Letter O.*/
	
	/*Now copy in the line with our value.*/
	Worker2 = Worker1;
	Worker3 = LineWorker[1];
	
	
	for (; *Worker2 != '\n' && *Worker2 != '\0'; ++Worker2, ++Worker3)
	{
		*Worker3 = *Worker2;
	}
	*Worker3 = '\0';
	
	/*Now, terminate MasterStream at the beginning of our attribute, to keep it as a HalfOne for us.*/
	*Worker1 = '\0';
	
	/*Allocate and copy in HalfTwo, which is everything beyond our line.*/
	HalfTwo = malloc(strlen(Worker2) + 1);
	snprintf(HalfTwo, strlen(Worker2) + 1, "%s", Worker2);
	
	/*Edit the value.*/
	Worker3 = LineWorker[1];
	
	if (!strstr(Worker3, " "))
	{
		if (strlen(Worker3) < (MAX_LINE_SIZE - 1))
		{
			TempVal = strlen(Worker3);
			Worker3[TempVal++] = ' ';
			Worker3[TempVal] = '\0';
		}
		else
		{
			snprintf(LineWorker[0], MAX_LINE_SIZE, "EditConfigValue(): Malformed attribute %s for object %s: No value.",
					Attribute, ObjectID);
			SpitError(LineWorker[0]);
			
			free(HalfTwo);
			free(MasterStream);
			return FAILURE;
		}
		
	}
	
	for (; *Worker3 != ' ' && *Worker3 != '\n' &&
		*Worker3 != '\0'; ++Worker3) ++TempVal; /*We have to get to the spaces anyways. Harvest string length up until a space.*/
	for (; *Worker3 == ' '; ++Worker3) ++TempVal;
	
	snprintf(Worker3, MAX_LINE_SIZE - TempVal, "%s", Value);
	
	/*Now record it back to disk.*/
	if ((Descriptor = fopen(CONFIGDIR CONF_NAME, "w")))
	{
		MasterStream = realloc(MasterStream, (TempVal = strlen(MasterStream) + strlen(LineWorker[1]) + strlen(HalfTwo) + 1));
		
		/*We do a really ugly hack here. See first argument to snprintf().*/
		snprintf(&MasterStream[strlen(MasterStream)], TempVal, "%s%s", LineWorker[1], HalfTwo);
		
		fwrite(MasterStream, 1, strlen(MasterStream), Descriptor);
		fclose(Descriptor);
	}
	else
	{
		SpitError("EditConfigValue(): Unable to open " CONFIGDIR CONF_NAME " for writing. No write permission?");
	}
	
	free(MasterStream);
	free(HalfTwo);
	
	return SUCCESS;
}

/*Adds an object to the table and, if the first run, sets up the table.*/
static ObjTable *AddObjectToTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;
	
	/*See, we actually allocate two cells initially. The base and it's node.
	 * We always keep a free one open. This is just more convenient.*/
	if (ObjectTable == NULL)
	{
		ObjectTable = malloc(sizeof(ObjTable));
		ObjectTable->Prev = NULL;
		ObjectTable->Next = NULL;

		Worker = ObjectTable;
	}
	
	while (Worker->Next)
	{
		Worker = Worker->Next;
	}

	Worker->Next = malloc(sizeof(ObjTable));
	Worker->Next->Next = NULL;
	Worker->Next->Prev = Worker;

	/*This is the first thing that must ever be initialized, because it's how we tell objects apart.*/
	snprintf(Worker->ObjectID, MAX_DESCRIPT_SIZE, "%s", ObjectID);
	
	/*Initialize these to their default values. Used to test integrity before execution begins.*/
	Worker->Started = false;
	Worker->ObjectDescription[0] = '\0';
	Worker->ObjectStartCommand[0] = '\0';
	Worker->ObjectStopCommand[0] = '\0';
	Worker->ObjectPIDFile[0] = '\0';
	Worker->ObjectStartPriority = 0;
	Worker->ObjectStopPriority = 0;
	Worker->Opts.StopMode = STOP_NONE;
	Worker->Opts.CanStop = true;
	Worker->ObjectPID = 0;
	Worker->ObjectRunlevels = malloc(sizeof(struct _RLTree));
	Worker->ObjectRunlevels->Next = NULL;
	Worker->Opts.NoWait = false;
	Worker->Enabled = 2; /*We can indeed store this in a bool you know. There's no 1 bit datatype.*/
	Worker->Opts.HaltCmdOnly = false;
	Worker->Opts.RawDescription = false;
	Worker->Opts.IsService = false;
	Worker->Opts.AutoRestart = false;
	
	return Worker;
}

static rStatus ScanConfigIntegrity(void)
{ /*Here we check common mistakes and problems.*/
	ObjTable *Worker = ObjectTable, *TOffender;
	char TmpBuf[1024];
	rStatus RetState = SUCCESS;
	
	if (ObjectTable == NULL)
	{ /*This can happen if configuration is filled with trash and nothing valid.*/
		SpitError("No objects found in configuration or invalid configuration.");
		return FAILURE;
	}
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (*Worker->ObjectDescription == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has no attribute ObjectDescription.\n"
						"Changing description to \"missing description\".", Worker->ObjectID);
			SpitWarning(TmpBuf);
			
			snprintf(Worker->ObjectDescription, MAX_DESCRIPT_SIZE, "%s", 
					CONSOLE_COLOR_YELLOW "[missing description]" CONSOLE_ENDCOLOR);

			RetState = WARNING;
		}
		
		if (*Worker->ObjectStartCommand == '\0' && *Worker->ObjectStopCommand == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has neither ObjectStopCommand nor ObjectStartCommand attributes.", Worker->ObjectID);
			SpitWarning(TmpBuf);
			RetState = FAILURE;
		}
		
		if (!Worker->Opts.HaltCmdOnly && *Worker->ObjectStartCommand == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has no attribute ObjectStartCommand\nand is not set to HALTONLY.\n"
					"Disabling.", Worker->ObjectID);
			SpitWarning(TmpBuf);
			Worker->Enabled = false;
			RetState = WARNING;
		}
		
		if (Worker->ObjectRunlevels == NULL)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has no attribute ObjectRunlevels.", Worker->ObjectID);
			SpitWarning(TmpBuf);
			RetState = FAILURE;
		}
		
		if (Worker->Enabled == 2)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has no attribute ObjectEnabled.", Worker->ObjectID);
			SpitError(TmpBuf);
			RetState = FAILURE;
		}
		
		/*Check for duplicate ObjectIDs.*/
		for (TOffender = ObjectTable; TOffender->Next != NULL; TOffender = TOffender->Next)
		{
			if (!strcmp(Worker->ObjectID, TOffender->ObjectID) && Worker != TOffender)
			{
				snprintf(TmpBuf, 1024, "Two objects in configuration with ObjectID \"%s\".", Worker->ObjectID);
				SpitError(TmpBuf);
				RetState = FAILURE;
			}			
		}
	}
	
	for (Worker = ObjectTable; Worker->Next != NULL; Worker = Worker->Next)
	{ /*Check for duplicate start/stop priorities.*/
		Bool IsStartPriority;
		
		for (TOffender = ObjectTable; TOffender->Next != NULL; TOffender = TOffender->Next)
		{			
			if (Worker->ObjectStartPriority != 0 && TOffender->ObjectStartPriority == Worker->ObjectStartPriority && 
				Worker != TOffender)
			{
				IsStartPriority = true;
			}
			else if ( Worker->ObjectStopPriority != 0 && TOffender->ObjectStopPriority == Worker->ObjectStopPriority &&
					Worker != TOffender)
			{
				IsStartPriority = false;
			}
			else
			{
				continue;
			}
			
			snprintf(TmpBuf, 1024, "Two objects in configuration with the same %s priority.\n"
									"They are \"%s\" and \"%s\". This could lead to strange behaviour.", 
									(IsStartPriority ? "start" : "stop"), Worker->ObjectID, TOffender->ObjectID);
			SpitWarning(TmpBuf);
			RetState = WARNING;
		}
	}
	return RetState;
}
	
/*Find an object in the table and return a pointer to it. This function is public
 * because while we don't want other places adding to the table, we do want read
 * access to the table.*/
ObjTable *LookupObjectInTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;

	if (!ObjectTable)
	{
		return NULL;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(Worker->ObjectID, ObjectID))
		{
			return Worker;
		}
	}

	return NULL;
}

/*Get the max priority number we need to scan.*/
unsigned long GetHighestPriority(Bool WantStartPriority)
{
	ObjTable *Worker = ObjectTable;
	unsigned long CurHighest = 0;
	unsigned long TempNum;
	
	if (!ObjectTable)
	{
		return 0;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		TempNum = (WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority);
		
		if (TempNum > CurHighest)
		{
			CurHighest = TempNum;
		}
		else if (TempNum == 0)
		{ /*We always skip anything with a priority of zero. That's like saying "DISABLED".*/
			continue;
		}
	}
	
	return CurHighest;
}

/*Functions for runlevel management.*/
Bool ObjRL_CheckRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (!strcmp(Worker->RL, InRL))
		{
			return true;
		}
	}
	
	return false;
}
	
void ObjRL_AddRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	while (Worker->Next != NULL) Worker = Worker->Next;
	
	Worker->Next = malloc(sizeof(struct _RLTree));
	Worker->Next->Next = NULL;
	Worker->Next->Prev = Worker;
	
	snprintf(Worker->RL, MAX_DESCRIPT_SIZE, "%s", InRL);
}

Bool ObjRL_DelRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (!strcmp(InRL, Worker->RL))
		{
			Worker->Next->Prev = Worker->Prev;
			
			if (Worker->Prev)
			{
				Worker->Prev->Next = Worker->Next;
			}
			else if (Worker->Next->Next)
			{
				InObj->ObjectRunlevels = Worker->Next;
			}
			else
			{
				InObj->ObjectRunlevels = NULL;
				free(Worker->Next);
			}
			
			free(Worker);
			
			return true;
		}
	}
	
	return false;
}

void ObjRL_ShutdownRunlevels(ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels, *NDel;
	
	for (; Worker != NULL; Worker = NDel)
	{
		NDel = Worker->Next;
		free(Worker);
	}
	
	InObj->ObjectRunlevels = NULL;
}
	

ObjTable *GetObjectByPriority(const char *ObjectRunlevel, Bool WantStartPriority, unsigned long ObjectPriority)
{ /*The primary lookup function to be used when executing commands.*/
	ObjTable *Worker = ObjectTable;
	
	if (!ObjectTable)
	{
		return NULL;
	}
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if ((ObjectRunlevel == NULL || ObjRL_CheckRunlevel(ObjectRunlevel, Worker)) && 
		/*As you can see by below, I obfuscate with efficiency!*/
		(WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority) == ObjectPriority)
		{
			return Worker;
		}
	}
	
	return NULL;
}

void ShutdownConfig(void)
{
	ObjTable *Worker = ObjectTable, *Temp;

	for (; Worker != NULL; Worker = Temp)
	{
		if (Worker->Next)
		{
			ObjRL_ShutdownRunlevels(Worker);
		}
		
		Temp = Worker->Next;
		free(Worker);
	}
	
	ObjectTable = NULL;
}

rStatus ReloadConfig(void)
{ /*This function is somewhat hard to read, but it does the job well.*/
	ObjTable *Worker = ObjectTable;
	ObjTable *TRoot = malloc(sizeof(ObjTable)), *SWorker = TRoot, *Temp;
	struct _RLTree *RLTemp1, *RLTemp2;
	
	for (; Worker->Next != NULL; Worker = Worker->Next, SWorker = SWorker->Next)
	{
		*SWorker = *Worker; /*Direct as-a-unit copy of the main list node to the backup list node.*/
		SWorker->Next = malloc(sizeof(ObjTable));
		SWorker->Next->Next = NULL;
		SWorker->Next->Prev = SWorker;
		RLTemp2 = SWorker->ObjectRunlevels = malloc(sizeof(struct _RLTree));
		
		for (RLTemp1 = Worker->ObjectRunlevels; RLTemp1->Next; RLTemp1 = RLTemp1->Next)
		{
			*RLTemp2 = *RLTemp1;
			RLTemp2->Next = malloc(sizeof(struct _RLTree));
			RLTemp2->Next->Next = NULL;
			RLTemp2->Next->Prev = RLTemp2;
			RLTemp2 = RLTemp2->Next;
		}
	}

	/*Actually do the reload of the config.*/
	ShutdownConfig();
	if (!InitConfig())
	{
		SpitError("ReloadConfig(): Failed to reload configuration.\n"
					"Restoring old configuration to memory.\n"
					"Please check epoch.conf for syntax errors.");
		ObjectTable = TRoot; /*Point ObjectTable to our new, identical copy of the old tree.*/
		return FAILURE;
	}
	
	for (SWorker = TRoot; SWorker->Next != NULL; SWorker = Temp)
	{ /*Add back the Started states, so we don't forget to stop services, etc.*/
		if ((Worker = LookupObjectInTable(SWorker->ObjectID)))
		{
			Worker->Started = SWorker->Started;
		}
		
		ObjRL_ShutdownRunlevels(SWorker);
		Temp = SWorker->Next;
		free(SWorker);
	}
	free(SWorker);
	
	puts(CONSOLE_COLOR_GREEN "Epoch: Configuration reloaded." CONSOLE_ENDCOLOR);
	return SUCCESS;
}
