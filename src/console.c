/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file is for console related stuff, like status reports and whatnot.
 * I can't see this file getting too big.**/

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/ioctl.h>
#include "epoch.h"

/*The banner we show upon startup.*/
struct _BootBanner BootBanner = { false, { '\0' }, { '\0' } };
/*Should we Disable CTRL-ALT-DEL instant reboots?*/
Bool DisableCAD = true;
/*Days in the month, for time stuff.*/
static const short MDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

void PrintBootBanner(void)
{ /*Real simple stuff.*/
	if (!BootBanner.ShowBanner)
	{
		return;
	}
	
	if (!strncmp(BootBanner.BannerText, "FILE", strlen("FILE")))
	{ /*Now we read the file and copy it into the new array.*/
		char *Worker, *TW, TChar;
		FILE *TempDescriptor;
		unsigned long Inc = 0;
		
		BootBanner.BannerText[Inc] = '\0';	
		
		Worker = BootBanner.BannerText + strlen("FILE");
		
		for (; *Worker == ' ' || *Worker == '\t'; ++Worker);

		if ((TW = strstr(Worker, "\n")))
		{
			*TW = '\0';
		}
		
		if (!(TempDescriptor = fopen(Worker, "r")))
		{
			char TmpBuf[1024];
			
			snprintf(TmpBuf, 1024, "Failed to display boot banner, can't open file \"%s\".", Worker);
			SpitWarning(TmpBuf);
			return;
		}
		
		for (; (TChar = getc(TempDescriptor)) != EOF && Inc < MAX_LINE_SIZE - 1; ++Inc)
		{ /*It's a loop copy. Get over it.*/
			BootBanner.BannerText[Inc] = TChar;
		}
		BootBanner.BannerText[Inc] = '\0';
		
		fclose(TempDescriptor);
	}
	
	if (*BootBanner.BannerColor)
	{
		printf("\n%s%s%s\n", BootBanner.BannerColor, BootBanner.BannerText, CONSOLE_ENDCOLOR);
	}
	else
	{
		printf("\n%s\n", BootBanner.BannerText);
	}
}

void SetBannerColor(const char *InChoice)
{
	if (!strcmp(InChoice, "BLACK"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_BLACK);
	}
	else if (!strcmp(InChoice, "BLUE"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_BLUE);
	}
	else if (!strcmp(InChoice, "RED"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_RED);
	}
	else if (!strcmp(InChoice, "GREEN"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_GREEN);
	}
	else if (!strcmp(InChoice, "YELLOW"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_YELLOW);
	}
	else if (!strcmp(InChoice, "MAGENTA"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_MAGENTA);
	}
	else if (!strcmp(InChoice, "CYAN"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_CYAN);
	}
	else if (!strcmp(InChoice, "WHITE"))
	{
		snprintf(BootBanner.BannerColor, 64, "%s", CONSOLE_COLOR_WHITE);
	}
	else
	{ /*Bad value? Warn and then set no color.*/
		char TmpBuf[1024];
		
		BootBanner.BannerColor[0] = '\0';
		snprintf(TmpBuf, 1024, "Bad color value \"%s\" specified for boot banner. Setting no color.", InChoice);
		SpitWarning(TmpBuf);
	}
}

/*Give this function the string you just printed, and it'll print a status report at the end of it, aligned to right.*/
void PrintStatusReport(const char *InStream, rStatus State)
{
	unsigned long StreamLength, Inc = 0;
	char OutMsg[2048] = { '\0' }, IP2[256];
	char StatusFormat[1024];
	struct winsize WSize;
	
	/*Get terminal width so we can adjust the status report.*/
    ioctl(0, TIOCGWINSZ, &WSize);
    StreamLength = WSize.ws_col;
    
	if (StreamLength >= sizeof OutMsg/2)
	{ /*Default to 80 if we get a very big number.*/
		StreamLength = 80;
	}

	snprintf(IP2, 256, "%s", InStream);
	
	switch (State)
	{
		case FAILURE:
		{
			snprintf(StatusFormat, 1024, "[%s]\n", CONSOLE_COLOR_RED "FAILED" CONSOLE_ENDCOLOR);
			break;
		}
		case SUCCESS:
		{
			snprintf(StatusFormat, 1024, "[%s]\n", CONSOLE_COLOR_GREEN "Done" CONSOLE_ENDCOLOR);
			break;
		}
		case WARNING:
		{
			snprintf(StatusFormat, 1024, "[%s]\n", CONSOLE_COLOR_YELLOW "WARNING" CONSOLE_ENDCOLOR);
			break;
		}
		case NOTIFICATION:
		{ /*Used for objects where Opts.NoWait == true.*/
			snprintf(StatusFormat, 1024, "[%s]\n", CONSOLE_COLOR_CYAN "Launched" CONSOLE_ENDCOLOR);
			break;
		}
		default:
		{
			SpitWarning("Bad parameter passed to PrintStatusReport() in console.c.");
			return;
		}
	}
	
	switch (State)
	{ /*Take our status reporting into account, but not with the color characters and newlines and stuff, 
		because that gives misleading results due to the extra characters that you can't see.*/
		case SUCCESS:
			StreamLength -= strlen("[Done]");
			break;
		case FAILURE:
			StreamLength -= strlen("[FAILED]");
			break;
		case WARNING:
			StreamLength -= strlen("[WARNING]");
			break;
		case NOTIFICATION:
			StreamLength -= strlen("[Launched]");
			break;
		default:
			SpitWarning("Bad parameter passed to PrintStatusReport() in console.c");
			return;
	}
	
	if (strlen(IP2) >= StreamLength)
	{ /*Keep it aligned if we are printing a multi-line report.*/
		strncat(OutMsg, "\n", 2);
	}
	else
	{
		StreamLength -= strlen(IP2);
	}
	
	/*Appropriate spacing.*/
	for (; Inc < StreamLength; ++Inc)
	{
		strncat(OutMsg, " ", 2);
	}
	
	strncat(OutMsg, StatusFormat, strlen(StatusFormat) + 1);
	
	printf("%s", OutMsg);
	
	return;
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

/*Two little error handling functions. Yay!*/
void SpitError(char *INErr)
{
	char HMS[3][16], MDY[3][16];
	
	GetCurrentTime(HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2]);
	
	fprintf(stderr, "[%s:%s:%s | %s/%s/%s] " CONSOLE_COLOR_RED "Epoch: ERROR:\n" CONSOLE_ENDCOLOR "%s\n\n",
			HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2], INErr);
}

void SpitWarning(char *INWarning)
{
	char HMS[3][16], MDY[3][16];
	
	GetCurrentTime(HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2]);
	
	fprintf(stderr, "[%s:%s:%s | %s/%s/%s] " CONSOLE_COLOR_YELLOW "Epoch: WARNING:\n" CONSOLE_ENDCOLOR "%s\n\n",
			HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2], INWarning);
}
