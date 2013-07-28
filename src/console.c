/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**This file is for console related stuff, like status reports and whatnot.
 * I can't see this file getting too big.**/

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include "epoch.h"


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
    
	strncpy(IP2, InStream, 256);
	
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
		default:
			SpitWarning("Bad parameter passed to PrintStatusReport() in console.c");
			return;
	}
	
	if (strlen(IP2) >= StreamLength)
	{ /*Keep it aligned if we are printing a multi-line report.*/
		strcat(OutMsg, "\n");
	}
	else
	{
		StreamLength -= strlen(IP2);
	}
	
	/*Appropriate spacing.*/
	for (; Inc < StreamLength; ++Inc)
	{
		strcat(OutMsg, " ");
	}
	
	strcat(OutMsg, StatusFormat);
	
	printf("%s", OutMsg);
	
	return;
}

