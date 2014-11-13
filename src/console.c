/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

/**This file is for console related stuff, like status reports and whatnot.
 * I can't see this file getting too big.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "epoch.h"

/*The banner we show upon startup.*/
struct _BootBanner BootBanner;
/*Should we Disable CTRL-ALT-DEL instant reboots?*/
Bool DisableCAD = true;

static const char *const ExitStrings[] = { CONSOLE_COLOR_RED "FAIL" CONSOLE_ENDCOLOR,
							CONSOLE_COLOR_GREEN "DONE" CONSOLE_ENDCOLOR, 
							CONSOLE_COLOR_YELLOW "WARN" CONSOLE_ENDCOLOR };
							
void PrintBootBanner(void)
{ /*Real simple stuff.*/
	if (!BootBanner.ShowBanner)
	{
		return;
	}
	
	if (!strncmp(BootBanner.BannerText, "FILE", strlen("FILE")))
	{ /*Now we read the file and copy it into the new array.*/
		char *Worker, *TW;
		FILE *TempDescriptor;
		int TChar;
		unsigned Inc = 0;
		
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
			*(unsigned char*)&BootBanner.BannerText[Inc] = (unsigned char)TChar;
		}
		BootBanner.BannerText[Inc] = '\0';
		
		fclose(TempDescriptor);
	}
	
	if (*BootBanner.BannerColor)
	{
		printf("%s%s%s\n\n", BootBanner.BannerColor, BootBanner.BannerText, CONSOLE_ENDCOLOR);
	}
	else
	{
		printf("%s\n\n", BootBanner.BannerText);
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

/*Creates the status report.*/
void RenderReturnCodeReport(const char *InReport)
{							
	printf("%s " CONSOLE_CTL_SAVESTATE CONSOLE_COLOR_CYAN "...." CONSOLE_ENDCOLOR, InReport);
	fflush(stdout);
}
	
void CompleteStatusReport(const char *InReport, ReturnCode ExitStatus, Bool LogReport)
{
	char OBuf[MAX_LINE_SIZE];
	
	printf(CONSOLE_CTL_RESTORESTATE "%s\n", ExitStrings[ExitStatus]);
	fflush(stdout);
	
	if (LogReport && InReport != NULL && EnableLogging)
	{	
		WriteLogLine(OBuf, true);
	}
		
}
	
/*Three little error handling functions. Yay!*/
void SpitError(const char *INErr)
{
	char HMS[3][16], MDY[3][16];
	
	GetCurrentTime(HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2]);
	
	fprintf(stderr, "[%s:%s:%s | %s-%s-%s] " CONSOLE_COLOR_RED "Epoch: ERROR:\n" CONSOLE_ENDCOLOR "%s\n\n",
			HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2], INErr);
}

void SmallError(const char *INErr)
{
	fprintf(stderr, CONSOLE_COLOR_RED "* " CONSOLE_ENDCOLOR "%s\n", INErr);
}

void SpitWarning(const char *INWarning)
{
	char HMS[3][16], MDY[3][16];
	
	GetCurrentTime(HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2]);
	
	fprintf(stderr, "[%s:%s:%s | %s-%s-%s] " CONSOLE_COLOR_YELLOW "Epoch: WARNING:\n" CONSOLE_ENDCOLOR "%s\n\n",
			HMS[0], HMS[1], HMS[2], MDY[0], MDY[1], MDY[2], INWarning);
}
