/*This code is part of the Epoch Boot System.
* The Epoch Boot System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/


/**This is the only header file we will ever need for Epoch, in all likelihood.**/

#ifndef __EPOCH_H__
#define __EPOCH_H__

/**Defines go here.**/

/*Limits and stuff.*/
#define MAX_DESCRIPT_SIZE 512
#define MAX_LINE_SIZE 8192
#define MIN_CONFIG_SIZE 16384

/*Configuration.*/
#ifndef CONFIGDIR /*This is available for good purpose.*/
#define CONFIGDIR "/etc"
#endif

#define CONF_NAME "epoch.conf"

/*Version.*/
#define VERSIONSTRING "Epoch Boot System v0.1"

/*Linux signals.*/
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

/*The key for the shared memory bus and related stuff.*/
#define MEMKEY (('E' + 'P' + 'O' + 'C' + 'H') + ('W'+'h'+'i'+'t'+'e' + 'R'+'a'+'t')) * 7 /*Cool, right?*/

#ifndef MEMBUS_SIZE
#define MEMBUS_SIZE 1024
#endif
/*The codes that are sent over the bus.*/

/*These are what we use to set message types.*/
#define MEMBUS_NOMSG 25
#define MEMBUS_MSG 100
#define MEMBUS_NEWCONNECTION 50

/*These are status for operations.*/
#define MEMBUS_CODE_ACKNOWLEDGED "OK"
#define MEMBUS_CODE_WARNING "WARN"
#define MEMBUS_CODE_FAILURE "FAIL"
#define MEMBUS_CODE_BADPARAM "BADPARAM"
/*These are what we actually send.*/
#define MEMBUS_CODE_HALT "INIT_HALT"
#define MEMBUS_CODE_POWEROFF "INIT_POWEROFF"
#define MEMBUS_CODE_REBOOT "INIT_REBOOT"
#define MEMBUS_CODE_RESET "EPOCH_REINIT" /*Forces a reset of the object table.*/
#define MEMBUS_CODE_CADON "CADON"
#define MEMBUS_CODE_CADOFF "CADOFF"
/*Codes that one expects to find information after.*/
#define MEMBUS_CODE_OBJSTART "OBJSTART"
#define MEMBUS_CODE_OBJSTOP "OBJSTOP"
#define MEMBUS_CODE_STATUS "OBJSTAT"

/**Types, enums, structs and whatnot**/


/**Enums go here.*/

/*Our own boolean type.*/
enum { false, true }; /*I don't want to use stdbool.*/
typedef signed char Bool;

/*How objects are stopped on shutdown.*/
typedef enum { STOP_NONE, STOP_COMMAND, STOP_PID, STOP_PIDFILE, STOP_INVALID } StopType;

/*Trinary return values for functions.*/
typedef enum { FAILURE, SUCCESS, WARNING } rStatus;

/**Structures go here.**/
typedef struct _EpochObjectTable
{
	char ObjectID[MAX_DESCRIPT_SIZE]; /*The ASCII ID given to this item by whoever configured Epoch.*/
	char ObjectName[MAX_DESCRIPT_SIZE]; /*The description of the object.*/
	char ObjectStartCommand[MAX_LINE_SIZE]; /*The command to be executed.*/
	char ObjectStopCommand[MAX_LINE_SIZE]; /*How to shut it down.*/
	char ObjectPIDFile[MAX_LINE_SIZE];
	unsigned long ObjectStartPriority;
	unsigned long ObjectStopPriority;
	Bool Started;
	StopType StopMode; /*If we use a stop command, set this to 1, otherwise, set to 0 to use PID.*/
	unsigned long ObjectPID; /*The process ID, used for shutting down.*/
	char ObjectRunlevel[MAX_LINE_SIZE];
	Bool Enabled;
	
	struct _EpochObjectTable *Prev;
	struct _EpochObjectTable *Next;
} ObjTable;

struct _BootBanner
{
	Bool ShowBanner;
	char BannerText[512];
	char BannerColor[64];
};

/**Globals go here.**/

extern struct _BootBanner BootBanner;
extern char CurRunlevel[MAX_LINE_SIZE];
extern int MemDescriptor;
extern char *MemData;
extern Bool DisableCAD;

/**Function forward declarations.*/

/*config.c*/
extern rStatus InitConfig(void);
extern void ShutdownConfig(void);
extern ObjTable *LookupObjectInTable(const char *ObjectID);
extern ObjTable *GetObjectByPriority(const char *ObjectRunlevel, Bool WantStartPriority, unsigned long ObjectPriority);
extern unsigned long GetHighestPriority(Bool WantStartPriority);

/*parse.c*/
extern rStatus ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode);
extern rStatus RunAllObjects(Bool IsStartingMode);

/*actions.c*/
extern void LaunchBootup(void);
extern void LaunchShutdown(unsigned long Signal);
extern void EmergencyShell(void);

/*modes.c*/
extern rStatus TellInitToDo(const char *MembusCode);
extern rStatus EmulKillall5(unsigned long InSignal);

/*membus.c*/
extern rStatus InitMemBus(Bool ServerSide);
extern rStatus MemBus_Write(const char *InStream, Bool ServerSide);
extern Bool MemBus_Read(char *OutStream, Bool ServerSide);
extern void EpochMemBusLoop(void);
extern rStatus ShutdownMemBus(Bool ServerSide);

/*console.c*/
extern void PrintBootBanner(void);
extern void SetBannerColor(const char *InChoice);
extern void PrintStatusReport(const char *InStream, rStatus State);
extern void SpitWarning(char *INWarning);
extern void SpitError(char *INErr);


#endif /* __EPOCH_H__ */
