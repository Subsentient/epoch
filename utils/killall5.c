/*This code is part of Mauri. Mauri is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**Version 0.1**/

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "../mauri.h"

int main(int argc, char **argv)
{
	unsigned char InSignal;
	char *InArg;
	
	if (argc > 2) /*Hmm, looks like we are being passed more than one argument.*/
	{
		fprintf(stderr, "Mauri killall5: Please specify a signal number such as -9,\n"
						"or nothing to default to -15.\n");
		return 1;
	}
	else if (argc == 1) /*They are just running us with no arguments. Assume signal 15.*/
	{
		InSignal = 15;
	}
	else
	{ /*Parse the argument.*/
		if (!strncmp("-", argv[1], 1))
		{ /*If they used a dash for the argument..*/
			InArg = argv[1] + 1;
		}
		else
		{
			InArg = argv[1];
		}
		
		InSignal = atoi(InArg);
		
		if (InSignal > OSCTL_SIGNAL_STOP || InSignal <= 0)
		{
			fprintf(stderr, "Mauri killall5: %s",
				"Bad argument provided. Please enter a valid signal number.\n");
		}
	}
	
	return kill(-1, InSignal); /*Well, since kill() returns non-zero on failure, I see no problem here.*/
}
