/*This code is part of Epoch. Epoch is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/


/**This is the only header file we will ever need for Epoch, in all likelihood.**/

#ifndef __EPOCH_H__
#define __EPOCH_H__

/**Defines go here.**/

/*Limits and stuff.*/
#define MAX_DESCRIPT_SIZE 8192
#define MAX_LINE_SIZE 8192
#define MIN_CONFIG_SIZE 16384

/*Configuration.*/
#ifndef CONFIGDIR /*This is available for good purpose.*/
#define CONFIGDIR "/etc"
#endif

#define CONF_NAME "epoch.conf"

/*Version.*/
#define VERSIONSTRING "Epoch Boot System v0.1"

/*Linux signals. Not everything that calls OSsendSignal() will have signal.h included.*/
#define OSCTL_SIGNAL_HUP 1
#define OSCTL_SIGNAL_INT 2
#define OSCTL_SIGNAL_QUIT 3
#define OSCTL_SIGNAL_ABRT 6
#define OSCTL_SIGNAL_KILL 9
#define OSCTL_SIGNAL_USR1 10
#define OSCTL_SIGNAL_USR2 12
#define OSCTL_SIGNAL_TERM 15
#define OSCTL_SIGNAL_CONT 18
#define OSCTL_SIGNAL_STOP 19

/*Power control magic. FIXME: Add support for BSD etc. here!*/
#define OSCTL_LINUX_REBOOT 0x1234567
#define OSCTL_LINUX_HALT 0xcdef0123
#define OSCTL_LINUX_POWEROFF 0x4321fedc
#define OSCTL_LINUX_DISABLE_CTRLALTDEL 0 /*Now isn't this hilarious. It's zero.*/
#define OSCTL_LINUX_ENABLE_CTRLALTDEL 0x89abcdef

/*Colors for text output.*/
#define CONSOLE_COLOR_BLACK "\033[30m"
#define CONSOLE_COLOR_RED "\033[31m"
#define CONSOLE_COLOR_GREEN "\033[32m"
#define CONSOLE_COLOR_YELLOW "\033[33m"
#define CONSOLE_COLOR_BLUE "\033[34m"
#define CONSOLE_COLOR_MAGENTA "\033[35m"
#define CONSOLE_COLOR_CYAN "\033[36m"
#define CONSOLE_COLOR_WHITE "\033[37m"
#define CONSOLE_ENDCOLOR "\033[0m"

/**Types, enums, structs and whatnot**/


/**Enums go here.*/

/*Our own boolean type.*/
enum { false, true }; /*I don't want to use stdbool.*/
typedef signed char Bool; /*Might want to check for other values however.*/

/*How objects are stopped on shutdown.*/
typedef enum { STOP_NONE, STOP_COMMAND, STOP_PID } StopType;

/*Trinary return values for functions.*/
typedef enum { FAILURE, SUCCESS, WARNING } rStatus;

/**Structures go here.**/
typedef struct _EpochObjectTable
{
	char ObjectID[MAX_DESCRIPT_SIZE]; /*The ASCII ID given to this item by whoever configured Epoch.*/
	char ObjectName[MAX_DESCRIPT_SIZE]; /*The description of the object.*/
	char ObjectStartCommand[MAX_DESCRIPT_SIZE]; /*The command to be executed.*/
	char ObjectStopCommand[MAX_DESCRIPT_SIZE]; /*How to shut it down.*/
	unsigned long ObjectStartPriority;
	unsigned long ObjectStopPriority;
	StopType StopMode; /*If we use a stop command, set this to 1, otherwise, set to 0 to use PID.*/
	unsigned long ObjectPID; /*The process ID, used for shutting down.*/
	char ObjectRunlevel[MAX_DESCRIPT_SIZE];
	
	struct _EpochObjectTable *Next;
} ObjTable;


/**Function forward declarations.*/

extern rStatus InitConfig(void);
extern void ShutdownConfig(void);
extern ObjTable *LookupObjectInTable(const char *ObjectID);
extern ObjTable *GetObjectByPriority(const char *ObjectRunlevel, Bool WantStartPriority, unsigned long ObjectPriority);
extern void PrintStatusReport(const char *InStream, rStatus State);

/**Tiny utility functions here.**/

void SpitError(char *INErr)
{
	fprintf(stderr, CONSOLE_COLOR_RED "Epoch: %s\n" CONSOLE_ENDCOLOR, INErr);
}

void SpitWarning(char *INErr)
{
	fprintf(stderr, CONSOLE_COLOR_YELLOW "Epoch: %s\n" CONSOLE_ENDCOLOR, INErr);
}

#endif /* __EPOCH_H__ */

