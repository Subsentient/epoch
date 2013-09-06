/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file LICENSE.TXT for more information.*/


/**This is the only header file we will ever need for Epoch, in all likelihood.**/

#ifndef __EPOCH_H__
#define __EPOCH_H__

/**Defines go here.**/

/*Limits and stuff.*/
#define MAX_DESCRIPT_SIZE 384
#define MAX_LINE_SIZE 8192

/*Configuration.*/
#ifndef CONFIGDIR /*This is available for good purpose.*/
#define CONFIGDIR "/etc/epoch/"
#endif

#define CONF_NAME "epoch.conf"

/*Version.*/
#define VERSIONSTRING "Epoch Init System v0.1"

/*Linux signals.*/
#define OSCTL_SIGNAL_HUP 1
#define OSCTL_SIGNAL_INT 2
#define OSCTL_SIGNAL_QUIT 3
#define OSCTL_SIGNAL_ILL 4
#define OSCTL_SIGNAL_TRAP 5
#define OSCTL_SIGNAL_ABRT 6
#define OSCTL_SIGNAL_FPE 8
#define OSCTL_SIGNAL_KILL 9
#define OSCTL_SIGNAL_USR1 10
#define OSCTL_SIGNAL_SEGV 11
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
#define MEMBUS_CODE_ABORTHALT "INIT_ABORTHALT"
#define MEMBUS_CODE_HALT "INIT_HALT"
#define MEMBUS_CODE_POWEROFF "INIT_POWEROFF"
#define MEMBUS_CODE_REBOOT "INIT_REBOOT"

#define MEMBUS_CODE_RESET "EPOCH_REINIT" /*Forces a reset of the object table.*/
#define MEMBUS_CODE_CADON "CADON"
#define MEMBUS_CODE_CADOFF "CADOFF"
/*Codes that one expects to find information after.*/
#define MEMBUS_CODE_OBJSTART "OBJSTART"
#define MEMBUS_CODE_OBJSTOP "OBJSTOP"
#define MEMBUS_CODE_OBJENABLE "OBJENABLE"
#define MEMBUS_CODE_OBJDISABLE "OBJDISABLE"
#define MEMBUS_CODE_OBJRLS "OBJRLS" /*Generic way to detect if we are using one of the OBJRLS commands below.*/
#define MEMBUS_CODE_OBJRLS_CHECK "OBJRLS_CHECK"
#define MEMBUS_CODE_OBJRLS_ADD "OBJRLS_ADD"
#define MEMBUS_CODE_OBJRLS_DEL "OBJRLS_DEL"
#define MEMBUS_CODE_STATUS "OBJSTAT"
#define MEMBUS_CODE_RUNLEVEL "RUNLEVEL"

/**Types, enums, structs and whatnot**/


/**Enums go here.*/

/*Our own boolean type.*/
enum { false, true }; /*I don't want to use stdbool.*/
typedef signed char Bool;

/*How objects are stopped on shutdown.*/
typedef enum { STOP_NONE, STOP_COMMAND, STOP_PID, STOP_PIDFILE, STOP_INVALID } StopType;

/*Trinary return values for functions.*/
typedef enum { FAILURE, SUCCESS, WARNING, NOTIFICATION } rStatus;


/**Structures go here.**/
struct _RLTree
{ /*Runlevel linked list.*/
	char RL[MAX_DESCRIPT_SIZE];
	
	struct _RLTree *Prev;
	struct _RLTree *Next;
};
	
typedef struct _EpochObjectTable
{
	char ObjectID[MAX_DESCRIPT_SIZE]; /*The ASCII ID given to this item by whoever configured Epoch.*/
	char ObjectDescription[MAX_DESCRIPT_SIZE]; /*The description of the object.*/
	char ObjectStartCommand[MAX_LINE_SIZE]; /*The command to be executed.*/
	char ObjectStopCommand[MAX_LINE_SIZE]; /*How to shut it down.*/
	char ObjectPIDFile[MAX_LINE_SIZE];
	unsigned long ObjectStartPriority;
	unsigned long ObjectStopPriority;
	unsigned long ObjectPID; /*The process ID, used for shutting down.*/
	Bool Enabled;
	Bool Started;
	
	struct 
	{
		Bool CanStop; /*Allowed to stop this without starting a shutdown?*/
		StopType StopMode; /*If we use a stop command, set this to 1, otherwise, set to 0 to use PID.*/
		Bool NoWait; /*Should we just start this thing and cut it loose, and not wait for it?*/
		Bool HaltCmdOnly; /*Run just the stop command when we halt, not the start command?*/
		Bool IsService; /*If true, we assume it's going to fork itself and one-up it's PID.*/
		Bool RawDescription; /*This inhibits insertion of "Starting", "Stopping", etc in front of descriptions.*/
	} Opts;
	
	struct _RLTree *ObjectRunlevels; /*Dynamically allocated, needless to say.*/
	
	struct _EpochObjectTable *Prev;
	struct _EpochObjectTable *Next;
} ObjTable;

struct _BootBanner
{
	Bool ShowBanner;
	char BannerText[MAX_LINE_SIZE];
	char BannerColor[64];
};

struct _HaltParams
{
	signed long HaltMode;
	unsigned long TargetHour;
	unsigned long TargetMin;
	unsigned long TargetSec;
	unsigned long TargetMonth;
	unsigned long TargetDay;
	unsigned long TargetYear;
};

/**Globals go here.**/

extern ObjTable *ObjectTable;
extern struct _BootBanner BootBanner;
extern char CurRunlevel[MAX_DESCRIPT_SIZE];
extern int MemDescriptor;
extern char *MemData;
extern Bool DisableCAD;
extern char Hostname[MAX_LINE_SIZE];
extern struct _HaltParams HaltParams;
extern Bool AutoMountOpts[5];

/**Function forward declarations.*/

/*config.c*/
extern rStatus InitConfig(void);
extern void ShutdownConfig(void);
extern rStatus ReloadConfig(void);
extern ObjTable *LookupObjectInTable(const char *ObjectID);
extern ObjTable *GetObjectByPriority(const char *ObjectRunlevel, Bool WantStartPriority, unsigned long ObjectPriority);
extern unsigned long GetHighestPriority(Bool WantStartPriority);
extern rStatus EditConfigValue(const char *ObjectID, const char *Attribute, const char *Value);
extern void ObjRL_AddRunlevel(const char *InRL, ObjTable *InObj);
extern Bool ObjRL_CheckRunlevel(const char *InRL, ObjTable *InObj);
extern Bool ObjRL_DelRunlevel(const char *InRL, ObjTable *InObj);
extern void ObjRL_ShutdownRunlevels(ObjTable *InObj);

/*parse.c*/
extern rStatus ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode);
extern rStatus RunAllObjects(Bool IsStartingMode);
extern rStatus SwitchRunlevels(const char *Runlevel);

/*actions.c*/
extern void LaunchBootup(void);
extern void LaunchShutdown(signed long Signal);
extern void EmergencyShell(void);

/*modes.c*/
extern rStatus SendPowerControl(const char *MembusCode);
extern rStatus EmulKillall5(unsigned long InSignal);
extern void EmulWall(const char *InStream, Bool ShowUser);
extern rStatus EmulShutdown(long ArgumentCount, const char **ArgStream);
extern Bool AskObjectStarted(const char *ObjectID);
extern rStatus ObjControl(const char *ObjectID, const char *MemBusSignal);

/*membus.c*/
extern rStatus InitMemBus(Bool ServerSide);
extern rStatus MemBus_Write(const char *InStream, Bool ServerSide);
extern Bool MemBus_Read(char *OutStream, Bool ServerSide);
extern void ParseMemBus(void);
extern rStatus ShutdownMemBus(Bool ServerSide);

/*console.c*/
extern void PrintBootBanner(void);
extern void SetBannerColor(const char *InChoice);
extern void PrintStatusReport(const char *InStream, rStatus State);
extern void SpitWarning(char *INWarning);
extern void SpitError(char *INErr);

/*utilfuncs.c*/
extern void GetCurrentTime(char *OutHr, char *OutMin, char *OutSec, char *OutMonth, char *OutDay, char *OutYear);
extern unsigned long DateDiff(unsigned long InHr, unsigned long InMin, unsigned long *OutMonth,
						unsigned long *OutDay, unsigned long *OutYear);
extern void MinsToDate(unsigned long MinInc, unsigned long *OutHr, unsigned long *OutMin,
				unsigned long *OutMonth, unsigned long *OutDay, unsigned long *OutYear);
extern Bool AllNumeric(const char *InStream);
extern Bool ProcessRunning(unsigned long InPID);


#endif /* __EPOCH_H__ */
