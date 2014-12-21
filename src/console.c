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

/*Specifies how we show our status reports to the world.*/
struct _StatusReportFormat StatusReportFormat =
		{
			CONSOLE_COLOR_CYAN "* " CONSOLE_ENDCOLOR "!TITLE! " CONSOLE_CTL_SAVESTATE "(" CONSOLE_COLOR_CYAN "...." CONSOLE_ENDCOLOR ")",
			CONSOLE_CTL_RESTORESTATE "(!STATUS!)\n",
			{
				CONSOLE_COLOR_RED "FAIL" CONSOLE_ENDCOLOR,
				CONSOLE_COLOR_GREEN "Done" CONSOLE_ENDCOLOR, 
				CONSOLE_COLOR_YELLOW "WARN" CONSOLE_ENDCOLOR 
			}
		};
/*Should we Disable CTRL-ALT-DEL instant reboots?*/
Bool DisableCAD = true;
							
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

		if ((TW = strchr(Worker, '\n')))
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
	const char *Color = NULL;
	
	if (!strcmp(InChoice, "BLACK"))
	{
		Color = CONSOLE_COLOR_BLACK;
	}
	else if (!strcmp(InChoice, "BLUE"))
	{
		Color = CONSOLE_COLOR_BLUE;
	}
	else if (!strcmp(InChoice, "RED"))
	{
		Color = CONSOLE_COLOR_RED;
	}
	else if (!strcmp(InChoice, "GREEN"))
	{
		Color = CONSOLE_COLOR_GREEN;
	}
	else if (!strcmp(InChoice, "YELLOW"))
	{
		Color = CONSOLE_COLOR_YELLOW;
	}
	else if (!strcmp(InChoice, "MAGENTA"))
	{
		Color = CONSOLE_COLOR_MAGENTA;
	}
	else if (!strcmp(InChoice, "CYAN"))
	{
		Color = CONSOLE_COLOR_CYAN;
	}
	else if (!strcmp(InChoice, "WHITE"))
	{
		Color = CONSOLE_COLOR_WHITE;
	}
	else
	{ /*Bad value? Warn and then set no color.*/
		char TmpBuf[1024];
		
		BootBanner.BannerColor[0] = '\0';
		snprintf(TmpBuf, sizeof TmpBuf, "Bad color value \"%s\" specified for boot banner. Setting no color.", InChoice);
		SpitWarning(TmpBuf);
		return;
	}
	
	strncpy(BootBanner.BannerColor, Color, sizeof BootBanner.BannerColor - 1);
	BootBanner.BannerColor[sizeof BootBanner.BannerColor - 1] = '\0';
}

/*Creates the status report.*/
void BeginStatusReport(const char *InReport)
{
	struct _StatusReportFormat TempStats = StatusReportFormat;
	char *TitleBegin = strstr(TempStats.StartFormat, "!TITLE!");
	char *TitleEnd = TitleBegin ? TitleBegin + sizeof "!TITLE!" - 1 : NULL;
	char Halves[2][sizeof TempStats.StartFormat] = { { '\0' } };
	
	if (TitleBegin) *TitleBegin = '\0'; /*So we can just copy it in with no fiddling..*/

	strcpy(Halves[0], TempStats.StartFormat); /*Copy it in whether or not we found the beginning.*/
	
	if (TitleEnd) strcpy(Halves[1], TitleEnd);
	
	printf("%s%s%s", Halves[0], InReport, Halves[1]);
	
}

void CompleteStatusReport(const char *InReport, ReturnCode ExitStatus, Bool LogReport)
{
	struct _StatusReportFormat TempStats = StatusReportFormat;
	char *StatusBegin = strstr(TempStats.FinishFormat, "!STATUS!");
	char *StatusEnd = StatusBegin ? StatusBegin + sizeof "!STATUS!" - 1 : NULL;
	char Halves[2][sizeof TempStats.FinishFormat] = { { '\0' } };
	char OBuf[MAX_LINE_SIZE];
	
	if (StatusBegin) *StatusBegin = '\0';
	
	strcpy(Halves[0], TempStats.FinishFormat);
	
	if (StatusEnd) strcpy(Halves[1], StatusEnd);
	
	
	printf("%s%s%s", Halves[0], StatusReportFormat.StatusFormats[ExitStatus], Halves[1]);

	/*Write status result to the logs.*/
	snprintf(OBuf, sizeof OBuf, "%s (%s)", InReport, StatusReportFormat.StatusFormats[ExitStatus]);
	
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
