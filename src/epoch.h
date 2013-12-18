/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/


/**This is the only header file we will ever need for Epoch, in all likelihood.**/

#ifndef __EPOCH_H__
#define __EPOCH_H__

/**Defines go here.**/

/*Limits and stuff.*/
#define MAX_DESCRIPT_SIZE 384
#define MAX_LINE_SIZE 2048

/*Configuration.*/

/*EPOCH_INIT_PATH is not used for much. Mainly reexec.*/
#ifndef EPOCH_BINARY_PATH
#define EPOCH_BINARY_PATH "/sbin/epoch"
#endif

#ifndef SHELLPATH
#define SHELLPATH "/bin/sh"
#endif

#ifndef CONFIGDIR /*This is available for good purpose.*/
#define CONFIGDIR "/etc/epoch/"
#endif

#ifndef LOGDIR
#define LOGDIR "/var/log/"
#endif

#define CONF_NAME "epoch.conf"
#define LOGFILE_NAME "system.log"

/*Environment variables.*/
#ifndef ENVVAR_HOME
#define ENVVAR_HOME "/"
#endif

#ifndef ENVVAR_USER
#define ENVVAR_USER "root"
#endif

#ifndef ENVVAR_SHELL
#define ENVVAR_SHELL SHELLPATH
#endif

#ifndef ENVVAR_PATH
#define ENVVAR_PATH "/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin"
#endif

#ifndef SHELLDISSOLVES
#define SHELLDISSOLVES true
#endif

/*Version.*/
#define VERSIONSTRING "Epoch Init System (git/master)"

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
#define MEMBUS_SIZE 2048
#elif MEMBUS_SIZE < 2048
#error "MEMBUS_SIZE cannot be below 2048!" /*This is important.*/
#endif

/*The codes that are sent over the bus.*/

/*These are what we use to set message types.*/
#define MEMBUS_NOMSG 25
#define MEMBUS_MSG 100
#define MEMBUS_CHECKALIVE_NOMSG 34
#define MEMBUS_CHECKALIVE_MSG 43

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
#define MEMBUS_CODE_OBJRELOAD "OBJRELOAD"
#define MEMBUS_CODE_OBJRLS "OBJRLS" /*Generic way to detect if we are using one of the OBJRLS commands below.*/
#define MEMBUS_CODE_OBJRLS_CHECK "OBJRLS_CHECK"
#define MEMBUS_CODE_OBJRLS_ADD "OBJRLS_ADD"
#define MEMBUS_CODE_OBJRLS_DEL "OBJRLS_DEL"
#define MEMBUS_CODE_RUNLEVEL "RUNLEVEL"
#define MEMBUS_CODE_GETRL "GETRL"
#define MEMBUS_CODE_KILLOBJ "KILLOBJ"
#define MEMBUS_CODE_SENDPID "SENDPID"
#define MEMBUS_CODE_LSOBJS "LSOBJS"
#define MEMBUS_CODE_RXD "RXD"
#define MEMBUS_CODE_RXD_OPTS "ORXD"

#define MEMBUS_LSOBJS_VERSION "V1.2"
/**Types, enums, structs and whatnot**/


/**Enums go here.*/

/*Our own boolean type.*/
enum { false, true }; /*I don't want to use stdbool.*/
typedef signed char Bool;

/*How objects are stopped on shutdown.*/
typedef enum { STOP_NONE, STOP_COMMAND, STOP_PID, STOP_PIDFILE, STOP_INVALID } StopType;

/*Trinary return values for functions.*/
typedef enum { FAILURE, SUCCESS, WARNING } rStatus;

/*Trinary boot/shutdown/nothing modes.*/
typedef enum { BOOT_NEUTRAL, BOOT_BOOTUP, BOOT_SHUTDOWN } BootMode;

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
	char ObjectPrestartCommand[MAX_LINE_SIZE]; /*Run before ObjectStartCommand, if it exists.*/
	char ObjectStopCommand[MAX_LINE_SIZE]; /*How to shut it down.*/
	char ObjectReloadCommand[MAX_LINE_SIZE]; /*Used to reload an object without starting/stopping. Most services don't have this.*/
	char ObjectPIDFile[MAX_LINE_SIZE];
	unsigned long ObjectStartPriority;
	unsigned long ObjectStopPriority;
	unsigned long ObjectPID; /*The process ID, used for shutting down.*/
	unsigned char TermSignal; /*The signal we send to an object if it's stop mode is PID or PIDFILE.*/
	Bool Enabled;
	Bool Started;
	unsigned long StartedSince; /*The time in UNIX seconds since it was started.*/
	
	struct 
	{
		StopType StopMode; /*If we use a stop command, set this to 1, otherwise, set to 0 to use PID.*/
		
		/*This saves a tiny bit of memory to use bitfields here.*/
		unsigned int CanStop : 1; /*Allowed to stop this without starting a shutdown?*/
		unsigned int HaltCmdOnly : 1; /*Run just the stop command when we halt, not the start command?*/
		unsigned int IsService : 1; /*If true, we assume it's going to fork itself and one-up it's PID.*/
		unsigned int RawDescription : 1; /*This inhibits insertion of "Starting", "Stopping", etc in front of descriptions.*/
		unsigned int AutoRestart : 1;
		unsigned int EmulNoWait : 1; /*Emulates the deprecated NOWAIT option by appending an ampersand to the end of ObjectStartCommand.*/
		unsigned int ForceShell : 1; /*Forces us to start /bin/sh to run an object, even if it looks like we don't need to.*/
		unsigned int HasPIDFile : 1; /*If StopMode == STOP_PIDFILE, we also stop it just by sending a signal to the PID in the file.*/
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
	unsigned long JobID;
};

struct _CTask
{
	const char *TaskName;
	ObjTable *Node;
	unsigned long PID;
	unsigned int Set : 1;
};


/**Globals go here.**/

extern ObjTable *ObjectTable;
extern struct _BootBanner BootBanner;
extern char CurRunlevel[MAX_DESCRIPT_SIZE];
extern volatile char *MemData;
extern Bool DisableCAD;
extern char Hostname[MAX_LINE_SIZE];
extern volatile struct _HaltParams HaltParams;
extern Bool AutoMountOpts[5];
extern Bool EnableLogging;
extern Bool LogInMemory;
extern Bool BlankLogOnBoot;
extern char *MemLogBuffer;
extern struct _CTask CurrentTask;
extern volatile BootMode CurrentBootMode;
extern Bool AlignStatusReports;
extern volatile signed long MemBusKey;
extern volatile Bool BusRunning;

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
extern Bool ObjRL_CheckRunlevel(const char *InRL, const ObjTable *InObj, Bool CountInherited);
extern Bool ObjRL_DelRunlevel(const char *InRL, ObjTable *InObj);
extern Bool ObjRL_ValidRunlevel(const char *InRL);
extern void ObjRL_ShutdownRunlevels(ObjTable *InObj);
extern char *WhitespaceArg(const char *InStream);

/*parse.c*/
extern rStatus ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode, Bool PrintStatus);
extern rStatus RunAllObjects(Bool IsStartingMode);
extern rStatus SwitchRunlevels(const char *Runlevel);
extern rStatus ProcessReloadCommand(ObjTable *CurObj, Bool PrintStatus);

/*actions.c*/
extern void LaunchBootup(void);
extern void LaunchShutdown(signed long Signal);
extern void EmergencyShell(void);
extern void ReexecuteEpoch(void);
extern void RecoverFromReexec(void);

/*modes.c*/
extern rStatus SendPowerControl(const char *MembusCode);
extern rStatus EmulKillall5(unsigned long InSignal);
extern void EmulWall(const char *InStream, Bool ShowUser);
extern rStatus EmulShutdown(long ArgumentCount, const char **ArgStream);
extern rStatus ObjControl(const char *ObjectID, const char *MemBusSignal);

/*membus.c*/
extern rStatus InitMemBus(Bool ServerSide);
extern rStatus MemBus_Write(const char *InStream, Bool ServerSide);
extern Bool MemBus_Read(char *OutStream, Bool ServerSide);
extern void ParseMemBus(void);
extern rStatus ShutdownMemBus(Bool ServerSide);
extern Bool HandleMemBusPings(void);
extern unsigned long MemBus_BinWrite(const void *InStream_, unsigned long DataSize, Bool ServerSide);
extern unsigned long MemBus_BinRead(void *OutStream_, unsigned long MaxOutSize, Bool ServerSide);

/*console.c*/
extern void PrintBootBanner(void);
extern void SetBannerColor(const char *InChoice);
extern void PerformStatusReport(const char *InStream, rStatus State, Bool WriteToLog);
extern void SpitWarning(const char *INWarning);
extern void SpitError(const char *INErr);
extern void SmallError(const char *INErr);

/*utilfuncs.c*/
extern void GetCurrentTime(char *OutHr, char *OutMin, char *OutSec, char *OutYear, char *OutMonth, char *OutDay);
extern unsigned long DateDiff(unsigned long InHr, unsigned long InMin, unsigned long *OutMonth,
						unsigned long *OutDay, unsigned long *OutYear);
extern void MinsToDate(unsigned long MinInc, unsigned long *OutHr, unsigned long *OutMin,
				unsigned long *OutMonth, unsigned long *OutDay, unsigned long *OutYear);
extern short GetStateOfTime(unsigned long Hr, unsigned long Min, unsigned long Sec,
				unsigned long Month, unsigned long Day, unsigned long Year);
extern Bool AllNumeric(const char *InStream);
extern Bool ObjectProcessRunning(const ObjTable *InObj);
extern unsigned long ReadPIDFile(const ObjTable *InObj);
extern rStatus WriteLogLine(const char *InStream, Bool AddDate);
unsigned long AdvancedPIDFind(ObjTable *InObj, Bool UpdatePID);


#endif /* __EPOCH_H__ */
