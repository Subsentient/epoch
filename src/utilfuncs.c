/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

/**This file contains functions and utilities used across Epoch
 * for miscellanious purposes, that don't really beint in a category of their own.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>

#include "epoch.h"

/**Constants**/
Bool EnableLogging = true;
Bool LogInMemory = true; /*This is necessary so int as we have a readonly filesystem.*/
Bool BlankLogOnBoot = true;
char *MemLogBuffer;

/*Days in the month, for time stuff.*/
static const unsigned char MDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

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

ReturnCode WriteLogLine(const char *InStream, Bool AddDate)
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
	
	GetCurrentTime(Hr, Min, Sec, Year, Month, Day);
	
	if (AddDate)
	{
		snprintf(OBuf, MAX_LINE_SIZE + 64, "[%s:%s:%s | %s-%s-%s] %s\n", Hr, Min, Sec, Year, Month, Day, InStream);
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
{ /*This is so much better than the convoluted /proc method we used before,
	* but I was scared of using kill() for this purpose. I thought that
	* some processes would notice it and whine. Lucky me that it turns out
	* signal 0 is not real.*/
	pid_t InPID = 0;
	

	if (!InObj->Opts.HasPIDFile || !(InPID = ReadPIDFile(InObj)))
	{ /*We got a PID file requested and present? Get PID from that, otherwise 
		* get the PID from memory.*/
		InPID = InObj->ObjectPID;
	}
	
	if (InPID == 0) /*This means the object has no PID.*/
	{
		return false;
	}
	
	if (kill(InPID, 0) == 0)
	{
		return true;
	}
	else
	{
		return false;
	}
}

void MinsToDate(unsigned MinInc, unsigned *OutHr, unsigned *OutMin,
				unsigned *OutMonth, unsigned *OutDay, unsigned *OutYear)
{  /*Returns the projected date that it will be after MinInc minutes.
	* Not really a good example of a function that beints in console.c.*/
	time_t CurrentTime, LaterTime;
	struct tm OutTime;
	
	time(&CurrentTime);
	
	LaterTime = CurrentTime + 60 * MinInc;

	localtime_r(&LaterTime, &OutTime);
	
	*OutHr = OutTime.tm_hour;
	*OutMin = OutTime.tm_min;
	*OutMonth = OutTime.tm_mon + 1;
	*OutDay = OutTime.tm_mday;
	*OutYear = OutTime.tm_year + 1900;
}

unsigned *DateDiff(unsigned InHr, unsigned InMin, unsigned *OutMonth,
						unsigned *OutDay, unsigned *OutYear)
{ /*Provides a true date as to when the next occurrence of this hour and minute will return via pointers, and
	* also provides the number of minutes that will elapse during the time between. You can pass NULL for the pointers.*/
	struct tm TimeStruct, TS2;
	time_t CoreClock = 0, Clock2 = 0;
	unsigned Hr, Min, Month, Day, Year;
	static unsigned RetVal[2];
	
	time(&CoreClock);
	localtime_r(&CoreClock, &TimeStruct);
	
	Hr = TimeStruct.tm_hour;
	Min = TimeStruct.tm_min;
	Month = TimeStruct.tm_mon + 1;
	Day = TimeStruct.tm_mday;
	Year = TimeStruct.tm_year + 1900;
	
	if (InHr < Hr || (InHr == Hr && InMin < Min))
	{ /*If that time has already happened, we know that this will occur in the next day.*/
		if (Day + 1 > MDays[Month - 1])
		{
			Day = 1;
			
			if (Month + 1 > 12)
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
	
	/*Provide the main return values.*/
	if (OutDay) *OutDay = Day;
	if (OutMonth) *OutMonth = Month;
	if (OutYear) *OutYear = Year;
	
	/*Return the time difference in minutes.*/
	TS2.tm_hour = InHr;
	TS2.tm_min = InMin;
	TS2.tm_mon = Month - 1;
	TS2.tm_mday = Day;
	TS2.tm_year = Year - 1900;
	
	
	/*Convert TS2 into a time_t*/
	Clock2 = mktime(&TS2);
	
	RetVal[0] = (Clock2 - CoreClock) / 60; /*Minutes.*/
	RetVal[1] = (Clock2 - CoreClock) % 60; /*and seconds.*/
	
	/*Return the difference in minutes.*/
	return RetVal;
}

void GetCurrentTime(char *OutHr, char *OutMin, char *OutSec, char *OutYear, char *OutMonth, char *OutDay)
{ /*You can put NULL for items that you don't want the value of.*/
	struct tm TimeStruct;
	int HMS_I[3];
	int MDY_I[3];
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
	localtime_r(&TimeT, &TimeStruct);
	
	HMS_I[0] = TimeStruct.tm_hour;
	HMS_I[1] = TimeStruct.tm_min;
	HMS_I[2] = TimeStruct.tm_sec;
	
	MDY_I[0] = TimeStruct.tm_mon + 1;
	MDY_I[1] = TimeStruct.tm_mday;
	MDY_I[2] = TimeStruct.tm_year + 1900;
	
	for (; Inc < 3; ++Inc)
	{
		if (HMS[Inc] == NULL)
		{
			continue;
		}
		
		snprintf(HMS[Inc], 16, (HMS_I[Inc] >= 10 ? "%u" : "0%u"), HMS_I[Inc]);
	}
	
	for (Inc = 0; Inc < 3; ++Inc)
	{
		if (MDY[Inc] != NULL)
		{
			snprintf(MDY[Inc], 16, (MDY_I[Inc] >= 10 ? "%u" : "0%u"), MDY_I[Inc]);
		}
	}
}

unsigned AdvancedPIDFind(ObjTable *InObj, Bool UpdatePID)
{ /*Advaaaanced! Ooh, shiney!
	*Ok, seriously now, it finds PIDs by scanning /proc/somenumber/cmdline.*/
	DIR *ProcDir = NULL;
	struct dirent *DirPtr = NULL;
	char FileName[MAX_LINE_SIZE];
	char FileBuf[MAX_LINE_SIZE];
	char CmdLine[MAX_LINE_SIZE];
	unsigned Inc = 0, Streamsize = 0, Countdown = 0;
	FILE *Descriptor = NULL;
	
	/*No point if there's no /proc you know.*/
	if (!ProcAvailable()) return 0;
	
	if (InObj->ObjectStartCommand == NULL)
	{
		return 0;
	}	
	
	if (!(ProcDir = opendir("/proc/")))
	{
		return 0;
	}
	
	/*Remove forbidden characters.*/
	snprintf(CmdLine, sizeof CmdLine, "%s", InObj->ObjectStartCommand);
		
	for (Countdown = strlen(CmdLine) - 1; Countdown > 0 &&
		(CmdLine[Countdown] == ' ' || CmdLine[Countdown] == '\t' ||
		CmdLine[Countdown] == '&' || CmdLine[Countdown] == ';'); --Countdown)
	{
		CmdLine[Countdown] = '\0';
	}
	
	while ((DirPtr = readdir(ProcDir)))
	{
		if (AllNumeric(DirPtr->d_name) && atol(DirPtr->d_name) >= InObj->ObjectPID)
		{
			int TChar;
			
			snprintf(FileName, sizeof FileName, "/proc/%s/cmdline", DirPtr->d_name);
			
			if (!(Descriptor = fopen(FileName, "r")))
			{
				closedir(ProcDir);
				return 0;
			}
			
			for (Inc = 0; (TChar = getc(Descriptor)) != EOF && Inc < MAX_LINE_SIZE - 1; ++Inc)
			{
				*(unsigned char*)&FileBuf[Inc] = (unsigned char)TChar;
			}
			FileBuf[Inc] = '\0';
			
			Streamsize = Inc;
			
			fclose(Descriptor);
			
			for (Inc = 0; Inc < Streamsize; ++Inc)
			{ /*We need to replace the NUL characters with spaces.*/
				if (FileBuf[Inc] == '\0')
				{
					FileBuf[Inc] = ' ';
				}
			}
			
			if (!strncmp(FileBuf, CmdLine, strlen(CmdLine)))
			{
				unsigned RealPID;
				
				RealPID = atol(DirPtr->d_name);
				
				if (UpdatePID)
				{
					InObj->ObjectPID = RealPID;
				}
				
				closedir(ProcDir);
				return RealPID;
				
			}
		}
	}
	closedir(ProcDir);
	
	return 0;
}

unsigned ReadPIDFile(const ObjTable *InObj)
{
	FILE *PIDFileDescriptor = fopen(InObj->ObjectPIDFile, "r");
	char PIDBuf[MAX_LINE_SIZE], *TW = NULL, *TW2 = NULL;
	unsigned InPID = 0, Inc = 0;
	int TChar;
	
	if (!PIDFileDescriptor)
	{
		return 0; /*Zero for failure.*/
	}
	
	for (; (TChar = getc(PIDFileDescriptor)) != EOF && Inc < (MAX_LINE_SIZE - 1); ++Inc)
	{
		*(unsigned char*)&PIDBuf[Inc] = (unsigned char)TChar;
		/*I bet very few people actually know that this common piece of code
		 * can potentially cause a signal to be raised if we're built in C99 mode,
		 * depending on the compiler. char is guaranteed not to trap, so it's better
		 * we assign an unsigned value to it in this method than trust the compiler not
		 * to implement a really bad option in the C99 standard.*/
	}
	PIDBuf[Inc] = '\0';
	
	fclose(PIDFileDescriptor);
	
	for (TW = PIDBuf; *TW == '\n' || *TW == '\t' || *TW == ' '; ++TW); /*Skip past initial junk if any.*/
	
	for (TW2 = TW; *TW2 != '\0' && *TW2 != '\t' && *TW2 != '\n' && *TW2 != ' '; ++TW2); /*Delete any following the number.*/
	*TW2 = '\0';
	
	if (AllNumeric(TW))
	{
		InPID = atoi(TW);
	}
	else
	{
		return 0;
	}
	
	return InPID;
}

short GetStateOfTime(unsigned Hr, unsigned Min, unsigned Sec,
				unsigned Month, unsigned Day, unsigned Year)
{  /*This function is used to determine if the passed time is in the past,
	* present, or future.*/
	time_t CurrentTime, PassedTime;
	struct tm PassedTimeS;
	
	time(&CurrentTime);
	
	PassedTimeS.tm_year = Year - 1900;
	PassedTimeS.tm_mon = Month - 1;
	PassedTimeS.tm_mday = Day;
	PassedTimeS.tm_hour = Hr;
	PassedTimeS.tm_min = Min;
	PassedTimeS.tm_sec = Sec;
	PassedTimeS.tm_isdst = -1;
	
	if ((PassedTime = mktime(&PassedTimeS)) == -1)
	{ /*Not entirely sure how else I can do this.*/
		return -1;
	}
	
	if (PassedTime < CurrentTime)
	{ /*The passed time is in the past.*/
		return 2;
	}
	else if (PassedTime == CurrentTime)
	{ /*The passed time is in the present.*/
		return 1;
	}
	else
	{ /*The passed time is in the future.*/
		return 0;
	}
	
}

Bool ProcAvailable(void)
{
	struct stat FileStat;
	
	return !stat("/proc/cmdline", &FileStat);
}
