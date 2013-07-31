/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/

/**Handles bootup, shutdown, poweroff and reboot, etc, and some misc stuff.**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "epoch.h"

/*Memory bus uhh, static globals.*/
int MemDescriptor;
char *MemData = NULL;

rStatus InitMemBus(void)
{
	if ((MemDescriptor = shmget((key_t)MEMKEY, MEMBUS_SIZE, IPC_CREAT | 0666)) < 0)
	{
		SpitError("Failed to allocate memory bus."); /*should probably use perror*/
		return FAILURE;
	}
	
	if ((MemData = shmat(MemDescriptor, NULL, 0)) == (void*)-1)
	{
		SpitError("Failed to attach memory bus to char *MemData.");
		MemData = NULL;
		return FAILURE;
	}
	
	memset(MemData, 0, MEMBUS_SIZE);
	
	return SUCCESS;	
}
	
rStatus ShutdownMemBus(void)
{
	if (!MemData)
	{
		return SUCCESS;
	}
	
	if (shmdt(MemData) != 0)
	{
		SpitWarning("Unable to shut down memory bus.");
		return FAILURE;
	}
	else
	{
		return SUCCESS;
	}
}

/*This does what it sounds like. It exits us to go to a shell in event of catastrophe.*/
void EmergencyShell(void)
{
	fprintf(stderr, CONSOLE_COLOR_MAGENTA "\nPreparing to start emergency shell." CONSOLE_ENDCOLOR "\n---\n");
	
	fprintf(stderr, "\nSyncing disks...\n");
	fflush(NULL);
	sync(); /*First things first, sync disks.*/
	
	fprintf(stderr, "Shutting down Epoch...\n");
	fflush(NULL);
	ShutdownConfig(); /*Release all memory.*/
	ShutdownMemBus(); /*Stop the membus.*/
	
	fprintf(stderr, "Launching the shell...\n");
	fflush(NULL);
	
	execlp("sh", "sh", NULL); /*Nuke our process image and replace with a shell. No point forking.*/
	
	/*We're supposed to be gone! Something went wrong!*/
	SpitError("Failed to start emergency shell! Sleeping forever.");
	fflush(NULL);
	
	while (1) sleep(1); /*Hang forever to prevent a kernel panic.*/
}

void LaunchBootup(void)
{ /*Handles what would happen if we were PID 1.*/
	printf("\n%s\n", VERSIONSTRING);
	
	if (!InitConfig())
	{ /*That is very very bad.*/
		EmergencyShell();
	}
	
	if (!InitMemBus())
	{
		EmergencyShell();
	}
	
	PrintBootBanner();
	
	if (!RunAllObjects(true))
	{
		EmergencyShell();
	}
}
