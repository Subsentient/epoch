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
/*The banner we show upon startup.*/
struct _BootBanner BootBanner = { false, { '\0' }, { '\0' } };

/*Function forward declarations for all the statics.*/
static ObjTable *AddObjectToTable(const char *ObjectID);
static char *NextLine(char *InStream);
static rStatus GetLineDelim(const char *InStream, char *OutStream);
static void SetBannerColor(const char *InChoice);

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
	char DelimCurr[MAX_DESCRIPT_SIZE];
	unsigned long LineNum = 0;
	
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
			snprintf(BootBanner.BannerText, 256, "%s%s\n\n", DelimCurr, CONSOLE_ENDCOLOR); /*We add ENDCOLOR to the end regardless of if we are going to show a color.*/
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
			
			strncpy(CurObj->ObjectStartCommand, DelimCurr, MAX_DESCRIPT_SIZE);
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
				
				strncpy(CurObj->ObjectPIDFile, Worker, MAX_DESCRIPT_SIZE);
				
				CurObj->StopMode = STOP_PIDFILE;
			}
			else if (!strncmp(DelimCurr, "NONE", strlen("NONE")))
			{
				CurObj->StopMode = STOP_NONE;
			}
			else
			{
				CurObj->StopMode = STOP_COMMAND;
				strncpy(CurObj->ObjectStopCommand, DelimCurr, MAX_DESCRIPT_SIZE);
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

			strncpy(CurObj->ObjectRunlevel, DelimCurr, MAX_DESCRIPT_SIZE);
			continue;
		}
		else
		{
			char TmpBuf[1024];
			snprintf(TmpBuf, 1024, "Unidentified attribute in epoch.conf on line %lu.", LineNum);
			SpitError(TmpBuf);
			
			return FAILURE;
		}
	} while (++LineNum, (Worker = NextLine(Worker)));

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
	for (Inc = 0; InStream[Inc + cOffset] != '\n' && InStream[Inc + cOffset] != '\0'; ++Inc)
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

	strncpy(Worker->ObjectID, ObjectID, MAX_DESCRIPT_SIZE);
	Worker->Started = false;

	return Worker;
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
		else if (TempNum == CurHighest)
		{ /*Bad. BAD. BAAAD.*/
			char TmpBuf[1024];
			
			snprintf(TmpBuf, 1024, "Objects %s and %s have the same priority!\n"
							"That's really bad. Please fix this when you can.\n"
							"%s will not be executed.", Worker->Prev->ObjectID, Worker->ObjectID, Worker->ObjectID);
			SpitError(TmpBuf);
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
