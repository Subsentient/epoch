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
#define MAX_CONFIG_FILES 400

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

/*Power control magic.*/
#define OSCTL_REBOOT 0x1234567
#define OSCTL_HALT 0xcdef0123
#define OSCTL_POWEROFF 0x4321fedc
#define OSCTL_DISABLE_CTRLALTDEL 0 /*Now isn't this hilarious. It's zero.*/
#define OSCTL_ENABLE_CTRLALTDEL 0x89abcdef

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

/*Stuff used for status reports etc*/
#define CONSOLE_CTL_SAVESTATE "\033[s"
#define CONSOLE_CTL_RESTORESTATE "\033[u"

/*The key for the shared memory bus and related stuff.*/
#define MEMKEY (('E' + 'P' + 'O' + 'C' + 'H') + ('W'+'h'+'i'+'t'+'e' + 'R'+'a'+'t')) * 7 /*Cool, right?*/

#define MEMBUS_SIZE 4096 + sizeof(long) * 2
#define MEMBUS_MSGSIZE 2047

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

#define MEMBUS_LSOBJS_VERSION "V4"
/**Types, enums, structs and whatnot**/


/**Enums go here.*/

/*Our own boolean type.*/
enum { false, true }; /*I don't want to use stdbool.*/
typedef signed char Bool;

/*How objects are stopped on shutdown.*/
enum _StopMode { STOP_NONE, STOP_COMMAND, STOP_PID, STOP_PIDFILE, STOP_INVALID };

enum { COPT_HALTONLY = 1, COPT_PERSISTENT, COPT_FORK, COPT_SERVICE, COPT_AUTORESTART,
		COPT_FORCESHELL, COPT_NOSTOPWAIT, COPT_STOPTIMEOUT, COPT_TERMSIGNAL,
		COPT_RAWDESCRIPTION, COPT_PIVOTROOT, COPT_EXEC, COPT_RUNONCE, COPT_FORKSCANONCE,
		COPT_NOTRACK, COPT_STARTFAILCRITICAL, COPT_STOPFAILCRITICAL, COPT_MAX };
		
/*Trinary return values for functions.*/
typedef enum { FAILURE, SUCCESS, WARNING } ReturnCode;

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
	unsigned ObjectStartPriority;
	unsigned ObjectStopPriority;
	unsigned ObjectPID; /*The process ID, used for shutting down.*/
	unsigned UserID; /*The user ID we run this as. Zero, of course, is root and we need do nothing.*/
	unsigned GroupID; /*Same as above, but with groups.*/
	unsigned StartedSince; /*The time in UNIX seconds since it was started.*/
	char *ObjectID; /*The ASCII ID given to this item by whoever configured Epoch.*/
	char *ObjectDescription; /*The description of the object.*/
	char *ObjectStartCommand; /*The command to be executed.*/
	char *ObjectPrestartCommand; /*Run before ObjectStartCommand, if it exists.*/
	char *ObjectStopCommand; /*How to shut it down.*/
	char *ObjectReloadCommand; /*Used to reload an object without starting/stopping. Most services don't have this.*/
	char *ObjectPIDFile; /*PID file location.*/
	char *ObjectWorkingDirectory; /*The working directory the object chdirs to before execution.*/
	char *ObjectStderr; /*A file that stderr redirects to.*/
	char *ObjectStdout; /*A file that stdout redirects to.*/
	
	const char *ConfigFile; /*The config file this object was declared in.
	* Points either to the correct element in ConfigFileList or it points to the single-file ConfigFile array.
	* You can safely cast the above pointer to remove const.*/
	
	unsigned char TermSignal; /*The signal we send to an object if it's stop mode is PID or PIDFILE.*/
	unsigned char ReloadCommandSignal; /*If the reload command sends a signal, this works.*/
	Bool Enabled;
	Bool Started;
	
	struct
	{ /*Maps an object's exit statuses to a special case of an ReturnCode value.*/
		unsigned char ExitStatus; /*The exit status of the program.*/
		unsigned char Value; /*An ReturnCode trinary value, or 4 for unused.*/
	} ExitStatuses[8];
	
	struct 
	{
		enum _StopMode StopMode; /*If we use a stop command, set this to 1, otherwise, set to 0 to use PID.*/
		unsigned StopTimeout; /*The number of seconds we wait for a task we're stopping's PID to become unavailable.*/
		
		/*This saves a tiny bit of memory to use bitfields here.*/
		unsigned Persistent : 1; /*Allowed to stop this without starting a shutdown?*/
		unsigned HaltCmdOnly : 1; /*Run just the stop command when we halt, not the start command?*/
		unsigned IsService : 1; /*If true, we assume it's going to fork itself and one-up it's PID.*/
		unsigned RawDescription : 1; /*This inhibits insertion of "Starting", "Stopping", etc in front of descriptions.*/
		unsigned AutoRestart : 1; /*Autorestarts a service whenever it terminates.*/
		unsigned ForceShell : 1; /*Forces us to start /bin/sh to run an object, even if it looks like we don't need to.*/
		unsigned HasPIDFile : 1; /*If StopMode == STOP_PIDFILE, we also stop it just by sending a signal to the PID in the file.*/
		unsigned NoStopWait : 1; /*Used to tell us not to wait for an object to actually quit.*/
		unsigned PivotRoot : 1; /*Says that ObjectStartCommand is actually used to pivot_root. See actions.c.*/
		unsigned Exec : 1; /*Says that we are gerbils.*/
		unsigned RunOnce : 1; /*Tells us to disable ourselves upon completion whenever we are started.*/
		unsigned StartFailIsCritical : 1; /*Starting this object is so important we're going to drop you to a shell if it fails.*/
		unsigned StopFailIsCritical : 1; /*Same but for stopping.*/
		unsigned NoTrack : 1; /*Don't track the PID with AdvancedPIDFind().*/
#ifndef NOMMU
		unsigned Fork : 1; /*Essentially do the same thing (with an Epoch twist) as Command& in sh.*/
		unsigned ForkScanOnce : 1; /*Same as Fork, but only scans through the PID once.*/
#endif
	} Opts;
	
	struct _EnvVarList *EnvVars; /*List of environment variables.*/
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
	signed HaltMode;
	unsigned TargetHour;
	unsigned TargetMin;
	unsigned TargetSec;
	unsigned TargetMonth;
	unsigned TargetDay;
	unsigned TargetYear;
	unsigned JobID;
};

struct _CTask
{
	const char *TaskName;
	ObjTable *Node;
	unsigned PID;
	unsigned Set : 1;
};

struct _MemBusInterface
{
	void *Root;
	unsigned long *LockPID;
	unsigned long *LockTime;
	
	struct
	{
		unsigned char *Status;
		char *Message;
		unsigned char *BinMessage;
	} Server, Client;
};

struct _EnvVarList
{
	char EnvVar[MAX_LINE_SIZE];

	struct _EnvVarList *Next;
	struct _EnvVarList *Prev;
};
	
/**Globals go here.**/

extern ObjTable *ObjectTable;
extern struct _BootBanner BootBanner;
extern char CurRunlevel[MAX_DESCRIPT_SIZE];
extern struct _MemBusInterface MemBus;
extern Bool DisableCAD;
extern char Hostname[256];
extern char Domainname[256];
extern struct _HaltParams HaltParams;
extern Bool AutoMountOpts[5];
extern Bool EnableLogging;
extern Bool LogInMemory;
extern Bool BlankLogOnBoot;
extern char *MemLogBuffer;
extern struct _CTask CurrentTask;
extern BootMode CurrentBootMode;
extern int MemBusKey;
extern Bool BusRunning;
extern char ConfigFile[MAX_LINE_SIZE];
extern char *ConfigFileList[MAX_CONFIG_FILES];
extern int NumConfigFiles;
extern struct _EnvVarList *GlobalEnvVars;
extern Bool AreInit;

/**Function forward declarations.*/

/*config.c*/
extern ReturnCode InitConfig(const char *CurConfigFile);
extern void ShutdownConfig(void);
extern ReturnCode ReloadConfig(void);
extern ObjTable *LookupObjectInTable(const char *ObjectID);
extern ObjTable *GetObjectByPriority(const char *ObjectRunlevel, ObjTable *LastNode,
									Bool WantStartPriority, unsigned ObjectPriority);
extern unsigned GetHighestPriority(Bool WantStartPriority);
extern ReturnCode EditConfigValue(const char *File, const char *ObjectID, const char *Attribute, const char *Value);
extern void ObjRL_AddRunlevel(const char *InRL, ObjTable *InObj);
extern Bool ObjRL_CheckRunlevel(const char *InRL, const ObjTable *InObj, Bool CountInherited);
extern Bool ObjRL_DelRunlevel(const char *InRL, ObjTable *InObj);
extern Bool ObjRL_ValidRunlevel(const char *InRL);
extern void ObjRL_ShutdownRunlevels(ObjTable *InObj);
extern char *WhitespaceArg(const char *InStream);
extern void EnvVarList_Shutdown(struct _EnvVarList **const List);
extern void EnvVarList_Add(const char *Var, struct _EnvVarList **const List);
extern Bool EnvVarList_Del(const char *const Check, struct _EnvVarList **const List);
extern void EnvVarList_Shutdown(struct _EnvVarList **const List);

/*parse.c*/
extern ReturnCode ProcessConfigObject(ObjTable *CurObj, Bool IsStartingMode, Bool PrintStatus);
extern ReturnCode RunAllObjects(Bool IsStartingMode);
extern ReturnCode SwitchRunlevels(const char *Runlevel);
extern ReturnCode ProcessReloadCommand(ObjTable *CurObj, Bool PrintStatus);

/*actions.c*/
extern void LaunchBootup(void);
extern void LaunchShutdown(int Signal);
extern void EmergencyShell(void);
extern void ReexecuteEpoch(void);
extern void RecoverFromReexec(Bool ViaMemBus);
extern void FinaliseLogStartup(Bool BlankLog);
extern void PerformExec(const char *Cmd_);
extern void PerformPivotRoot(const char *NewRoot, const char *OldRootDir);

/*modes.c*/
extern ReturnCode SendPowerControl(const char *MembusCode);
extern ReturnCode EmulKillall5(unsigned InSignal);
extern void EmulWall(const char *InStream, Bool ShowUser);
extern ReturnCode EmulShutdown(int ArgumentCount, const char **ArgStream);
extern ReturnCode ObjControl(const char *ObjectID, const char *MemBusSignal);

/*membus.c*/
extern ReturnCode InitMemBus(Bool ServerSide);
extern ReturnCode MemBus_Write(const char *InStream, Bool ServerSide);
extern Bool MemBus_Read(char *OutStream, Bool ServerSide);
extern void ParseMemBus(void);
extern ReturnCode ShutdownMemBus(Bool ServerSide);
extern Bool HandleMemBusPings(void);
extern Bool CheckMemBusIntegrity(void);
extern unsigned MemBus_BinWrite(const void *InStream_, unsigned DataSize, Bool ServerSide);
extern unsigned MemBus_BinRead(void *OutStream_, unsigned MaxOutSize, Bool ServerSide);

/*console.c*/
extern void PrintBootBanner(void);
extern void SetBannerColor(const char *InChoice);
extern void RendeReturnCodeReport(const char *InReport);
extern void CompleteStatusReport(const char *InReport, ReturnCode ExitStatus, Bool LogReport);
extern void SpitWarning(const char *INWarning);
extern void SpitError(const char *INErr);
extern void SmallError(const char *INErr);

/*utilfuncs.c*/
extern void GetCurrentTime(char *OutHr, char *OutMin, char *OutSec, char *OutYear, char *OutMonth, char *OutDay);
extern unsigned DateDiff(unsigned InHr, unsigned InMin, unsigned *OutMonth,
						unsigned *OutDay, unsigned *OutYear);
extern void MinsToDate(unsigned MinInc, unsigned *OutHr, unsigned *OutMin,
				unsigned *OutMonth, unsigned *OutDay, unsigned *OutYear);
extern short GetStateOfTime(unsigned Hr, unsigned Min, unsigned Sec,
				unsigned Month, unsigned Day, unsigned Year);
extern Bool AllNumeric(const char *InStream);
extern Bool ObjectProcessRunning(const ObjTable *InObj);
extern unsigned ReadPIDFile(const ObjTable *InObj);
extern ReturnCode WriteLogLine(const char *InStream, Bool AddDate);
extern unsigned AdvancedPIDFind(ObjTable *InObj, Bool UpdatePID);
extern Bool ProcAvailable(void);

#endif /* __EPOCH_H__ */
