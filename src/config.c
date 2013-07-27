/*This code is part of Mauri. Mauri is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file handles the parsing of mauri.conf, our configuration file.
 * It adds everything into the object table.**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../mauri.h"

/*We want the only interface for this to be LookupObjectInTable().*/
static ObjTable *ObjectTable = NULL;

/*Function forward declarations for all the statics.*/
static ObjTable *AddObjectToTable(unsigned long ObjectID);
static char *NextLine(char *InStream);
static rStatus GetLineDelim(const char *InStream, char *OutStream);

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
		SpitError("Failed to obtain information about configuration file mauri.conf. Does it exist?");
		return FAILURE;
	}
	else
	{ /*No? Use the file size to allocate space in memory, since a char is a byte big.
	* If it's not a byte on your platform, your OS is not UNIX, and Mauri was not designed for you.*/
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
		SpitError("Seems that mauri.conf is empty or corrupted.");
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
		else if (!strncmp(Worker, "ObjectID", strlen("ObjectID")))
		{ /*Used as both identifier and order of execution specifier.*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectID in mauri.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			CurObj = AddObjectToTable(atoi(DelimCurr)); /*Sets this as our current object.*/

			continue;
		}
		else if (!strncmp(Worker, "ObjectName", strlen("ObjectName")))
		{ /*It's description.*/
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectName in mauri.conf line %lu.", LineNum);
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
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStartCommand in mauri.conf line %lu.", LineNum);
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
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectStopCommand in mauri.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			if (!strncmp(DelimCurr, "PID", MAX_DESCRIPT_SIZE))
			{
				CurObj->StopMode = STOP_PID;
			}
			else if (!strncmp(DelimCurr, "NONE", MAX_DESCRIPT_SIZE))
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
		else if (!strncmp(Worker, "ObjectRunlevel", strlen("ObjectRunLevel")))
		{ /*Runlevel.*/
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				char TmpBuf[1024];
				snprintf(TmpBuf, 1024, "Missing or bad value for attribute ObjectRunlevel in mauri.conf line %lu.", LineNum);
				SpitError(TmpBuf);
				
				return FAILURE;
			}

			strncpy(CurObj->ObjectRunlevel, DelimCurr, MAX_DESCRIPT_SIZE);
			continue;
		}
		else
		{
			char TmpBuf[1024];
			snprintf(TmpBuf, 1024, "Unidentified attribute in mauri.conf on line %lu.", LineNum);
			SpitError(TmpBuf);
			
			return FAILURE;
		}
	} while (++LineNum, (Worker = NextLine(Worker)));

	free(ConfigStream); /*Release ConfigStream, since we only use the object table now.*/

	return SUCCESS;
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

		snprintf(TmpBuf, 1024, "No parameter for identifier \"%s\" in mauri.conf.", ObjectInQuestion);

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
static ObjTable *AddObjectToTable(unsigned long ObjectID)
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
	}

	Worker->ObjectID = ObjectID;

	return Worker;
}

/*Find an object in the table and return a pointer to it. This function is public
 * because while we don't want other places adding to the table, we do want read
 * access to the table.*/
ObjTable *LookupObjectInTable(unsigned long ObjectID)
{
	ObjTable *Worker = ObjectTable;

	while (Worker->Next)
	{
		if (Worker->ObjectID == ObjectID)
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
