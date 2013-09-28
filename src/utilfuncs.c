/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file contains functions and utilities used across Epoch
 * for miscellanious purposes, that don't really belong in a category of their own.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include "epoch.h"

/**Constants**/
Bool EnableLogging = false;
Bool LogInMemory = true; /*This is necessary so long as we have a readonly filesystem.*/
char *MemLogBuffer = NULL;

/*Days in the month, for time stuff.*/
static const short MDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

Bool AllNumeric(const char *InStream)
{ /*Is the string all numbers?*/
	if (!*InStream)
	{ /*No data? Don't say it's all numeric then.*/
		return false;
	}
	
	for (; *InStream != '\0'; ++InStream)
	{
		if (!isdigit(*InStream))
		{
			return false;
		}
	}
	
	return true;
}

rStatus WriteLogLine(const char *InStream, Bool AddDate)
{ /*This is pretty much the entire logging system.*/
	FILE *Descriptor = NULL;
	char Hr[16], Min[16], Sec[16], Month[16], Day[16], Year[16], OBuf[MAX_LINE_SIZE + 64] = { '\0' };
	static Bool FailedBefore = false;
	
	if (!EnableLogging)
	{
		return SUCCESS;
	}
	
	if (!LogInMemory && !(Descriptor = fopen(LOGDIR LOGFILE_NAME, "a")))
	{
		if (!FailedBefore)
		{
			FailedBefore = true;
			SpitWarning("Cannot write to log file. Log system is inoperative. Check permissions?");
		}
		
		return FAILURE;
	}
	
	GetCurrentTime(Hr, Min, Sec, Month, Day, Year);
	
	if (AddDate)
	{
		snprintf(OBuf, MAX_LINE_SIZE + 64, "[%s:%s:%s | %s/%s/%s] %s\n", Hr, Min, Sec, Month, Day, Year, InStream);
	}
	else
	{
		snprintf(OBuf, MAX_LINE_SIZE, "%s\n", InStream);
	}
	
	if (LogInMemory)
	{
		if (MemLogBuffer == NULL)
		{
			MemLogBuffer = malloc(1);
			*MemLogBuffer = '\0';
		}
		
		MemLogBuffer = realloc(MemLogBuffer, strlen(MemLogBuffer) + strlen(OBuf) + 1);
		
		strncat(MemLogBuffer, OBuf, strlen(OBuf));
	}
	else
	{
		fwrite(OBuf, 1, strlen(OBuf), Descriptor);
		
		fflush(Descriptor);
		fclose(Descriptor);
	}
	
	return SUCCESS;
}
	

Bool ObjectProcessRunning(const ObjTable *InObj)
{ /*Checks /proc for a directory with the name of the requested process.*/
	DIR *DCore;
	struct dirent *DStruct;
	pid_t InPID;
	

	if (InObj->Opts.StopMode != STOP_PIDFILE || !(InPID = ReadPIDFile(InObj)))
	{ /*We got a PID file requested and present? Get PID from that, otherwise 
		* get the PID from memory.*/
		InPID = InObj->ObjectPID;
	}
	
	DCore = opendir("/proc/");
	
	while ((DStruct = readdir(DCore)))
	{
		if (AllNumeric(DStruct->d_name) && DStruct->d_type == 4)
		{
			if (atoi(DStruct->d_name) == InPID)
			{
				closedir(DCore);
				return true;
			}
		}
	}
	closedir(DCore);
	
	return false;
}

void MinsToDate(unsigned long MinInc, unsigned long *OutHr, unsigned long *OutMin,
				unsigned long *OutMonth, unsigned long *OutDay, unsigned long *OutYear)
{  /*Returns the projected date that it will be after MinInc minutes.
	* Not really a good example of a function that belongs in console.c.*/
	time_t CoreClock;
	struct tm *TimeStruct = NULL;
	unsigned long Hr, Min, Day, Mon, Year;
	
	time(&CoreClock);
	TimeStruct = localtime(&CoreClock);
	
	Hr = TimeStruct->tm_hour;
	Min = TimeStruct->tm_min;
	Mon = TimeStruct->tm_mon + 1;
	Day = TimeStruct->tm_mday;
	Year = TimeStruct->tm_year + 1900;
	
	for (; MinInc; --MinInc)
	{
		if (Min + 1 == 60)
		{
			Min = 0;
			
			if (Hr + 1 == 24)
			{
				Hr = 0;
				
				if (Day == MDays[Mon - 1])
				{
					Day = 1;
					
					if (Mon == 12)
					{
						Mon = 1;
						
						++Year;
					}
					else
					{
						++Mon;
					}
				}
				else
				{
					++Day;
				}
	
			}
			else
			{
				++Hr;
			}
		}
		else
		{
			++Min;
		}
	}
	
	*OutHr = Hr;
	*OutMin = Min;
	*OutMonth = Mon;
	*OutDay = Day;
	*OutYear = Year;
}

unsigned long DateDiff(unsigned long InHr, unsigned long InMin, unsigned long *OutMonth,
						unsigned long *OutDay, unsigned long *OutYear)
{ /*Provides a true date as to when the next occurrence of this hour and minute will return via pointers, and
	* also provides the number of minutes that will elapse during the time between. You can pass NULL for the pointers.*/
	struct tm *TimeP;
	time_t CoreClock;
	unsigned long Hr, Min, Month, Day, Year, IncMin = 0;
	
	time(&CoreClock);
	TimeP = localtime(&CoreClock);
	
	Hr = TimeP->tm_hour;
	Min = TimeP->tm_min;
	Month = TimeP->tm_mon + 1;
	Day = TimeP->tm_mday;
	Year = TimeP->tm_year + 1900;
	
	for (; Hr != InHr || Min != InMin; ++IncMin)
	{
		if (Min == 60)
		{
			Min = 0;
			
			if (Hr + 1 == 24)
			{
				Hr = 0;
				
				if (Day == MDays[Month - 1])
				{
					Day = 1;
					
					if (Month == 12)
					{
						Month = 1;
						
						++Year;
					}
					else
					{
						++Month;
					}
				}
				else
				{
					++Day;
				}
			}
			else
			{
				++Hr;
			}
		}
		else
		{
			++Min;
		}
	}
	
	if (OutMonth) *OutMonth = Month;
	if (OutDay) *OutDay = Day;
	if (OutYear) *OutYear = Year;
	
	return IncMin;
}

void GetCurrentTime(char *OutHr, char *OutMin, char *OutSec, char *OutMonth, char *OutDay, char *OutYear)
{ /*You can put NULL for items that you don't want the value of.*/
	struct tm *TimeP;
	long HMS_I[3];
	long MDY_I[3];
	char *HMS[3];
	char *MDY[3];
	short Inc = 0;
	time_t TimeT;
	
	/*Compiler whines if I try to initialize these.*/
	HMS[0] = OutHr;
	HMS[1] = OutMin;
	HMS[2] = OutSec;
	
	MDY[0] = OutMonth;
	MDY[1] = OutDay;
	MDY[2] = OutYear;
	
	/*Actually get the time.*/
	time(&TimeT);
	TimeP = localtime(&TimeT);
	
	HMS_I[0] = TimeP->tm_hour;
	HMS_I[1] = TimeP->tm_min;
	HMS_I[2] = TimeP->tm_sec;
	
	MDY_I[0] = TimeP->tm_mon + 1;
	MDY_I[1] = TimeP->tm_mday;
	MDY_I[2] = TimeP->tm_year + 1900;
	
	for (; Inc < 3; ++Inc)
	{
		if (HMS[Inc] == NULL)
		{
			continue;
		}
		
		snprintf(HMS[Inc], 16, (HMS_I[Inc] >= 10 ? "%ld" : "0%ld"), HMS_I[Inc]);
	}
	
	for (Inc = 0; Inc < 3; ++Inc)
	{
		if (MDY[Inc] != NULL)
		{
			snprintf(MDY[Inc], 16, (MDY_I[Inc] >= 10 ? "%ld" : "0%ld"), MDY_I[Inc]);
		}
	}
}

unsigned long ReadPIDFile(const ObjTable *InObj)
{
	FILE *PIDFileDescriptor = fopen(InObj->ObjectPIDFile, "r");
	char TChar, PIDBuf[MAX_LINE_SIZE], *TW = NULL, *TW2 = NULL;
	unsigned long InPID = 0, Inc = 0;
	
	if (!PIDFileDescriptor)
	{
		return 0; /*Zero for failure.*/
	}
	
	for (; (TChar = getc(PIDFileDescriptor)) != EOF && Inc < (MAX_LINE_SIZE - 1); ++Inc)
	{
		PIDBuf[Inc] = TChar;
	}
	PIDBuf[Inc] = '\0';
	
	fclose(PIDFileDescriptor);
	
	for (TW = PIDBuf; *TW == '\n' || *TW == '\t' || *TW == ' '; ++TW); /*Skip past initial junk if any.*/
	
	for (TW2 = TW; *TW2 != '\0' && *TW2 != '\t' && *TW2 != '\n' && *TW2 != ' '; ++TW2); /*Delete any following the number.*/
	*TW2 = '\0';
	
	if (AllNumeric(PIDBuf))
	{
		InPID = atoi(PIDBuf);
	}
	else
	{
		return 0;
	}
	
	return InPID;
}

