/*This code is part of Epoch. Epoch is maintained by Subsentient.
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
static ObjTable *ObjectTable = NULL;

/*Function forward declarations for all the statics.*/
static ObjTable *AddObjectToTable(const char *ObjectID);
static char *NextLine(char *InStream);
static rStatus GetLineDelim(const char *InStream, char *OutStream);
static void SetBannerColor(const char *InChoice);
static rStatus ScanConfigIntegrity(void);

/*Actual functions.*/
static char *NextLine(char *InStream)
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

	return InStream;
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
		ConfigStream = malloc(FileStat.st_size);
	}

	Descriptor = fopen(CONFIGDIR CONF_NAME, "r"); /*Open the configuration file.*/

	/*Read the file into memory. I don't really trust fread(), but oh well.
	 * People will whine if I use a loop instead.*/
	fread(ConfigStream, 1, FileStat.st_size, Descriptor);
	fclose(Descriptor); /*Close the file.*/

	Worker = ConfigStream;

	/*Empty file?*/
	if (*Worker == '\n' || *Worker == '\0')
	{
		SpitError("Seems that epoch.conf is empty or corrupted.");
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
		/*Now we get into the actual attribute tags.*/
		else if (!strncmp(Worker, "BootBannerText", strlen("BootBannerText")))
		{ /*The text shown at boot up as a kind of greeter, before we start executing objects. Can be disabled, off by default.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute BootBannerText in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*So, they decided to explicitly opt out of banner display. Ok.*/
			{
				BootBanner.BannerText[0] = '\0';
				BootBanner.BannerColor[0] = '\0';
				BootBanner.ShowBanner = false; /*Should already be false, but to prevent possible bugs...*/
				continue;
			}
			strncat(BootBanner.BannerText, DelimCurr, 512);
			
			BootBanner.ShowBanner = true;
			continue;
		}
		else if (!strncmp(Worker, "BootBannerColor", strlen("BootBannerColor")))
		{ /*Color for boot banner.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute BootBannerColor in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
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
		else if (!strncmp(Worker, "ObjectID", strlen("ObjectID")))
		{ /*ASCII value used to identify this object internally, and also a kind of short name for it.*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectID in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			CurObj = AddObjectToTable(DelimCurr); /*Sets this as our current object.*/

			continue;
		}
		else if (!strncmp(Worker, "ObjectEnabled", strlen("ObjectEnabled")))
		{
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectEnabled in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
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
			{ /*Warn about bad value, then treat as if enabled.*/
				char TmpBuf[1024];
				
				CurObj->Enabled = true;
				
				snprintf(TmpBuf, 1024, "Bad value %s for attribute ObjectEnabled for object %s at line %lu.\n"
						"Valid values are true and false. Assuming enabled.",
						DelimCurr, CurObj->ObjectID, LineNum);

				SpitWarning(TmpBuf);
			}
			
			continue;
		}
		else if (!strncmp(Worker, "ObjectName", strlen("ObjectName")))
		{ /*It's description.*/
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectName in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			strncpy(CurObj->ObjectName, DelimCurr, MAX_DESCRIPT_SIZE);

			continue;
		}
		else if (!strncmp(Worker, "ObjectStartCommand", strlen("ObjectStartCommand")))
		{ /*What we execute to start it.*/
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartCommand in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			strncpy(CurObj->ObjectStartCommand, DelimCurr, MAX_LINE_SIZE);
			continue;
		}
		else if (!strncmp(Worker, "ObjectStopCommand", strlen("ObjectStopCommand")))
		{ /*If it's "PID", then we know that we need to kill the process ID only. If it's "NONE", well, self explanitory.*/
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopCommand in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			if (!strncmp(DelimCurr, "PID", strlen("PID")))
			{
				CurObj->StopMode = STOP_PID;
			}
			else if (!strncmp(DelimCurr, "PIDFILE", strlen("PIDFILE")))
			{ /*They want us to kill a PID file on exit.*/
				char *Worker = DelimCurr;
				
				Worker += strlen("PIDFILE");
				
				while (*Worker == ' ')
				{ /*Skip past all spaces.*/
					++Worker;
				}
				
				strncpy(CurObj->ObjectPIDFile, Worker, MAX_LINE_SIZE);
				
				CurObj->StopMode = STOP_PIDFILE;
			}
			else if (!strncmp(DelimCurr, "NONE", strlen("NONE")))
			{
				CurObj->StopMode = STOP_NONE;
			}
			else
			{
				CurObj->StopMode = STOP_COMMAND;
				strncpy(CurObj->ObjectStopCommand, DelimCurr, MAX_LINE_SIZE);
			}
			continue;
		}
		else if (!strncmp(Worker, "ObjectStartPriority", strlen("ObjectStartPriority")))
		{
			/*The order in which this item is started. If it is disabled in this runlevel, the next object in line is executed, IF
			 * and only IF it is enabled. If not, the one after that and so on.*/
			 if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!isdigit(DelimCurr[0])) /*Make sure we are getting a number, not Shakespeare.*/
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Bad non-integer value for attribute ObjectStartPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			CurObj->ObjectStartPriority = atoi(DelimCurr);
			continue;
		}
		else if (!strncmp(Worker, "ObjectStopPriority", strlen("ObjectStopPriority")))
		{
			/*Same as above, but used for when the object is being shut down.*/
			 if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			if (!isdigit(DelimCurr[0]))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Bad non-integer value for attribute ObjectStopPriority in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}
			
			CurObj->ObjectStopPriority = atoi(DelimCurr);
			continue;
		}
		else if (!strncmp(Worker, "ObjectRunlevel", strlen("ObjectRunLevel")))
		{ /*Runlevel.*/
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectRunlevel in epoch.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			strncpy(CurObj->ObjectRunlevel, DelimCurr, MAX_LINE_SIZE);
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
	
	if (!ScanConfigIntegrity())
	{ /*We failed integrity checking.*/
		fprintf(stderr, CONSOLE_COLOR_MAGENTA "Beginning dump of epoch.conf to console.\n" CONSOLE_ENDCOLOR);
		fprintf(stderr, "%s", ConfigStream);
		fflush(NULL);
		
		return FAILURE;
	}
		
	free(ConfigStream); /*Release ConfigStream, since we only use the object table now.*/

	return SUCCESS;
}

static void SetBannerColor(const char *InChoice)
{
	if (!strcmp(InChoice, "BLACK"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_BLACK, 64);
	}
	else if (!strcmp(InChoice, "BLUE"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_BLUE, 64);
	}
	else if (!strcmp(InChoice, "RED"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_RED, 64);
	}
	else if (!strcmp(InChoice, "GREEN"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_GREEN, 64);
	}
	else if (!strcmp(InChoice, "YELLOW"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_YELLOW, 64);
	}
	else if (!strcmp(InChoice, "MAGENTA"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_MAGENTA, 64);
	}
	else if (!strcmp(InChoice, "CYAN"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_CYAN, 64);
	}
	else if (!strcmp(InChoice, "WHITE"))
	{
		strncpy(BootBanner.BannerColor, CONSOLE_COLOR_WHITE, 64);
	}
	else
	{ /*Bad value? Warn and then set no color.*/
		char TmpBuf[1024];
		
		BootBanner.BannerColor[0] = '\0';
		snprintf(TmpBuf, 1024, "Bad color value \"%s\" specified for boot banner. Setting no color.", InChoice);
		SpitWarning(TmpBuf);
	}
}

static rStatus GetLineDelim(const char *InStream, char *OutStream)
{
	unsigned long cOffset, Inc;

	/*Jump to the first tab or space. If we get a newline or null, problem.*/
	for (Inc = 0; InStream[Inc] != '\t' && InStream[Inc] != ' ' && InStream[Inc] != '\n' && InStream[Inc] != '\0'; ++Inc);

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

		snprintf(TmpBuf, 1024, "No parameter for identifier \"%s\" in epoch.conf.", ObjectInQuestion);

		SpitError(TmpBuf);

		return FAILURE;
	}

	/*Continue until we are past all tabs and spaces.*/
	for (; InStream[Inc] == ' ' || InStream[Inc] == '\t'; ++Inc);

	cOffset = Inc; /*Store this offset.*/

	/*Copy over the argument to the parameter. Quit whining about the loop copy.*/
	for (Inc = 0; InStream[Inc + cOffset] != '\n' && InStream[Inc + cOffset] != '\0' && Inc < MAX_LINE_SIZE; ++Inc)
	{
		OutStream[Inc] = InStream[Inc + cOffset];
	}
	OutStream[Inc] = '\0';

	return SUCCESS;
}

/*Adds an object to the table and, if the first run, sets up the table.*/
static ObjTable *AddObjectToTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;
	static Bool FirstTime = true;

	/*See, we actually allocate two cells initially. The base and it's node.
	 * We always keep a free one open. This is just more convenient.*/
	if (FirstTime)
	{
		FirstTime = false;

		ObjectTable = malloc(sizeof(ObjTable));
		ObjectTable->Next = malloc(sizeof(ObjTable));
		ObjectTable->Next->Next = NULL;
		ObjectTable->Next->Prev = ObjectTable;
		ObjectTable->Prev = NULL;

		Worker = ObjectTable;
	}
	else
	{
		while (Worker->Next)
		{
			Worker = Worker->Next;
		}

		Worker->Next = malloc(sizeof(ObjTable));
		Worker->Next->Next = NULL;
		Worker->Next->Prev = Worker;
	}

	/*This is the first thing that must ever be initialized, because it's how we tell objects apart.*/
	strncpy(Worker->ObjectID, ObjectID, MAX_DESCRIPT_SIZE);
	
	/*Initialize these to their default values. Used to test integrity before execution begins.*/
	Worker->Started = false;
	Worker->ObjectName[0] = '\0';
	Worker->ObjectStartCommand[0] = '\0';
	Worker->ObjectStopCommand[0] = '\0';
	Worker->ObjectPIDFile[0] = '\0';
	Worker->ObjectStartPriority = 0;
	Worker->ObjectStopPriority = 0;
	Worker->StopMode = STOP_INVALID;
	Worker->ObjectPID = 0;
	Worker->ObjectRunlevel[0] = '\0';
	Worker->Enabled = true; /*Don't make ObjectEnabled attribute mandatory*/
	
	return Worker;
}

static rStatus ScanConfigIntegrity(void)
{ /*Here we check common mistakes and problems.*/
	ObjTable *Worker = ObjectTable, *TOffender;
	char TmpBuf[1024];
	
	while (Worker->Next)
	{
		if (*Worker->ObjectName == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has no attribute ObjectName.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (*Worker->ObjectStartCommand == '\0' && *Worker->ObjectStopCommand == '\0')
		{
			snprintf(TmpBuf, 1024, "Object %s has neither ObjectStopCommand nor ObjectStartCommand attributes.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (Worker->StopMode == STOP_INVALID)
		{
			snprintf(TmpBuf, 1024, "Internal error when loading StopMode for Object \"%s\".", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (*Worker->ObjectRunlevel == '\0')
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has no attribute ObjectRunlevel.", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		else if (Worker->Prev != NULL && !strcmp(Worker->Prev->ObjectID, Worker->ObjectID))
		{
			snprintf(TmpBuf, 1024, "Two objects in configuration with ObjectID \"%s\".", Worker->ObjectID);
			SpitError(TmpBuf);
			return FAILURE;
		}
		
		Worker = Worker->Next;
	}
	
	Worker = ObjectTable;
	
	while (Worker->Next)
	{
		if (((TOffender = GetObjectByPriority(Worker->ObjectRunlevel, true, Worker->ObjectStartPriority)) != NULL ||
			(TOffender = GetObjectByPriority(Worker->ObjectRunlevel, false, Worker->ObjectStopPriority)) != NULL) &&
			strcmp(TOffender->ObjectID, Worker->ObjectID) != 0 && TOffender->Enabled && Worker->Enabled)
		{ /*We got a priority collision.*/
			snprintf(TmpBuf, 1024, "Two objects in configuration with the same priority.\n"
			"They are \"%s\" and \"%s\". This could lead to strange behaviour.", Worker->ObjectID, TOffender->ObjectID);
			SpitWarning(TmpBuf);
			return WARNING;
		}
		
		Worker = Worker->Next;
	}
	return SUCCESS;
}
	
/*Find an object in the table and return a pointer to it. This function is public
 * because while we don't want other places adding to the table, we do want read
 * access to the table.*/
ObjTable *LookupObjectInTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;

	while (Worker->Next)
	{
		if (!strcmp(Worker->ObjectID, ObjectID))
		{
			return Worker;
		}
		Worker = Worker->Next;
	}

	return NULL;
}

/*Get the max priority number we need to scan.*/
unsigned long GetHighestPriority(Bool WantStartPriority)
{
	ObjTable *Worker = ObjectTable;
	unsigned long CurHighest = 0;
	unsigned long TempNum;
	
	while (Worker->Next)
	{
		TempNum = (WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority);
		
		if (TempNum > CurHighest)
		{
			CurHighest = TempNum;
		}
		else if (TempNum == 0)
		{ /*We always skip anything with a priority of zero. That's like saying "DISABLED".*/
			Worker = Worker->Next;
			continue;
		}
		
		Worker = Worker->Next;
	}
	
	return CurHighest;
}

ObjTable *GetObjectByPriority(const char *ObjectRunlevel, Bool WantStartPriority, unsigned long ObjectPriority)
{ /*The primary lookup function to be used when executing commands.*/
	ObjTable *Worker = ObjectTable;
	
	while (Worker->Next)
	{
		if (!strcmp(Worker->ObjectRunlevel, ObjectRunlevel) &&  /*As you can see by below, I obfuscate with efficiency!*/
		(WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority) == ObjectPriority)
		{
			return Worker;
		}
		Worker = Worker->Next;
	}
	
	return NULL;
}

void ShutdownConfig(void)
{
	ObjTable *Worker = ObjectTable, *Temp;

	while (Worker != NULL)
	{
		Temp = Worker->Next;
		free(Worker);
		Worker = Temp;
	}
}
