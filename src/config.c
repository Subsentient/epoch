/*This code is part of the Epoch Init System.
* The Epoch Init System is maintained by Subsentient.
* This software is public domain.
* Please read the file UNLICENSE.TXT for more information.*/

/**This file handles the parsing of our configuration file.
 * It adds everything into the object table.**/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <grp.h>
#include <pwd.h>
#include <ctype.h>
#include "epoch.h"

#define CONFIGWARNTXT "CONFIG: " CONSOLE_COLOR_YELLOW "WARNING: " CONSOLE_ENDCOLOR
#define CONFIGERRORTXT  "CONFIG: "CONSOLE_COLOR_RED "ERROR: " CONSOLE_ENDCOLOR

/*We want the only interface for this to be LookupObjectInTable().*/
ObjTable *ObjectTable;
char ConfigFile[MAX_LINE_SIZE] = CONFIGDIR CONF_NAME;
char *ConfigFileList[MAX_CONFIG_FILES] = { ConfigFile };
int NumConfigFiles = 1;

/*Used to allow for things like 'ObjectStartPriority Services', where Services == 3, for example.*/
static struct _PriorityAliasTree
{ /*Start/Stop priority alias support for grouping.*/
	char Alias[MAX_DESCRIPT_SIZE];
	unsigned Target;
	
	struct _PriorityAliasTree *Next;
	struct _PriorityAliasTree *Prev;
} *PriorityAliasTree;

/*Used to allow runlevels to be inherited by other runlevels.*/
static struct _RunlevelInheritance
{ /*I __REVILE__ this solution.
Epoch is just a linked list of linked lists anymore.*/
	char Inheriter[MAX_DESCRIPT_SIZE];
	char Inherited[MAX_DESCRIPT_SIZE];
	
	struct _RunlevelInheritance *Next;
	struct _RunlevelInheritance *Prev;
} *RunlevelInheritance;

/*Holds the system hostname.*/
char Hostname[256];
/*Holds the system domain name.*/
char Domainname[256];

/*Function forward declarations for all the statics.*/
static ObjTable *AddObjectToTable(const char *ObjectID, const char *File);
static char *NextLine(const char *InStream);
static ReturnCode GetLineDelim(const char *InStream, char *OutStream);
static ReturnCode ScanConfigIntegrity(void);
static void ConfigProblem(const char *File, short Type, const char *Attribute, const char *AttribVal, unsigned LineNum);
static unsigned PriorityAlias_Lookup(const char *Alias);
static void PriorityAlias_Add(const char *Alias, unsigned Target);
static void PriorityAlias_Shutdown(void);
static void RLInheritance_Add(const char *Inheriter, const char *Inherited);
static Bool RLInheritance_Check(const char *Inheriter, const char *Inherited);
static void RLInheritance_Shutdown(void);
static unsigned PriorityOfLookup(const char *const ObjectID, Bool IsStartingMode);

/*Used for error handling in InitConfig() by ConfigProblem(CurConfigFile, ).*/
enum { CONFIG_EMISSINGVAL = 1, CONFIG_EBADVAL, CONFIG_ETRUNCATED, CONFIG_EAFTER,
	CONFIG_EBEFORE, CONFIG_ELARGENUM };

/*Actual functions.*/
static char *NextLine(const char *InStream)
{
	if (!(InStream = strchr(InStream, '\n')))
	{
		return NULL;
	}

	if (*(InStream + 1) == '\0')
	{ /*End of file.*/
		return NULL;
	}

	++InStream; /*Plus one for the newline. We want to skip past it.*/

	return (char*)InStream;
}

/*This function was so useful I gave it external linkage.*/
char *WhitespaceArg(const char *InStream)
{  /*This is used for parsing lines that need to be divided by spaces.*/
	while (*InStream != ' ' && *InStream != '\t' &&
			*InStream != '\n' && *InStream != '\0') ++InStream;
	
	if (*InStream == '\n' || *InStream == '\0')
	{
		return NULL;
	}
	
	while (*InStream == ' ' || *InStream == '\t') ++InStream;
	
	if (*InStream == '\0' || *InStream == '\n')
	{
		return NULL;
	}
	
	return (char*)InStream;
}


static void ConfigProblem(const char *File, short Type, const char *Attribute, const char *AttribVal, unsigned LineNum)
{ /*Special little error handler used by InitConfig() to prevent repetitive duplicate errors.*/
	char TmpBuf[1024];
	char LogBuffer[MAX_LINE_SIZE];

	switch (Type)
	{
		case CONFIG_EMISSINGVAL:
			snprintf(TmpBuf, 1024, "Missing or bad value for attribute %s in %s line %u.\nIgnoring.",
					Attribute, File, LineNum);
			break;
		case CONFIG_EBADVAL:
			snprintf(TmpBuf, 1024, "Bad value %s for attribute %s in %s line %u.", AttribVal, Attribute, File, LineNum);
			break;
		case CONFIG_ETRUNCATED:
			snprintf(TmpBuf, 1024, "Attribute %s in %s line %u has\n"
					"abnormally long value and may have been truncated.", Attribute, File, LineNum);
			break;
		case CONFIG_EAFTER:
			snprintf(TmpBuf, 1024, "Attribute %s cannot be set after an ObjectID attribute; "
					"%s line %u. Ignoring.", Attribute, File, LineNum);
			break;
		case CONFIG_EBEFORE:
			snprintf(TmpBuf, 1024, "Attribute %s comes before any ObjectID attribute.\n"
					"%s line %u. Ignoring.", Attribute, File, LineNum);
			break;
		case CONFIG_ELARGENUM:
			snprintf(TmpBuf, 1024, "Attribute %s in %s line %u has\n"
					"abnormally high numeric value and may cause malfunctions.", Attribute, File, LineNum);
			break;
		default:
			return;
	}
	
	snprintf(LogBuffer, MAX_LINE_SIZE, CONFIGWARNTXT "%s\n", TmpBuf);
	
	SpitWarning(TmpBuf);
	WriteLogLine(LogBuffer, true);
}

ReturnCode InitConfig(const char *CurConfigFile)
{ /*Set aside storage for the table.*/
	FILE *Descriptor = NULL;
	struct stat FileStat;
	char *ConfigStream = NULL, *Worker = NULL;
	ObjTable *CurObj = NULL, *ObjWorker = NULL;
	char DelimCurr[MAX_LINE_SIZE] = { '\0' };
	unsigned LineNum = 1;
	const char *CurrentAttribute = NULL;
	Bool LongComment = false;
	Bool TrueLogEnable = EnableLogging;
	Bool PrevLogInMemory = LogInMemory;
	char ErrBuf[MAX_LINE_SIZE];
	const Bool IsPrimaryConfigFile = !strcmp(ConfigFile, CurConfigFile);
	
	if (IsPrimaryConfigFile)
	{
		EnableLogging = true; /*To temporarily turn on the logging system.*/
		LogInMemory = true;
	}
	
	/*Get the file size of the config file.*/
	if (stat(CurConfigFile, &FileStat) != 0)
	{ /*Failure?*/
		snprintf(ErrBuf, sizeof ErrBuf, CONFIGERRORTXT "Failed to obtain information about config file \"%s\".\nDoes it exist?", CurConfigFile);
		
		SpitError(ErrBuf);
		if (!IsPrimaryConfigFile) WriteLogLine(ErrBuf, true);
		
		return FAILURE;
	}
	else
	{ /*No? Use the file size to allocate space in memory, since a char is a byte big.
	* If it's not a byte on your platform, your OS is not UNIX, and Epoch was not designed for you.*/
		ConfigStream = malloc(FileStat.st_size + 1);
	}

	if (!(Descriptor = fopen(CurConfigFile, "r"))) /*Open the configuration file.*/
	{
		snprintf(ErrBuf, sizeof ErrBuf, CONFIGERRORTXT "Unable to open configuration file \"%s\"! Permissions?", CurConfigFile);
		SpitError(ErrBuf);
		if (!IsPrimaryConfigFile) WriteLogLine(ErrBuf, true);
		EmergencyShell();
	}
	
	/*Read the file into memory. I don't really trust fread(), but oh well.
	 * People will whine if I use a loop instead.*/
	fread(ConfigStream, 1, FileStat.st_size, Descriptor);
	fclose(Descriptor); /*Close the file.*/

	ConfigStream[FileStat.st_size] = '\0'; /*Null terminate.*/
	
	Worker = ConfigStream;
	
	/*Check for non-ASCII characters.*/
	while (*(unsigned char*)Worker++ != '\0')
	{
		if ((*(unsigned char*)Worker & 128) == 128)
		{ /*Check for a sign bit or >= 128. Works for one's complement systems
			too if we have signed char by default, but who uses one's complement?*/
			
			snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Non-ASCII characters detected in configuration file \"%s\"!\n"
						"Epoch does not support Unicode or the like!", CurConfigFile);
			SpitWarning(ErrBuf);
			WriteLogLine(ErrBuf, true);
			break;
		}
	}
	
	Worker = ConfigStream;
	
	/*Empty file?*/
	if ((*Worker == '\n' && *(Worker + 1) == '\0') || *Worker == '\0')
	{
		snprintf(ErrBuf, sizeof ErrBuf, "Seems that the config file \"%s\" is empty or corrupted.", CurConfigFile);
		SpitError(ErrBuf);
		if (!IsPrimaryConfigFile) WriteLogLine(ErrBuf, true);
		free(ConfigStream);
		return FAILURE;
	}
	
	do /*This loop does most of the parsing.*/
	{
		
		/*Allow whitespace to precede a line in case people want to create a block-styled appearance.*/
		while (*Worker == ' ' || *Worker == '\t') ++Worker;
		
		/**Multi-line comment support: Multi-line comments are created in the following way:
		 * >!> stuff
		 * stuff
		 * stuff stuff
		 * stuffy stuff
		 * <!< stuff
		 * stuff
		 * 
		 * It is not recognized to place a multi-line comment beginner or terminator anywhere but the beginning
		 * of the line. As such, one may place do things like "ObjectID >!>" to create an object with ID ">!>". **/

		if (!strncmp(Worker, "<!<", strlen("<!<")))
		{ /*It's probably not good to have stray multi-line comment terminators around.*/
			if (!LongComment)
			{
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Stray multi-line comment terminator in \"%s\" line %u\n", CurConfigFile, LineNum);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
				continue;
			}
			LongComment = false;
			
			/*Allow next line to begin right ater the terminator on the same line.*/
			Worker += strlen("<!<");
			while (*Worker == ' ' || *Worker == '\t') ++Worker;
		}
		else if (LongComment)
		{
			continue;
		}
		else if (!strncmp(Worker, ">!>", strlen(">!>")))
		{
			LongComment = true;
			continue;
		}
		
		/**Single-line comments are created by placing "#" at the beginning of the line. Placing them
		 * anywhere else has no effect and as such '#' may be used in commands and object IDs and descriptions.**/
		if (*Worker == '\n')
		{ /*Empty line.*/
			continue;
		}
		else if (*Worker == '#')
		{ /*Line is just a comment.*/
			continue;
		}
		
		/**Global configuration begins here.**/
		if (!strncmp(Worker, (CurrentAttribute = "Import"), sizeof "Import" - 1))
		{
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (NumConfigFiles == MAX_CONFIG_FILES)
			{
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Cannot import config file \"%s\", config file limit of %d has been reached!\n"
						"Attempting to continue.", DelimCurr, MAX_CONFIG_FILES);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
				continue;
			}
			
			if (*DelimCurr == '/')
			{ /*Absolute path?*/
				ConfigFileList[NumConfigFiles] = malloc(strlen(DelimCurr) + 1);
				
				strncpy(ConfigFileList[NumConfigFiles], DelimCurr, strlen(DelimCurr) + 1);
			}
			else
			{ /*A file in our config folder.*/
				char OutBuf[MAX_LINE_SIZE];
				
				snprintf(OutBuf, sizeof OutBuf, CONFIGDIR "%s", DelimCurr);
				
				ConfigFileList[NumConfigFiles] = malloc(strlen(OutBuf) + 1);
				
				strncpy(ConfigFileList[NumConfigFiles], OutBuf, strlen(OutBuf) + 1);
			}
				
			
			++NumConfigFiles; /*This is incremented prior to the call to InitConfig() for a reason.*/
			
			if (!InitConfig(ConfigFileList[NumConfigFiles - 1])) /*It's very important we pass this pointer and not DelimCurr.*/
			{
				
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGERRORTXT
						"Failed to load imported config file \"%s\"! File is imported on line %u in \"%s\"\n"
						"Please correct your configuration! Attempting to continue.",
						ConfigFileList[NumConfigFiles - 1], LineNum, CurConfigFile);
				SpitError(ErrBuf);
				WriteLogLine(ErrBuf, true);
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "GlobalEnvVar"), sizeof "GlobalEnvVar" - 1))
		{
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strchr(DelimCurr, '='))
			{
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Malformed global environment variable, line %u in \"%s\".\n"
						"Cannot set this environment variable.", LineNum, CurConfigFile);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
				continue;
			}
			
			EnvVarList_Add(DelimCurr, &GlobalEnvVars);
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "DisableCAD"), sizeof "DisableCAD" - 1))
		{ /*Should we disable instant reboots on CTRL-ALT-DEL?*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}

			if (!strcmp(DelimCurr, "true"))
			{
				DisableCAD = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				DisableCAD = false;
			}
			else
			{
				DisableCAD = true;
				
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "BlankLogOnBoot"), sizeof "BlankLogOnBoot" - 1))
		{ /*Should the log only hold the current boot cycle's logs?*/

			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}

			if (!strcmp(DelimCurr, "true"))
			{
				BlankLogOnBoot = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				BlankLogOnBoot = false;
			}
			else
			{				
				BlankLogOnBoot = false;
				
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
			}

			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "EnableLogging"), sizeof "EnableLogging" - 1))
		{
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);

				continue;
			}
			
			if (!strcmp(DelimCurr, "true"))
			{
				TrueLogEnable = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				TrueLogEnable = false;
			}
			else
			{
				
				TrueLogEnable = false;
				
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "RunlevelInherits"), sizeof "RunlevelInherits" - 1))
		{
			char Inheriter[MAX_DESCRIPT_SIZE], Inherited[MAX_DESCRIPT_SIZE];
			const char *TWorker = DelimCurr;
			unsigned TInc = 0;
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			for (; *TWorker != ' ' && *TWorker != '\t' && *TWorker != '\0' && TInc < MAX_DESCRIPT_SIZE - 1; ++TInc, ++TWorker)
			{
				Inheriter[TInc] = *TWorker;
			}
			Inheriter[TInc] = '\0';
			
			if (*TWorker == '\0')
			{
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
				continue;
			}
			
			TWorker = WhitespaceArg(TWorker);
			
			if (strchr(TWorker, ' ') || strchr(TWorker, '\t'))
			{
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
				continue;
			}
			
			snprintf(Inherited, MAX_DESCRIPT_SIZE, "%s", TWorker);
			
			RLInheritance_Add(Inheriter, Inherited);
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "DefinePriority"), sizeof "DefinePriority" - 1))
		{
			char Alias[MAX_DESCRIPT_SIZE] = { '\0' };
			unsigned Target = 0, TInc = 0;
			const char *TWorker = DelimCurr;
			
			if (CurObj != NULL)
			{ /*We can't allow this in object-local options, because then this may not be properly defined.*/
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				
				continue;
			}
			
			for (; *TWorker != ' ' && *TWorker != '\t' &&
				*TWorker != '\0' && TInc < MAX_DESCRIPT_SIZE -1; ++TInc, ++TWorker)
			{ /*Copy in the identifier.*/
				Alias[TInc] = *TWorker;
			}
			Alias[TInc] = '\0';
			
			if (!ValidIdentifierName(Alias))
			{ //We have to check if this contains odd characters we might want to do something with in config ourselves, and disallow it.
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, Alias, LineNum);
				continue;
			}
			
			if (*TWorker == '\0')
			{
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
				continue;
			}
			
			TWorker = WhitespaceArg(TWorker); /*I abuse this delightful little function. It was meant for do-while loops.*/
			
			if (!AllNumeric(TWorker))
			{
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
				continue;
			}
			
			Target = atol(TWorker);
			
			PriorityAlias_Add(Alias, Target); /*Now add it to the linked list.*/
			
			continue;
		}
		/*This will mount /dev, /proc, /sys, /dev/pts, and /dev/shm on boot time, upon request.*/
		else if (!strncmp(Worker, (CurrentAttribute = "MountVirtual"), strlen("MountVirtual")))
		{
			const char *TWorker = DelimCurr;
			unsigned Inc = 0;
			char CurArg[MAX_DESCRIPT_SIZE];
			const char *VirtualID[5] = { "procfs", "sysfs", "devfs", "devpts", "devshm" };
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);

				continue;
			}
			
			do
			{ /*Load in all the arguments in the line.*/
				Bool FoundSomething = false;
				
				for (Inc = 0; TWorker[Inc] != ' ' && TWorker[Inc] != '\t' && TWorker[Inc] != '\n' &&
					TWorker[Inc] != '\0' && Inc < (MAX_DESCRIPT_SIZE - 1); ++Inc)
				{ /*Copy in the argument for this line.*/
					CurArg[Inc] = TWorker[Inc];
				}
				CurArg[Inc] = '\0';
				
				
				for (Inc = 0; Inc < 5; ++Inc)
				{ /*Search through the argument to see what it matches.*/
					unsigned char Options = 0;
					const char *Arg = CurArg;
					
					if (*Arg == '~')
					{ /*Don't report an error if it fails to mount, just silently continue.*/
						Options |= MOUNTVIRTUAL_NOERROR;
						++Arg;
					}
					
					/*Create the directory for mount if it doesn't exist.*/
					if (Arg[strlen(Arg) - 1] == '+') Options |= MOUNTVIRTUAL_MKDIR;
					
					if (!strncmp(VirtualID[Inc], Arg, strlen(VirtualID[Inc])))
					{						
						AutoMountOpts[Inc] = true | Options;
						FoundSomething = true;
						break;
					}
				}

				if (!FoundSomething)
				{ /*If it doesn't match anything, that's bad.*/
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					
					continue;
				}
					
			} while ((TWorker = WhitespaceArg(TWorker)));
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			continue;
		}
		/*Now we get into the actual attribute tags.*/
		else if (!strncmp(Worker, (CurrentAttribute = "BootBannerText"), sizeof "BootBannerText" - 1))
		{ /*The text shown at boot up as a kind of greeter, before we start executing objects. Can be disabled, off by default.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*So, they decided to explicitly opt out of banner display. Ok.*/
			{
				BootBanner.BannerText[0] = '\0';
				BootBanner.BannerColor[0] = '\0';
				BootBanner.ShowBanner = false; /*Should already be false, but to prevent possible bugs...*/
				continue;
			}
			snprintf(BootBanner.BannerText, MAX_LINE_SIZE, "%s", DelimCurr);
			
			BootBanner.ShowBanner = true;
			
			if ((strlen(DelimCurr) + 1) >= MAX_DESCRIPT_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "BootBannerColor"), sizeof "BootBannerColor" - 1))
		{ /*Color for boot banner.*/
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strcmp(DelimCurr, "NONE")) /*They don't want a color.*/
			{
				BootBanner.BannerColor[0] = '\0';
				continue;
			}
			
			SetBannerColor(DelimCurr); /*Function to be found elsewhere will do this for us, otherwise this loop would be even bigger.*/
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "DefaultRunlevel"), sizeof "DefaultRunlevel" - 1))
		{
			if (CurRunlevel[0] != 0)
			{ /*If the runlevel has already been set, don't set it again.
				* This prevents a rather nasty bug.*/
				continue;
			}
			
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}	
			
			snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", DelimCurr);
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "LogFile"), sizeof "LogFile" - 1))
		{ //Specify a log file to use.
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;

			}
			
			DelimCurr[MAX_LINE_SIZE - 1] = '\0';
			strcpy(LogFile, DelimCurr);
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "Hostname"), sizeof "Hostname" - 1))
		{
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;

			}
			
			if (!strncmp(DelimCurr, "FILE", sizeof "FILE" - 1))
			{
				FILE *TDesc;
				unsigned Inc = 0;
				int TChar;
				const char *TW = DelimCurr;
				char THostname[sizeof Hostname];
				
				TW += strlen("FILE");
				
				for (; *TW == ' ' || *TW == '\t'; ++TW);
				
				if (!(TDesc = fopen(TW, "r")))
				{
					snprintf(ErrBuf, sizeof ErrBuf, "Failed to set hostname from file \"%s\".\n", TW);
					SpitWarning(ErrBuf);
					WriteLogLine(ErrBuf, true);
					continue;
				}
				
				for (Inc = 0; (TChar = getc(TDesc)) != EOF && Inc < sizeof Hostname - 1; ++Inc)
				{ /*There is a reason for this. Just trust me.*/
					*(unsigned char*)&THostname[Inc] = (unsigned char)TChar;
				}
				THostname[Inc] = '\0';
				
				/*Skip past spaces, tabs, and newlines.*/
				for (TW = THostname; *TW == '\n' ||
					*TW == ' ' || *TW == '\t'; ++TW);
				
				/*Copy into the real hostname from our new offset.*/
				for (Inc = 0; TW[Inc] != '\0' && TW[Inc] != '\n'; ++Inc)
				{
					Hostname[Inc] = TW[Inc];
				}
				Hostname[Inc] = '\0';
				
				fclose(TDesc);
			}
			else
			{	
				snprintf(Hostname, sizeof Hostname, "%s", DelimCurr);
			}
			
								
			/*Check for spaces and tabs in the actual hostname.*/
			if (strchr(Hostname, ' ') != NULL || strchr(Hostname, '\t') != NULL)
			{
				const char *const ErrString = "Tabs and/or spaces in hostname file. Cannot set hostname.";
				SpitWarning(ErrString);
				WriteLogLine(ErrString, true);
				*Hostname = '\0'; /*Set the hostname back to nothing.*/
				continue;
			}
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			
			if (strlen(Domainname) >= sizeof Domainname)
			{ /*There is limited space most OSes will accept.*/
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "\nHostname attribute on line %u in file \"%s\" has specified\n"
						"that a hostname inter than %u be set.\nThe specified hostname has been truncated\n"
						"to fit in the aforementioned space.", LineNum, CurConfigFile, (unsigned)sizeof Hostname - 1);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "Domainname"), sizeof "Domainname" - 1))
		{
			if (CurObj != NULL)
			{
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strncmp(DelimCurr, "FILE", sizeof "FILE" - 1))
			{ /*Set from file.*/
				char *TWorker = DelimCurr + (sizeof "FILE" - 1);
				char TDomainname[sizeof Domainname];
				FILE *TDesc = NULL;
				int Inc = 0, TChar = 0;
				
				for (; *TWorker == ' ' || *TWorker == '\t'; ++TWorker);
				
				if (*TWorker == '\0')
				{ /*Just Domainname=FILE or something*/
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				if (!(TDesc = fopen(TWorker, "r")))
				{
					snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Failed to set domain name from file \"%s\".", TWorker);
					SpitWarning(ErrBuf);
					WriteLogLine(ErrBuf, true);
					continue;
				}
				
				for (; (TChar = getc(TDesc)) != EOF && Inc < sizeof TDomainname - 1; ++Inc)
				{
					*(unsigned char*)&TDomainname[Inc] = TChar;
				}
				TDomainname[Inc] = '\0';
				
				fclose(TDesc); TDesc = NULL;
				
				/*Skip past newlines etc.*/
				for (TWorker = TDomainname; *TWorker ==  '\n' || *TWorker == ' ' || *TWorker == '\t'; ++TWorker);
				
				/*Copy the data to the master array.*/
				for (Inc = 0; TWorker[Inc] != '\n' && TWorker[Inc] != '\0' && Inc < sizeof Domainname - 1; ++Inc)
				{
					Domainname[Inc] = TWorker[Inc];
				}
				Domainname[Inc] = '\0';
			}
			else
			{
				strncpy(Domainname, DelimCurr, sizeof Domainname - 1);
				Domainname[sizeof Domainname - 1] = '\0';
			}
			
			if (strchr(Domainname, ' ') || strchr(Domainname, '\t'))
			{
				const char *const ErrString = "Tabs and/or spaces in domain name file. Cannot set domain name.";
				SpitWarning(ErrString);
				WriteLogLine(ErrString, true);
				*Domainname = '\0'; /*Set the hostname back to nothing.*/
				continue;
			}
			
			if (strlen(DelimCurr) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
				continue;
			}

			if (strlen(Domainname) >= sizeof Domainname)
			{ /*There is limited space most OSes will accept.*/
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "\nDomainname attribute on line %u in file \"%s\" has specified\n"
						"that a domain name inter than %u be set.\nThe specified domain name has been truncated\n"
						"to fit in the aforementioned space.", LineNum, CurConfigFile, (unsigned)sizeof Domainname - 1);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "StartingStatusFormat"), sizeof "StartingStatusFormat" - 1))
		{ /*The first half of our status format, before we get to Done or FAIL or something.*/
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strncmp(DelimCurr, "FILE", sizeof "FILE" - 1))
			{
				const char *TW = DelimCurr + sizeof "FILE" - 1;
				char Filename[MAX_LINE_SIZE];
				FILE *Desc = NULL;
				struct stat FileStat;
				unsigned ReadSize = 0;
				
				while (*TW == ' ' || *TW == '\t') ++TW;
				
				if (!*TW)
				{
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				/*Copy in the filename.*/
				strncpy(Filename, TW, sizeof Filename - 1);
				Filename[sizeof Filename - 1] = '\0';
				
				if (stat(Filename, &FileStat) != 0 || !(Desc = fopen(Filename, "r")))
				{
					snprintf(ErrBuf, sizeof ErrBuf, "Unable to open file %s for attribute %s!", Filename, CurrentAttribute);
					SpitWarning(ErrBuf);
					continue;
				}
				
				/*Read it in.*/
				if (FileStat.st_size >= sizeof StatusReportFormat.StartFormat)
				{
					ReadSize = sizeof StatusReportFormat.StartFormat - 1;
				}
				else
				{
					ReadSize = FileStat.st_size;
				}
				
				fread(StatusReportFormat.StartFormat, 1, ReadSize, Desc);
				StatusReportFormat.StartFormat[ReadSize] = '\0';
				
				fclose(Desc);
				continue;
			}
			strncpy(StatusReportFormat.StartFormat, DelimCurr, sizeof StatusReportFormat.StartFormat - 1);
			StatusReportFormat.StartFormat[sizeof StatusReportFormat.StartFormat - 1] = '\0';
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "FinishedStatusFormat"), sizeof "FinishedStatusFormat" - 1))
		{ /*The second half of our status report format, e.g. [ DONE ] (but the Done part is defined in the next one*/
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strncmp(DelimCurr, "FILE", sizeof "FILE" - 1))
			{
				const char *TW = DelimCurr + sizeof "FILE" - 1;
				char Filename[MAX_LINE_SIZE];
				FILE *Desc = NULL;
				struct stat FileStat;
				unsigned ReadSize = 0;
				
				while (*TW == ' ' || *TW == '\t') ++TW;
				
				if (!*TW)
				{
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				/*Copy in the filename.*/
				strncpy(Filename, TW, sizeof Filename - 1);
				Filename[sizeof Filename - 1] = '\0';
				
				if (stat(Filename, &FileStat) != 0 || !(Desc = fopen(Filename, "r")))
				{
					snprintf(ErrBuf, sizeof ErrBuf, "Unable to open file %s for attribute %s!", Filename, CurrentAttribute);
					SpitWarning(ErrBuf);
					continue;
				}
				
				/*Read it in.*/
				if (FileStat.st_size >= sizeof StatusReportFormat.FinishFormat)
				{
					ReadSize = sizeof StatusReportFormat.FinishFormat - 1;
				}
				else
				{
					ReadSize = FileStat.st_size;
				}
				
				fread(StatusReportFormat.FinishFormat, 1, ReadSize, Desc);
				StatusReportFormat.FinishFormat[ReadSize] = '\0';
				
				fclose(Desc);
				continue;
			}
			
			strncpy(StatusReportFormat.FinishFormat, DelimCurr, sizeof StatusReportFormat.FinishFormat - 2);
			StatusReportFormat.FinishFormat[sizeof StatusReportFormat.FinishFormat - 2] = '\0'; /*Minus two so we can fit a newline.*/
			
			/*So it doesn't all pile up on one line all weirdy and stuff.*/
			strcat(StatusReportFormat.FinishFormat, "\n");
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "StatusNames"), sizeof "StatusNames" - 1))
		{ /*We specify our status names here, e.g. FAIL, Done, WARN.*/
			unsigned TInc = 0, Lines = 1;
			char *TW2 = NULL;
			
			if (CurObj != NULL)
			{ /*What the warning says. It'd get all weird if we allowed that.*/
				ConfigProblem(CurConfigFile, CONFIG_EAFTER, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strncmp(DelimCurr, "FILE", sizeof "FILE" - 1))
			{
				const char *TW = DelimCurr + sizeof "FILE" - 1;
				char Filename[MAX_LINE_SIZE];
				FILE *Desc = NULL;
				struct stat FileStat;
				char *FileBuf = NULL;

				
				while (*TW == ' ' || *TW == '\t') ++TW;
				
				if (!*TW)
				{
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				/*Copy in the filename.*/
				strncpy(Filename, TW, sizeof Filename - 1);
				Filename[sizeof Filename - 1] = '\0';
				
				if (stat(Filename, &FileStat) != 0 || !(Desc = fopen(Filename, "r")))
				{
					snprintf(ErrBuf, sizeof ErrBuf, "Unable to open file %s for attribute %s!", Filename, CurrentAttribute);
					SpitWarning(ErrBuf);
					continue;
				}
				
				/*Allocate space for the file's contents*/
				FileBuf = malloc(FileStat.st_size + 1);
				
				/*Read the contents in now.*/
				fread(FileBuf, 1, FileStat.st_size, Desc);
				FileBuf[FileStat.st_size] = '\0';
				
				fclose(Desc);
				
				/*Remove trailing whitespace.*/
				for (TInc = FileStat.st_size - 1; TInc > 0 && FileBuf[TInc] == '\n'; --TInc)
				{
					FileBuf[TInc] = '\0';
				}
				
				/*Count lines, make sure they equal three.*/
				for (TW2 = FileBuf; (TW2 = strchr(TW2, '\n')); ++Lines) ++TW2;
				
				if (Lines != 3)
				{ /*Nope.*/
					free(FileBuf); FileBuf = NULL;
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				/*Now actually get that data.*/
				Lines = 0; TInc = 0; TW2 = FileBuf;
				do
				{
					while (*TW2 == '\n') ++TW2;
					
					for (TInc = 0; *TW2 != '\n' && *TW2 != '\0' && TInc < sizeof StatusReportFormat.StatusFormats[Lines] - 1; ++TInc, ++TW2)
					{
						StatusReportFormat.StatusFormats[Lines][TInc] = *TW2;
					}
					StatusReportFormat.StatusFormats[Lines][TInc] = '\0';
				} while (++Lines, (TW2 = strchr(TW2, '\n')));
				
				free(FileBuf); FileBuf = NULL;
				continue;
			}
			
			/*Count number of commas.*/
			for (TW2 = DelimCurr; (TW2 = strchr(TW2, ',')); ++Lines) ++TW2;
			
			if (Lines != 3)
			{ /*Needs to be three.*/
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
				continue;
			}
			
			/*Get the data.*/
			Lines = 0; TW2 = DelimCurr;
			do
			{
				if (*TW2 == ',') ++TW2;
				
				for (TInc = 0; *TW2 != ',' && *TW2 != '\0' && TInc < sizeof StatusReportFormat.StatusFormats[Lines] - 1; ++TInc, ++TW2)
				{
					StatusReportFormat.StatusFormats[Lines][TInc] = *TW2;
				}
				StatusReportFormat.StatusFormats[Lines][TInc] = '\0';
			} while (++Lines, (TW2 = strchr(TW2, ',')));

			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectID"), sizeof "ObjectID" - 1))
		{ /*ASCII value used to identify this object internally, and also a kind of short name for it.*/
			char *Temp = NULL;
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}

			if ((Temp = strpbrk(DelimCurr, " \t")) != NULL) /*We cannot allow whitespace.*/
			{
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "ObjectIDs may not contain whitespace! Truncating up to occurence of whitespace\n"
						"Line %u in %s.", LineNum, CurConfigFile);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
				*Temp = '\0';
			}
			
			if (!ValidIdentifierName(DelimCurr))
			{ //We have to check if this contains odd characters we might want to do something with in config ourselves, and disallow it.
				snprintf(DelimCurr, sizeof DelimCurr, "object%u%u%u_badid", rand(), rand(), rand()); //Generate a new ID since this one is bad.
				
				//Now alert them.
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "ObjectID contains invalid character. Generating new ID: \"%s\".", DelimCurr);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
			}

			
			DelimCurr[MAX_DESCRIPT_SIZE - 1] = '\0'; /*Chop it off to prevent overflow.*/
			
			if (!(CurObj = AddObjectToTable(DelimCurr, CurConfigFile))) /*Sets this as our current object.*/
			{
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Duplicate ObjectID %s detected in config file %s, ignoring.",
						DelimCurr, CurConfigFile);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
				continue;
			}

			if ((strlen(DelimCurr) + 1) >= MAX_DESCRIPT_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}

			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectWorkingDirectory"), sizeof "ObjectWorkingDirectory" - 1))
		{
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectWorkingDirectory != NULL)
			{
				free(CurObj->ObjectWorkingDirectory);
			}
			
			CurObj->ObjectWorkingDirectory = malloc(strlen(DelimCurr) + 1);
			
			strncpy(CurObj->ObjectWorkingDirectory, DelimCurr, strlen(DelimCurr) + 1);	
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectEnabled"), sizeof "ObjectEnabled" - 1))
		{
			if (!CurObj)
			{

				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strcmp(DelimCurr, "true"))
			{
				CurObj->Enabled = true;
			}
			else if (!strcmp(DelimCurr, "false"))
			{
				CurObj->Enabled = false;
			}
			else
			{
				ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectOptions"), sizeof "ObjectOptions" - 1))
		{
			const char *TWorker = DelimCurr;
			unsigned Inc;
			char CurArg[MAX_DESCRIPT_SIZE];
			
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			do
			{
				
				for (Inc = 0; TWorker[Inc] != ' ' && TWorker[Inc] != '\t' && TWorker[Inc] != '\n'
					&& TWorker[Inc] != '\0' && Inc < (MAX_DESCRIPT_SIZE - 1); ++Inc)
				{
					CurArg[Inc] = TWorker[Inc];
				}
				CurArg[Inc] = '\0';
				
				
				if (!strcmp(CurArg, "HALTONLY"))
				{ /*Allow entries that execute on shutdown only.*/
					CurObj->Started = true;
					CurObj->Opts.Persistent = true;
					CurObj->Opts.HaltCmdOnly = true;
				}
				else if (!strcmp(CurArg, "PERSISTENT"))
				{
					CurObj->Opts.Persistent = true;
				}
				else if (!strcmp(CurArg, "RUNONCE"))
				{
					CurObj->Opts.RunOnce = true;
				}
				else if (!strcmp(CurArg, "STARTFAILCRITICAL"))
				{
					CurObj->Opts.StartFailIsCritical = true;
				}
				else if (!strcmp(CurArg, "STOPFAILCRITICAL"))
				{
					CurObj->Opts.StopFailIsCritical = true;
				}
				else if (!strcmp(CurArg, "INTERACTIVE"))
				{
					CurObj->Opts.Interactive = true;
				}
				else if (!strncmp(CurArg, "FORK", sizeof "FORK" - 1))
				{
			#ifndef NOMMU
					CurObj->Opts.Fork = true;
					if (!strcmp(CurArg, "FORKN")) CurObj->Opts.ForkScanOnce = true;
			#else
					snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Object \"%s\" has specified the FORK option,\n"
							"but this is not supported on NOMMU builds. Disabling the object.", CurObj->ObjectID);
					SpitWarning(ErrBuf);
					WriteLogLine(ErrBuf, true);
					CurObj->Enabled = false;
			#endif /*NOMMU*/
				}
				else if (!strcmp(CurArg, "EXEC"))
				{
					CurObj->Opts.Exec = true;
				}
				else if (!strcmp(CurArg, "PIVOT"))
				{
					CurObj->Opts.PivotRoot = true;
				}
				else if (!strcmp(CurArg, "RAWDESCRIPTION"))
				{
					CurObj->Opts.RawDescription = true;
				}
				else if (!strcmp(CurArg, "SERVICE"))
				{
					CurObj->Opts.IsService = true;
				}
				else if (!strncmp(CurArg, "AUTORESTART", sizeof "AUTORESTART" - 1))
				{
					
					CurObj->Opts.AutoRestart = true;

					if (CurArg[sizeof "AUTORESTART" - 1] == '=' && CurArg[sizeof "AUTORESTART"] != '\0')
					{
						const char *Arg = CurArg + sizeof "AUTORESTART=" - 1;
						unsigned short MinimumRestartTime = atoi(Arg);
						
						CurObj->Opts.AutoRestart |= MinimumRestartTime << 1;
					}
					else
					{
						CurObj->Opts.AutoRestart |= 5 << 1;
					}
				}
				else if (!strcmp(CurArg, "NOTRACK"))
				{
					CurObj->Opts.NoTrack = true;
				}
				else if (!strcmp(CurArg, "FORCESHELL"))
				{
					#ifndef NOSHELL
						CurObj->Opts.ForceShell = true;
					#else
						snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Object %s has the option FORCESHELL set,\n"
								"but Epoch was compiled without shell support.\n"
								"Ignoring.", CurObj->ObjectID);
						SpitWarning(ErrBuf);
						WriteLogLine(ErrBuf, true);
					#endif
				}
				else if (!strncmp(CurArg, "NOSTOPWAIT", sizeof "NOSTOPWAIT" - 1))
				{
					CurObj->Opts.NoStopWait = true;
				}
				else if (!strncmp(CurArg, "STOPTIMEOUT", sizeof "STOPTIMEOUT" - 1))
				{
					const char *TWorker = CurArg + sizeof "STOPTIMEOUT" - 1;
					
					if (*TWorker != '=' || *(TWorker + 1) == '\0')
					{
						ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, CurArg, LineNum);
						continue;
					}
					++TWorker;
					
					if (!AllNumeric(TWorker))
					{
						ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, CurArg, LineNum);
						continue;
					}
					
					CurObj->Opts.StopTimeout = atol(TWorker);
				}
				else if (!strncmp(CurArg, "MAPEXITSTATUS", sizeof "MAPEXITSTATUS" - 1))
				{
					const char *TWorker = CurArg + sizeof "MAPEXITSTATUS" - 1;
					unsigned TInc = 0;
					char ExitStatusT[4];
					char ValueT[32];
					
					if (*TWorker != '=' || *(TWorker + 1) == '\0' || !isdigit(*(TWorker + 1)))
					{
						goto BadVal;
					}
					++TWorker;
										
					/*Get the exit status*/
					for (; TWorker[TInc] != ',' && TWorker[TInc] != '\0' &&
						TInc < sizeof ExitStatusT - 1; ++TInc)
					{
						ExitStatusT[TInc] = TWorker[TInc];
					}
					ExitStatusT[TInc] = '\0';
					
					if (*(TWorker += TInc) != ',' || TWorker[1] == '\0')
					{
						goto BadVal;
					}
					++TWorker;
					
					for (TInc = 0; TWorker[TInc] != '\0' && TInc < sizeof ValueT - 1; ++TInc)
					{ /*Get the ReturnCode value.*/
						ValueT[TInc] = TWorker[TInc];
					}
					ValueT[TInc] = '\0';
					
					/*Now we save the value.*/
					for (TInc = 0; CurObj->ExitStatuses[TInc].Value != 3 &&
						TInc < sizeof CurObj->ExitStatuses / sizeof CurObj->ExitStatuses[0]; ++TInc);
					
					if (TInc == sizeof CurObj->ExitStatuses / sizeof CurObj->ExitStatuses[0])
					{
						snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT
								"More than %u MAPEXITSTATUS options specified for object %s.\n"
								"Line %u in \"%s\".", (unsigned int)(sizeof CurObj->ExitStatuses / sizeof CurObj->ExitStatuses[0]),
								CurObj->ObjectID, LineNum, CurConfigFile);
						goto BadVal;
					}
					
					CurObj->ExitStatuses[TInc].ExitStatus = atoi(ExitStatusT);
					
					if (!strcmp(ValueT, "SUCCESS")) CurObj->ExitStatuses[TInc].Value = SUCCESS;
					else if (!strcmp(ValueT, "WARNING")) CurObj->ExitStatuses[TInc].Value = WARNING;
					else if (!strcmp(ValueT, "FAILURE")) CurObj->ExitStatuses[TInc].Value = FAILURE;
					else goto BadVal;
					
					continue;
				BadVal:
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, CurArg, LineNum);
					continue;
				}
				else if (!strncmp(CurArg, "TERMSIGNAL", sizeof "TERMSIGNAL" - 1))
				{
					const char *TWorker = CurArg + sizeof "TERMSIGNAL" - 1;
					
					if (*TWorker != '=' || *(TWorker + 1) == '\0')
					{
						ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, CurArg, LineNum);
						continue;
					}
					
					++TWorker;
					
					if (AllNumeric(TWorker))
					{
						if (atoi(TWorker) > 255)
						{
							ConfigProblem(CurConfigFile, CONFIG_ELARGENUM, CurArg, NULL, LineNum);
						}

						CurObj->TermSignal = atoi(TWorker);
					}
					else if (!strcmp("SIGTERM", TWorker))
					{
						CurObj->TermSignal = SIGTERM;
					}
					else if (!strcmp("SIGKILL", TWorker))
					{
						CurObj->TermSignal = SIGKILL;
					}
					else if (!strcmp("SIGHUP", TWorker))
					{
						CurObj->TermSignal = SIGKILL;
					}
					else if (!strcmp("SIGINT", TWorker))
					{
						CurObj->TermSignal = SIGINT;
					}
					else if (!strcmp("SIGABRT", TWorker))
					{
						CurObj->TermSignal = SIGABRT;
					}
					else if (!strcmp("SIGQUIT", TWorker))
					{
						CurObj->TermSignal = SIGQUIT;
					}
					else if (!strcmp("SIGUSR1", TWorker))
					{
						CurObj->TermSignal = SIGUSR1;
					}
					else if (!strcmp("SIGUSR2", TWorker))
					{
						CurObj->TermSignal = SIGUSR2;
					}
					else
					{
						ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, TWorker, LineNum);
						continue;
					}
				}
				else
				{
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, CurArg, LineNum);
					break;
				}
			} while ((TWorker = WhitespaceArg(TWorker)));
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectDescription"), sizeof "ObjectDescription" - 1))
		{ /*It's description.*/
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectDescription != NULL) free(CurObj->ObjectDescription);
			
			DelimCurr[MAX_DESCRIPT_SIZE - 1] = '\0'; /*Chop it off to prevent overflow.*/

			CurObj->ObjectDescription = malloc(strlen(DelimCurr) + 1);
			strncpy(CurObj->ObjectDescription, DelimCurr, strlen(DelimCurr) + 1);
			
			if ((strlen(DelimCurr) + 1) >= MAX_DESCRIPT_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}

			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectStartCommand"), sizeof "ObjectStartCommand" - 1))
		{ /*What we execute to start it.*/
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectStartCommand) free(CurObj->ObjectStartCommand);

			CurObj->ObjectStartCommand = malloc(strlen(DelimCurr) + 1);
			strncpy(CurObj->ObjectStartCommand, DelimCurr, strlen(DelimCurr) + 1);

			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectPrestartCommand"), sizeof "ObjectPrestartCommand" - 1))
		{
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectPrestartCommand) free(CurObj->ObjectPrestartCommand);
			
			CurObj->ObjectPrestartCommand = malloc(strlen(DelimCurr) + 1);
			strncpy(CurObj->ObjectPrestartCommand, DelimCurr, strlen(DelimCurr) + 1);
			
			if (strlen(DelimCurr) + 1 >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, NULL, LineNum);
				continue;
			}
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectReloadCommand"), sizeof "ObjectReloadCommand" - 1))
		{
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}

			
			if (!strncmp(DelimCurr, "SIGNAL", strlen("SIGNAL")))
			{
				const char *Tag = "SIGNAL";
				const char *TWorker = &DelimCurr[strlen(Tag)];
				
				if (!strcmp(DelimCurr, Tag) ||
					(TWorker[0] != ' ' && 
					TWorker[0] != '\t')) /*No arg. Bad.*/
				{
					char TBuf[MAX_LINE_SIZE];
					
					snprintf(TBuf, sizeof TBuf, "Object \"%s\"'s reload command has 'SIGNAL' specified,\n"
							"but syntax is not valid.", CurObj->ObjectID);
					WriteLogLine(TBuf, true);
					SpitWarning(TBuf);
					continue;
				}
				
				TWorker = WhitespaceArg(TWorker);
				
				if (AllNumeric(TWorker))
				{
					if (atoi(TWorker) > 255)
					{
						ConfigProblem(CurConfigFile, CONFIG_ELARGENUM, CurrentAttribute, NULL, LineNum);
					}
					CurObj->ReloadCommandSignal = (unsigned char)atoi(TWorker);
				}
				else if (!strcmp("SIGTERM", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGTERM;
				}
				else if (!strcmp("SIGKILL", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGKILL;
				}
				else if (!strcmp("SIGHUP", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGKILL;
				}
				else if (!strcmp("SIGINT", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGINT;
				}
				else if (!strcmp("SIGABRT", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGABRT;
				}
				else if (!strcmp("SIGQUIT", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGQUIT;
				}
				else if (!strcmp("SIGUSR1", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGUSR1;
				}
				else if (!strcmp("SIGUSR2", TWorker))
				{
					CurObj->ReloadCommandSignal = SIGUSR2;
				}
				else
				{
					char TBuf[MAX_LINE_SIZE];
					
					snprintf(TBuf, sizeof TBuf, CONFIGWARNTXT
							"ObjectReloadCommand starts with SIGNAL, but the argument to SIGNAL\n"
							"is invalid. Object \"%s\" in %s line %u", CurObj->ObjectID, CurConfigFile, LineNum);
					SpitWarning(TBuf);
					WriteLogLine(TBuf, true);
					continue;
				}
			}
			else
			{
				if (CurObj->ObjectReloadCommand) free(CurObj->ObjectReloadCommand);
				
				CurObj->ObjectReloadCommand = malloc(strlen(DelimCurr) + 1);
				strncpy(CurObj->ObjectReloadCommand, DelimCurr, strlen(DelimCurr) + 1);
			}
			
			if (strlen(DelimCurr) + 1 >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectStopCommand"), sizeof "ObjectStopCommand" - 1))
		{ /*If it's "PID", then we know that we need to kill the process ID only. If it's "NONE", well, self explanitory.*/
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}

			if (!strncmp(DelimCurr, "PIDFILE", strlen("PIDFILE")))
			{ /*They want us to kill a PID file on exit.*/
				const char *Worker = DelimCurr;
				
				CurObj->Opts.StopMode = STOP_PIDFILE;
				
				if (*(Worker += strlen("PIDFILE")) != '\0')
				{ /*It used to be that this was the only way to specify a PID file.*/
                                
					while (*Worker == ' ' || *Worker == '\t')
					{ /*Skip past all spaces and tabs.*/
						++Worker;
					}
					
					if (*Worker != '\0')
					{
						CurObj->ObjectPIDFile = malloc(strlen(Worker) + 1);
						strncpy(CurObj->ObjectPIDFile, Worker, strlen(Worker) + 1);
						
						CurObj->Opts.HasPIDFile = true;
					}
				}
			}
			else if (!strncmp(DelimCurr, "PID", sizeof "PID" - 1))
			{
				CurObj->Opts.StopMode = STOP_PID;
			}
			else if (!strncmp(DelimCurr, "NONE", sizeof "NONE" - 1))
			{
				CurObj->Opts.StopMode = STOP_NONE;
			}
			else
			{
				CurObj->Opts.StopMode = STOP_COMMAND;
				
				if (CurObj->ObjectStopCommand) free(CurObj->ObjectStopCommand);
				CurObj->ObjectStopCommand = malloc(strlen(DelimCurr) + 1);
				strncpy(CurObj->ObjectStopCommand, DelimCurr, strlen(DelimCurr) + 1);
			}
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectStartPriority"), sizeof "ObjectStartPriority" - 1))
		{
			/*The order in which this item is started. If it is disabled in this runlevel, the next object in line is executed, IF
			 * and only IF it is enabled. If not, the one after that and so on.*/
			
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!AllNumeric(DelimCurr)) /*Make sure we are getting a number, not Shakespeare.*/
			{ /*No number? We're probably looking at an alias.*/
				unsigned TmpTarget = 0;
				signed int Change = 0;
				Bool PositiveChange = false;
				
				char *Lookup = strpbrk(DelimCurr, "-+");
				if (Lookup != NULL && AllNumeric(Lookup + 1))
				{ //They provided an increment or decrement.
					PositiveChange = *Lookup == '+';
					*Lookup = '\0';
					Change = atoi(Lookup + 1);
				}
				
				if (!(TmpTarget = PriorityAlias_Lookup(DelimCurr)) && !(TmpTarget = PriorityOfLookup(DelimCurr, true)))
				{
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				if (Change)
				{ //They incremented or decremented.
					if (PositiveChange)
					{
						TmpTarget += Change;
					}
					else
					{
						TmpTarget -= Change;
					}
				}
				CurObj->ObjectStartPriority = TmpTarget;
				continue;
			}
			
			CurObj->ObjectStartPriority = atol(DelimCurr);
			
			if (strlen(DelimCurr) >= 8)
			{ /*An eight digit number is too high.*/
				ConfigProblem(CurConfigFile, CONFIG_ELARGENUM, CurrentAttribute, NULL, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectStopPriority"), sizeof "ObjectStopPriority" - 1))
		{
			/*Same as above, but used for when the object is being shut down.*/
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!AllNumeric(DelimCurr))
			{
				unsigned TmpTarget = 0;
				signed int Change = 0;
				Bool PositiveChange = false;
				
				char *Lookup = strpbrk(DelimCurr, "-+");
				if (Lookup != NULL && AllNumeric(Lookup + 1))
				{ //They provided an increment or decrement.
					PositiveChange = *Lookup == '+';
					*Lookup = '\0';
					Change = atoi(Lookup + 1);
				}
				
				
				if (!(TmpTarget = PriorityAlias_Lookup(DelimCurr)) && !(TmpTarget = PriorityOfLookup(DelimCurr, false)))
				{
					ConfigProblem(CurConfigFile, CONFIG_EBADVAL, CurrentAttribute, DelimCurr, LineNum);
					continue;
				}
				
				if (Change)
				{ //They incremented or decremented.
					if (PositiveChange)
					{
						TmpTarget += Change;
					}
					else
					{
						TmpTarget -= Change;
					}
				}
				
				CurObj->ObjectStopPriority = TmpTarget;
				continue;
			}
			
			CurObj->ObjectStopPriority = atol(DelimCurr);
			
			if (strlen(DelimCurr) >= 8)
			{ /*An eight digit number is too high.*/
				ConfigProblem(CurConfigFile, CONFIG_ELARGENUM, CurrentAttribute, NULL, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectPIDFile"), sizeof "ObjectPIDFile" - 1))
		{ /*This really needs to be specified if Opts.StopMode is STOP_PIDFILE, or we'll reset the object to STOP_PID.*/
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectPIDFile) free(CurObj->ObjectPIDFile);
			
			CurObj->ObjectPIDFile = malloc(strlen(DelimCurr) + 1);
			strncpy(CurObj->ObjectPIDFile, DelimCurr, strlen(DelimCurr) + 1);
			
			CurObj->Opts.HasPIDFile = true;
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectUser"), sizeof "ObjectUser" - 1))
		{
			struct passwd *UserStruct = NULL;
			
			if (CurObj == NULL)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!(UserStruct = getpwnam(DelimCurr)))
			{ /*getpwnam_r() is more trouble than it's worth in single-threaded Epoch.*/
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT
						"Unable to lookup requested USER \"%s\" for object \"%s\".\n"
						"Line %u in %s", DelimCurr, CurObj->ObjectID, LineNum, CurConfigFile);
				WriteLogLine(ErrBuf, true);
				SpitWarning(ErrBuf);
				continue;
			}
			
			CurObj->UserID = (unsigned)UserStruct->pw_uid;
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectGroup"), sizeof "ObjectGroup" - 1))
		{
			struct group *GroupStruct = NULL;
			
			if (CurObj == NULL)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!(GroupStruct = getgrnam(DelimCurr)))
			{ /*getgrnam_r() is more trouble than it's worth in single-threaded Epoch.*/
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT
						"Unable to lookup requested GROUP \"%s\" for object \"%s\".\n"
						"Line %u in %s", DelimCurr, CurObj->ObjectID, LineNum, CurConfigFile);
				WriteLogLine(ErrBuf, true);
				SpitWarning(ErrBuf);
				continue;
			}
			
			CurObj->GroupID = (unsigned)GroupStruct->gr_gid;
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectStdout"), sizeof "ObjectStdout" - 1))
		{
			if (CurObj == NULL)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectStdout) free(CurObj->ObjectStdout);
			
			if (!strcmp(DelimCurr, "LOG"))
			{
				CurObj->ObjectStdout = malloc(strlen(LogFile) + 1);

				strncpy(CurObj->ObjectStdout, LogFile, strlen(LogFile) + 1);
			}
			else
			{
				CurObj->ObjectStdout = malloc(strlen(DelimCurr) + 1);
				strncpy(CurObj->ObjectStdout, DelimCurr, strlen(DelimCurr) + 1);
				
				if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
				{
					ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
				}
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectStderr"), sizeof "ObjectStderr" - 1))
		{
			if (CurObj == NULL)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectStderr) free(CurObj->ObjectStderr);
			
			if (!strcmp(DelimCurr, "LOG"))
			{
				CurObj->ObjectStderr = malloc(strlen(LogFile) + 1);

				strncpy(CurObj->ObjectStderr, LogFile, strlen(LogFile) + 1);
			}
			else
			{
				CurObj->ObjectStderr = malloc(strlen(DelimCurr) + 1);
				strncpy(CurObj->ObjectStderr, DelimCurr, strlen(DelimCurr) + 1);
				
				if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
				{
					ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
				}
			}
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectEnvVar"), sizeof "ObjectEnvVar" - 1))
		{
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (!strchr(DelimCurr, '='))
			{
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT"Malformed environment variable for object %s,\n"
						"in file \"%s\" line %u. Not setting this environment variable.", CurObj->ObjectID, CurConfigFile, LineNum);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
				continue;
			}
			
			EnvVarList_Add(DelimCurr, &CurObj->EnvVars);
			continue;
		}
		else if (!strncmp(Worker, (CurrentAttribute = "ObjectRunlevels"), sizeof "ObjectRunlevels" - 1))
		{ /*Runlevel.*/
			char *TWorker;
			char TRL[MAX_DESCRIPT_SIZE], *TRL2;
			
			if (!CurObj)
			{
				ConfigProblem(CurConfigFile, CONFIG_EBEFORE, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			if (CurObj->ObjectRunlevels != NULL)
			{ /*We cannot have multiple runlevel attributes because it messes up config file editing.*/
				snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Object %s has more than one ObjectRunlevels line.\n"
						"This is not advised because the config file editing code is not smart enough\n"
						"to handle multiple lines. You should put the additional runlevels on the same line.\n"
						"Line %u in %s",
						CurObj->ObjectID, LineNum, CurConfigFile);
				SpitWarning(ErrBuf);
				WriteLogLine(ErrBuf, true);
			}
			
			if (!GetLineDelim(Worker, DelimCurr))
			{
				ConfigProblem(CurConfigFile, CONFIG_EMISSINGVAL, CurrentAttribute, NULL, LineNum);
				continue;
			}
			
			TWorker = DelimCurr;
			
			do
			{
				for (TRL2 = TRL; *TWorker != ' ' && *TWorker != '\t' && *TWorker != '\n' && *TWorker != '\0'; ++TWorker, ++TRL2)
				{
					*TRL2 = *TWorker;
				}
				*TRL2 = '\0';
				
				ObjRL_AddRunlevel(TRL, CurObj);
				
			} while ((TWorker = WhitespaceArg(TWorker)));
			
			if ((strlen(DelimCurr) + 1) >= MAX_LINE_SIZE)
			{
				ConfigProblem(CurConfigFile, CONFIG_ETRUNCATED, CurrentAttribute, DelimCurr, LineNum);
			}
			
			continue;

		}
		else
		{ /*No big deal.*/
			snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "Unidentified attribute in %s on line %u.", CurConfigFile, LineNum);
			SpitWarning(ErrBuf);
			WriteLogLine(ErrBuf, true);
			continue;
		}
	} while (++LineNum, (Worker = NextLine(Worker)));
	
	/*This is harmless, but it's bad form and could indicate human error in writing the config file.*/
	if (LongComment)
	{
		snprintf(ErrBuf, sizeof ErrBuf, CONFIGWARNTXT "No comment terminator at end of config file \"%s\".", CurConfigFile);
		SpitWarning(ErrBuf);
		WriteLogLine(ErrBuf, true);
	}
	
	if (IsPrimaryConfigFile) /*We are at the top level config file and therefore need to clean up.*/
	{
		PriorityAlias_Shutdown();
		
		for (ObjWorker = ObjectTable; ObjWorker->Next; ObjWorker = ObjWorker->Next)
		{
			/*We don't need to specify a description, but if we neglect to, use the ObjectID.*/
			if (ObjWorker->ObjectDescription == NULL)
			{
				ObjWorker->ObjectDescription = ObjWorker->ObjectID;
			}
		}
		
		switch (ScanConfigIntegrity())
		{
			case SUCCESS:
				break;
			case FAILURE:
				/*We failed integrity checking.*/
				ShutdownConfig();
				free(ConfigStream);
				
				return FAILURE;
			case WARNING:
			{
				const char *const WarnTxt = "Noncritical configuration problems exist. Check your logs.";
				
				WriteLogLine(WarnTxt, true);
				SpitWarning(WarnTxt);
				break;
	
			}
		}
		
		LogInMemory = PrevLogInMemory;
		EnableLogging = TrueLogEnable;
	}
	
	free(ConfigStream); /*Release ConfigStream, since we only use the object table now.*/

	return SUCCESS;
}

static ReturnCode GetLineDelim(const char *InStream, char *OutStream)
{
	const char *Worker = InStream;
	char *W2 = OutStream;
	const void *const EndPoint = InStream + (MAX_LINE_SIZE - 1);
	void *const OutStreamStartPoint = OutStream;
	
	/*Jump to the first tab or space. If we get a newline or null, problem.*/
	while (*Worker != '\t' && *Worker != ' ' && *Worker != '=' &&
			*Worker != '\n' && *Worker != '\0') ++Worker;

	/*Hit a null or newline before tab or space. ***BAD!!!*** */
	if (*Worker == '\0' || *Worker == '\n')
	{
		char TmpBuf[1024];
		char ObjectInQuestion[1024];
		const char *TW = Worker;
		unsigned Inc = 0;
		
		for (; *TW != '\n' && *TW != '\0'; ++Inc, ++TW)
		{
			ObjectInQuestion[Inc] = *TW;
		}
		ObjectInQuestion[Inc] = '\0';

		snprintf(TmpBuf, 1024, "No parameter for attribute \"%s\".", ObjectInQuestion);

		SpitError(TmpBuf);

		return FAILURE;
	}
	
	if (*Worker == '=')
	{ /*We give the choice of using whitespace or using an equals sign. It's only nice.*/
		++Worker;
	}
	else
	{
		/*Continue until we are past all tabs and spaces.*/
		while (*Worker == ' ' || *Worker == '\t') ++Worker;
	}

	/*Copy to OutStream, pointed to by W2.*/
	for (; Worker != EndPoint && *Worker != '\n' && *Worker != '\0'; ++Worker, ++W2)
	{
		*W2 = *Worker;
	}
	*W2 = '\0';
	
	/*Nuke spaces and tabs at the end.*/
	for (; W2 != OutStreamStartPoint && (*W2 == '\t' || *W2 == ' '); --W2)
	{
		*W2 = '\0';
	}
	return SUCCESS;
}

ReturnCode MergeImportLine(const char *LineData)
{ //I'm so tired of bugs in my config writing functions that I wrote this one with nice, safe, pointer black magic. I hope it helps.
	char NewData[MAX_LINE_SIZE];
	
	//Build the new line.

	
	struct stat FileStat;
	
	if (stat(ConfigFile, &FileStat) != 0) return FAILURE;

	FILE *Descriptor = fopen(ConfigFile, "rb");
	
	if (!Descriptor) return FAILURE;
	
	const char *const MasterStream = calloc(FileStat.st_size + 1, 1);
	
	//Read in the config file.
	fread((char*)MasterStream, 1, FileStat.st_size, Descriptor);
	((char*)MasterStream)[FileStat.st_size] = '\0';
	fclose(Descriptor);
	
	//Search for ObjectIDs, because we'll write the line before the first of those.
	char *EndSearch = strstr(MasterStream, "ObjectID");
	const char *HalfTwo = NULL;
	
	if (EndSearch)
	{
		HalfTwo = strdup(EndSearch);
		*(char*)EndSearch = '\0';
	}
	
		
	if (MasterStream[strlen(MasterStream) - 1] != '\n')
	{
		snprintf(NewData, sizeof NewData, "\nImport=%s\n", LineData);
	}
	else
	{
		snprintf(NewData, sizeof NewData, "Import=%s\n", LineData);
	}
	
	//Write the file back to disk.
	Descriptor = fopen(ConfigFile, "wb");
	
	if (!Descriptor)
	{
		if (HalfTwo) free((void*)HalfTwo);
		free((void*)MasterStream);
		return FAILURE;
	}
	
	fwrite(MasterStream, 1, strlen(MasterStream), Descriptor);
	fwrite(NewData, 1, strlen(NewData), Descriptor);
	if (HalfTwo) fwrite(HalfTwo, 1, strlen(HalfTwo), Descriptor);
	fclose(Descriptor);
	
	free((void*)MasterStream);
	if (HalfTwo) free((void*)HalfTwo);
	
	return SUCCESS;
}

ReturnCode UnmergeImportLine(const char *Filename)
{
	struct stat FileStat;
	
	if (stat(ConfigFile, &FileStat) != 0) return FAILURE;
	
	FILE *Descriptor = fopen(ConfigFile, "rb");
	
	if (!Descriptor) return FAILURE;
	
	//Read in the contents.
	const char *MasterStream = calloc(1, FileStat.st_size + 1);
	fread((char*)MasterStream, 1, FileStat.st_size, Descriptor);
	fclose(Descriptor);
	((char*)MasterStream)[FileStat.st_size] = '\0';
	
	const char *Worker = MasterStream;
	const char *HalfTwo = NULL;
	
	do
	{
		if (!strncmp("Import", Worker, sizeof "Import" - 1))
		{
			char ID[MAX_LINE_SIZE];
			if (!GetLineDelim(Worker, ID))
			{
				free((void*)MasterStream);
				return FAILURE;
			}
			
			if (!strcmp(ID, Filename))
			{
				//Isolate half two.
				HalfTwo = strchr(Worker, '\n');
				if (HalfTwo) ++HalfTwo; //Skip past the newline.
				
				//Chop off the offending chunk of data.
				*((char*)Worker) = '\0';
				
				//Write it to disk.
				if ((Descriptor = fopen(ConfigFile, "wb")) == NULL)
				{
					free((void*)MasterStream);
					return FAILURE;
				}
				
				fwrite(MasterStream, 1, strlen(MasterStream), Descriptor);
				if (HalfTwo) fwrite(HalfTwo, 1, strlen(HalfTwo), Descriptor);
				fclose(Descriptor);
				
				return SUCCESS;
			}
				
		}
	} while ((Worker = NextLine(Worker)));
	
	WriteLogLine(Filename, true);
	return FAILURE;
}

ReturnCode EditConfigValue(const char *File, const char *ObjectID, const char *Attribute, const char *Value)
{ /*Looks up the attribute for the passed ID and replaces the value for that attribute.*/
	char *MasterStream = NULL, *HalfTwo = NULL;
	char *NewValue = NULL, *Worker = NULL, *Stopper = NULL, *LineArm = NULL;
	char LineWorkerR[MAX_LINE_SIZE];
	FILE *Descriptor = NULL;
	char *WhiteSpace = NULL, *LineWorker = NULL;
	struct stat FileStat;
	unsigned Inc = 0, Inc2 = 0, LineNum = 1;
	unsigned NumWhiteSpaces = 0;
	Bool PresentHalfTwo = false;
	
	if (stat(File, &FileStat) != 0)
	{
		char ErrBuf[MAX_LINE_SIZE];
		snprintf(ErrBuf, sizeof ErrBuf, "EditConfigValue(): Failed to stat \"%s\". Does the file exist?", File);
		SpitError(ErrBuf);
		return FAILURE;
	}
	
	if ((Descriptor = fopen(File, "r")) == NULL)
	{
		char ErrBuf[MAX_LINE_SIZE];
		snprintf(ErrBuf, sizeof ErrBuf, "EditConfigValue(): Failed to open \"%s\". Are permissions correct?", File);
		SpitError(ErrBuf);
		return FAILURE;
	}
	
	MasterStream = malloc(FileStat.st_size + 1);
	
	/*Read in the file.*/
	fread(MasterStream, 1, FileStat.st_size, Descriptor);
	MasterStream[FileStat.st_size] = '\0';
	
	/*We don't need this anymore.*/
	fclose(Descriptor);
	
	if (*MasterStream == '\0')
	{
		free(MasterStream);
		return FAILURE;
	}

	/*Erase the newlines at the end of the file so we don't need to mess with them later.*/
	for (; MasterStream[Inc2] != '\0'; ++Inc2);
	
	for (; MasterStream[Inc2] == '\n'; --Inc2)
	{
		MasterStream[Inc2] = '\0';
	}
	
	/*Find the object ID of our object in config.*/
	LineArm = MasterStream; do
	{
		for (Inc = 0; LineArm[Inc] != '\n' && LineArm[Inc] != '\0'; ++Inc)
		{
			LineWorkerR[Inc] = LineArm[Inc];
		}
		LineWorkerR[Inc] = '\0';
		
		/*Skip past any whitespace at the beginning of the line.*/
		for (Inc2 = 0; LineWorkerR[Inc2] == ' ' || LineWorkerR[Inc2] == '\t'; ++Inc2);
		
		LineWorker = &LineWorkerR[Inc2];
		
		if (strncmp(LineWorker, "ObjectID", strlen("ObjectID")) != 0)
		{ /*Not ObjectID?*/
			continue;
		}
		
		/*Assume we found an ObjectID beyond here.*/
		
		/*Get to the beginning of the whitespace. We use Inc2 as a marker for it's beginning*/
		for (Inc2 = 0; LineWorker[Inc2] != ' ' && LineWorker[Inc2] != '\t' &&
			LineWorker[Inc2] != '=' && LineWorker[Inc2] != '\0' && LineWorker[Inc2] != '\n'; ++Inc2);
		
		if (LineWorker[Inc2] == '=')
		{ /* We allow both spaces and whitespace as the delimiter.*/
			NumWhiteSpaces = 1;
		}
		else
		{ /*Count the number of whitespaces.*/
			char *TW = LineWorker + Inc2;
			
			for (NumWhiteSpaces = 0; *TW == ' ' || *TW == '\t'; ++NumWhiteSpaces, ++TW);
		}
			
		if (LineWorker[NumWhiteSpaces + Inc2] == '\0' ||
			LineWorker[NumWhiteSpaces + Inc2] == '\n')
		{ /*Malformed config lines cannot be edited.*/
			free(MasterStream);
			return FAILURE;
		}
		
		if (strcmp(&LineWorker[NumWhiteSpaces + Inc2], ObjectID) != 0)
		{ /*Not the ObjectID we were looking for?*/
			continue;
		}
		
		/*We found it! Save the pointer.*/
		Worker = LineArm + Inc2 + NumWhiteSpaces + strlen(ObjectID);
	} while (++LineNum, (LineArm = NextLine(LineArm)));
	
	if (Worker == NULL)
	{ /*If we didn't find it.*/
		free(MasterStream);
		return FAILURE;
	}
	
	/*Do not accidentally jump to a different object's attribute of the same name.*/
	if ((Stopper = strstr(Worker, "ObjectID")) != NULL)
	{
		*Stopper = '\0';
	}
	
	if (!(Worker = strstr(Worker, Attribute)) || (Worker > &LineArm[0] && *(Worker - 1) == '#'))
	{ /*Doesn't exist for that object? We also ignore comments.*/
		free(MasterStream);
		return FAILURE;
	}
	
	if (Stopper) *Stopper = 'O'; /*set back, use capital O.*/
	
	/*Null-terminate half one.*/
	*Worker++ = '\0';
	
	/*Jump to the whitespace.*/
	for (Inc = 0; Worker[Inc] != ' ' && Worker[Inc] != '\t' &&
		Worker[Inc] != '=' && Worker[Inc] != '\n' && Worker[Inc] != '\0'; ++Inc);
		
	if (Worker[Inc] == '\n' || Worker[Inc] == '\0')
	{ /*Malformed line. Can't edit it.*/
		free(MasterStream);
		return FAILURE;
	}
	
	if (Worker[Inc] == '=')
	{
		NumWhiteSpaces = 1;
		Worker += Inc;
	}
	else
	{ /*Count the whitespace.*/
		for (NumWhiteSpaces = 0, Worker += Inc; Worker[NumWhiteSpaces] == ' ' ||
			Worker[NumWhiteSpaces] == '\t'; ++NumWhiteSpaces);
	}
	
	WhiteSpace = malloc(NumWhiteSpaces + 1);
	
	/*Save the whitespace while incrementing Worker to the value of this line at the same time.*/
	for (Inc2 = 0; *Worker == '=' || *Worker == ' ' || *Worker == '\t'; ++Inc2, ++Worker)
	{
		WhiteSpace[Inc2] = *Worker;
	}
	WhiteSpace[Inc2] = '\0';
	
	/*Jump past the newline on the end of this line.*/
	for (; *Worker != '\n' && *Worker != '\0'; ++Worker);
	
	if (*Worker != '\0')
	{ /*There is more beyond this line.*/
		PresentHalfTwo = true;
		HalfTwo = malloc(strlen(Worker) + 1);
		
		strncpy(HalfTwo, Worker, strlen(Worker) + 1); /*Plus one to copy the null terminator.*/
		
	}
	
	/*Set up the new value.*/
	if (Value == NULL) /**Means we are supposed to DELETE this line.**/
	{
		int Inc = 0;
		NewValue = "";
		
		if (*HalfTwo == '\n') /*We need to delete the newline that follows.*/
		{
			const char *Temp = HalfTwo + 1;
			int Jump = 1;
			
			/*Don't mess up our whitespace please.*/
			while (*Temp == '\t' || *Temp == ' ') ++Jump, ++Temp;
			
			for (; HalfTwo[Inc + Jump] != '\0'; ++Inc)
			{
				HalfTwo[Inc] = HalfTwo[Inc + Jump];
			}
			HalfTwo[Inc] = '\0';
		}
	}
	else
	{
		NewValue = malloc(strlen(Attribute) + NumWhiteSpaces + strlen(Value) + 1);
		snprintf(NewValue, (strlen(Attribute) + NumWhiteSpaces + strlen(Value) + 1),
				"%s%s%s", Attribute, WhiteSpace, Value);
	}
	
	/*Wwe copied the whitespace back into the new value, so release it's memory now.*/
	free(WhiteSpace);
	
	/*Reallocate MasterStream to accomodate the new data.*/
	MasterStream = realloc(MasterStream, strlen(MasterStream) + strlen(NewValue) +
							(PresentHalfTwo ? strlen(HalfTwo) : 0) + 1);
							
	/*Copy in the new string.*/
	snprintf( (MasterStream + strlen(MasterStream)), (strlen(MasterStream) +
				strlen(NewValue) +(PresentHalfTwo ? strlen(HalfTwo) : 0) + 1),
				"%s%s", NewValue, (PresentHalfTwo ? HalfTwo : ""));
				
	/*Release the other variables now that we don't need them.*/
	if (Value) free(NewValue); /*If Value is NULL, that means NewValue is a string literal.*/
	free(HalfTwo);
	
	/*Write the configuration back to disk.*/
	if (!(Descriptor = fopen(File, "w")))
	{
		char ErrBuf[2048];
		snprintf(ErrBuf, sizeof ErrBuf, "Failed to edit attribute %s for object %s in file %s. File unchanged.",
				Attribute, ObjectID, File);
		
		SpitWarning(ErrBuf);
		free(MasterStream);
		return FAILURE;
	}
	
	fwrite(MasterStream, 1, strlen(MasterStream), Descriptor);
	fclose(Descriptor);
	
	/*Release MasterStream.*/
	free(MasterStream);
	
	return SUCCESS;
}

/*Adds an object to the table and, if the first run, sets up the table.*/
static ObjTable *AddObjectToTable(const char *ObjectID, const char *File)
{
	ObjTable *Worker = ObjectTable, *Next, *Prev;
	int Inc = 0;
	/*See, we actually allocate two cells initially. The base and it's node.
	 * We always keep a free one open. This is just more convenient.*/
	if (ObjectTable == NULL)
	{
		ObjectTable = malloc(sizeof(ObjTable));
		ObjectTable->Prev = NULL;
		ObjectTable->Next = NULL;

		Worker = ObjectTable;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(ObjectID, Worker->ObjectID))
		{ /*Do not allow duplicate entries.*/
			return NULL;
		}
	}

	Worker->Next = malloc(sizeof(ObjTable));
	Worker->Next->Next = NULL;
	Worker->Next->Prev = Worker;

	/*These are the only two variables inside that we need to save before we wipe.*/
	Next = Worker->Next;
	Prev = Worker->Prev;
	
	memset(Worker, 0, sizeof(ObjTable)); /*Set everything that is going to be zero to zero.*/
	
	Worker->Next = Next;
	Worker->Prev = Prev;
	
	/*This is the first thing that must ever be initialized, because it's how we tell objects apart.*/
	/*This and all things like it are dynamically allocated to provide aggressive memory savings.*/
	Worker->ObjectID = malloc(strlen(ObjectID) + 1);
	strncpy(Worker->ObjectID, ObjectID, strlen(ObjectID) + 1);
	
	Worker->ConfigFile = File; /*Set the config file. The pointer actually points to an element in ConfigFileList.*/
	
	/*Initialize these to their default values. Used to test integrity before execution begins.*/
	Worker->TermSignal = SIGTERM; /*This can be changed via config.*/
	Worker->Enabled = 2; /*We can indeed store this in a bool you know.
						There's no 1 bit datatype, and in Epoch,
						Bool is just signed char.*/
	Worker->Opts.StopTimeout = 10; /*Ten seconds by default.*/
	
	for (; Inc < sizeof Worker->ExitStatuses / sizeof Worker->ExitStatuses[0]; ++Inc)
	{ /*Set these to their *special* zero.*/
		Worker->ExitStatuses[Inc].Value = 3; /*One above what we will ever see.*/
	}
	
	return Worker;
}

static ReturnCode ScanConfigIntegrity(void)
{ /*Here we check common mistakes and problems.*/
#define IntegrityWarn(msg) WriteLogLine(msg, true), SpitWarning(msg)
	ObjTable *Worker = ObjectTable, *TOffender;
	char TmpBuf[1024];
	ReturnCode RetState = SUCCESS;
	static Bool WasRunBefore = false;
	
	if (ObjectTable == NULL)
	{ /*This can happen if configuration is filled with trash and nothing valid.*/
		SpitError("No objects found in configuration or invalid configuration.");
		return FAILURE;
	}
	
	if (*CurRunlevel == 0 || !ObjRL_ValidRunlevel(CurRunlevel))
	{	
		if (*CurRunlevel == 0)
		{
			SpitError("No default runlevel specified!");
		}
		else
		{
	
			snprintf(TmpBuf, sizeof TmpBuf, "%sThe runlevel \"%s\" does not exist.",
					WasRunBefore ? "A problem has occured in configuration.\n" : "Error booting to default runlevel.\n",
					CurRunlevel);

			SpitError(TmpBuf);
			
			if (WasRunBefore)
			{
				puts("Switch to an existing runlevel and then try to reload the configuration again.");
			}
		}
		
		if (!WasRunBefore)
		{ /*We can ask for a new runlevel if we are just booting, otherwise the other is restored by ReloadConfig().*/
			char NewRL[MAX_DESCRIPT_SIZE];
			Bool BadRL = true;

			do
			{
				printf("Please enter a valid runlevel to continue\n"
						"or strike enter to go to an emergency shell.\n\n--> ");
				
				fgets(NewRL, MAX_DESCRIPT_SIZE, stdin);
				
				if (NewRL[0] == '\n')
				{
					puts("Starting emergency shell as per your request.");
					EmergencyShell();
				}
				
				NewRL[strlen(NewRL) - 1] = '\0'; /*nuke the newline at the end.*/
				
				if (ObjRL_ValidRunlevel(NewRL))
				{
					puts("Runlevel accepted.\n");
					BadRL = false;
	
					snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", NewRL);
				}
				
				if (BadRL) SmallError("The runlevel you entered was not found. Please try again.\n");
			} while (BadRL);
		}
		else
		{
			return FAILURE;
		}
			
	}
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{		
		if (Worker->ObjectStartCommand == NULL && Worker->ObjectStopCommand == NULL && Worker->Opts.StopMode == STOP_COMMAND)
		{
			snprintf(TmpBuf, 1024, "Object %s has neither ObjectStopCommand nor ObjectStartCommand attributes.", Worker->ObjectID);
			SpitError(TmpBuf);
			RetState = FAILURE;
		}
		
		if (!Worker->Opts.HaltCmdOnly && Worker->ObjectStartCommand == NULL)
		{
			snprintf(TmpBuf, 1024, "Object %s has no attribute ObjectStartCommand\nand is not set to HALTONLY.\n"
					"Disabling.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Opts.Exec = false; /*Just in case.*/
			Worker->Opts.PivotRoot = false;
			Worker->Enabled = false;
			Worker->Started = false;
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Opts.HasPIDFile && Worker->Opts.StopMode == STOP_PID)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" is set to stop via tracked PID,\n"
					"but a PID file has been specified! Switching to STOP_PIDFILE from STOP_PID.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Opts.StopMode = STOP_PIDFILE;
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Opts.PivotRoot && Worker->Opts.Exec)
		{ /*What?*/
			snprintf(TmpBuf, 1024, "Object \"%s\" has both EXEC and PIVOT options set!\n"
					"This makes no sense. Disabling the object.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Enabled = false;
			if (RetState) RetState = WARNING;
		}

		if (!Worker->Opts.HasPIDFile && Worker->Opts.StopMode == STOP_PIDFILE)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" is set to stop via PID File,\n"
					"but no PID File attribute specified! Switching to STOP_PID.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Opts.StopMode = STOP_PID;
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Enabled == 2)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has no attribute ObjectEnabled.", Worker->ObjectID);
			SpitError(TmpBuf);
			RetState = FAILURE;
		}
		
		if (Worker->Opts.StopMode != STOP_COMMAND && Worker->Opts.HaltCmdOnly)
		{ /*We put this here instead of InitConfig() because we can't really do anything but disable.*/
			snprintf(TmpBuf, 1024, "Object \"%s\" has HALTONLY set,\n"
					"but stop method is not a command!\nDisabling.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Enabled = false;
			Worker->Started = false;
			Worker->Opts.StopMode = STOP_NONE;
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Opts.PivotRoot && Worker->Opts.HaltCmdOnly)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has the PIVOT option set,\n"
					"but has HALTONLY set as well. Disabling object.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Enabled = false;
			Worker->Started = false;
			Worker->Opts.PivotRoot = false;
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Opts.Exec && Worker->Opts.HaltCmdOnly)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has the EXEC option set,\n"
					"but has HALTONLY set as well. Disabling object.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			Worker->Opts.Exec = false;
			Worker->Enabled = false;
			Worker->Started = false;
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Opts.NoStopWait && Worker->Opts.StopTimeout != 10)
		{ /*Why are you setting a stop timeout and then turning off the thing that uses your new value?*/
			snprintf(TmpBuf, 1024, "Object \"%s\" has both NOSTOPWAIT and STOPTIMEOUT options set.\n"
					"This doesn't seem very useful.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			if (RetState) RetState = WARNING;
		}
			
		
		if (Worker->Opts.PivotRoot && Worker->Opts.StopMode != STOP_NONE)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has the PIVOT option set,\n"
					"but ObjectStopCommand is not NONE. Setting to NONE.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			
			Worker->Opts.StopMode = STOP_NONE;
			Worker->ObjectStopPriority = 0;
			
			if (Worker->ObjectStopCommand)
			{
				free(Worker->ObjectStopCommand);
				Worker->ObjectStopCommand = NULL;
			}
			
			if (RetState) RetState = WARNING;
		}
		
		if (Worker->Opts.PivotRoot && Worker->Opts.HasPIDFile)
		{
			snprintf(TmpBuf, 1024, "Object \"%s\" has the PIVOT option set,\n"
					"but a PID file has been specified. Unsetting PID file attribute.", Worker->ObjectID);
			IntegrityWarn(TmpBuf);
			
			Worker->Opts.HasPIDFile = false;
			
			if (Worker->ObjectPIDFile)
			{
				free(Worker->ObjectPIDFile);
				Worker->ObjectPIDFile = NULL;
			}
			
			if (RetState) RetState = WARNING;
		}
		
		/*Check for duplicate ObjectIDs.*/
		for (TOffender = ObjectTable; TOffender->Next != NULL; TOffender = TOffender->Next)
		{
			if (!strcmp(Worker->ObjectID, TOffender->ObjectID) && Worker != TOffender)
			{
				snprintf(TmpBuf, 1024, "Two objects in configuration with ObjectID \"%s\".", Worker->ObjectID);
				SpitError(TmpBuf);
				RetState = FAILURE;
			}			
		}
	}
			
			
	WasRunBefore = true;
	
	return RetState;
}
	
/*Find an object in the table and return a pointer to it. This function is public
 * because while we don't want other places adding to the table, we do want read
 * access to the table.*/
ObjTable *LookupObjectInTable(const char *ObjectID)
{
	ObjTable *Worker = ObjectTable;

	if (!ObjectTable)
	{
		return NULL;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(Worker->ObjectID, ObjectID))
		{
			return Worker;
		}
	}

	return NULL;
}

static unsigned PriorityOfLookup(const char *const ObjectID, Bool IsStartingMode)
{
	ObjTable *Worker = ObjectTable;
	
	if (!ObjectTable) return 0;
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(ObjectID, Worker->ObjectID))
		{
			return IsStartingMode ? Worker->ObjectStartPriority : Worker->ObjectStopPriority;
		}
	}
	
	return 0;
}



/*Get the max priority number we need to scan.*/
unsigned GetHighestPriority(Bool WantStartPriority)
{
	ObjTable *Worker = ObjectTable;
	unsigned CurHighest = 0;
	unsigned TempNum;
	
	if (!ObjectTable)
	{
		return 0;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		TempNum = (WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority);
		
		if (TempNum > CurHighest)
		{
			CurHighest = TempNum;
		}
		else if (TempNum == 0)
		{ /*We always skip anything with a priority of zero. That's like saying "DISABLED".*/
			continue;
		}
	}
	
	return CurHighest;
}

/*Functions for environment variable management.*/
void EnvVarList_Add(const char *Var, struct _EnvVarList **const List)
{
	struct _EnvVarList *Worker = *List;
	
	if (!*List)
	{
		Worker = *List = malloc(sizeof(struct _EnvVarList));
		Worker->Next = NULL;
		Worker->Prev = NULL;
		
	}
	
	while (Worker->Next) Worker = Worker->Next;
	
	Worker->Next = malloc(sizeof(struct _EnvVarList));
	Worker->Next->Next = NULL;
	Worker->Next->Prev = Worker;
	
	/*Copy in the environment variable.*/
	strncpy(Worker->EnvVar, Var, sizeof Worker->EnvVar - 1);
	Worker->EnvVar[sizeof Worker->EnvVar - 1] = '\0';
}

Bool EnvVarList_Del(const char *const Check, struct _EnvVarList **const List) /*Check if either the object or the envvar are the same pointer, delete those that match.*/
{
	struct _EnvVarList *Worker = NULL;
	
	if (!Check || !List || !*List) return false;
	
	Worker = *List;
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (Worker->EnvVar == Check)
		{
			if (Worker == *List)
			{
				if (!Worker->Next->Next)
				{
					EnvVarList_Shutdown(List);
				}
				else
				{
					Worker->Next->Prev = NULL;
					*List = Worker->Next;
					free(Worker);
				}
			}
			else
			{
				Worker->Next->Prev = Worker->Prev;
				Worker->Prev->Next = Worker->Next;
				free(Worker);
			}
			return true;
		}
	}
	
	return false;
}

void EnvVarList_Shutdown(struct _EnvVarList **const List)
{
	struct _EnvVarList *Worker = NULL, *Del = NULL;
	
	if (!List) return;
	
	for (Worker = *List; Worker; Worker = Del)
	{
		Del = Worker->Next;
		free(Worker);
	}
	
	*List = NULL;
}

/*Functions for runlevel management.*/
Bool ObjRL_CheckRunlevel(const char *InRL, const ObjTable *InObj, Bool CountInherited)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	Bool RetVal = false;
	
	if (Worker == NULL)
	{
		return false;
	}
	
	if (CountInherited)
	{
		for (; Worker->Next; Worker = Worker->Next)
		{
			if (RLInheritance_Check(InRL, Worker->RL))
			{
				RetVal = 2;
				break;
			}
		}
	}
	
	for (Worker = InObj->ObjectRunlevels; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (!strcmp(Worker->RL, InRL))
		{
			return true;
		}
	}
	
	return RetVal;
}

void ObjRL_AddRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	if (InObj->ObjectRunlevels == NULL)
	{
		InObj->ObjectRunlevels = malloc(sizeof(struct _RLTree));
		
		InObj->ObjectRunlevels->Prev = NULL;
		InObj->ObjectRunlevels->Next = NULL;
		Worker = InObj->ObjectRunlevels;
	}
	
	while (Worker->Next != NULL) Worker = Worker->Next;
	
	Worker->Next = malloc(sizeof(struct _RLTree));
	Worker->Next->Next = NULL;
	Worker->Next->Prev = Worker;
	
	snprintf(Worker->RL, MAX_DESCRIPT_SIZE, "%s", InRL);
}

Bool ObjRL_DelRunlevel(const char *InRL, ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels;
	
	if (Worker == NULL)
	{
		return false;
	}
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(InRL, Worker->RL))
		{
			if (Worker == InObj->ObjectRunlevels)
			{ /*If it's the first node*/
				
				if (InObj->ObjectRunlevels->Next->Next != NULL)
				{ /*Are there other runlevels enabled, or just us?*/
					InObj->ObjectRunlevels->Next->Prev = NULL;
					InObj->ObjectRunlevels = InObj->ObjectRunlevels->Next;
					free(Worker);
				}
				else
				{ /*Apparently just us.*/
					ObjRL_ShutdownRunlevels(InObj);
				}
				
				return true;
			}
				
			/*Otherwise, do this.*/
			Worker->Prev->Next = Worker->Next;
			Worker->Next->Prev = Worker->Prev;	
				
			free(Worker);
			
			return true;
		}
	}
	
	return false;
}

Bool ObjRL_ValidRunlevel(const char *InRL)
{ /*checks if a runlevel has anything at all using it.*/
	const ObjTable *Worker = ObjectTable;
	Bool ValidRL = false;
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!Worker->Opts.HaltCmdOnly && ObjRL_CheckRunlevel(InRL, Worker, true))
		{
			ValidRL = true;
			break;
		}
	}
	
	return ValidRL;
}

void ObjRL_ShutdownRunlevels(ObjTable *InObj)
{
	struct _RLTree *Worker = InObj->ObjectRunlevels, *NDel;
	
	for (; Worker != NULL; Worker = NDel)
	{
		NDel = Worker->Next;
		free(Worker);
	}
	
	InObj->ObjectRunlevels = NULL;
}

static void PriorityAlias_Add(const char *Alias, unsigned Target)
{ /*This code should be simple enough. Just routine linked list stuff.*/
	struct _PriorityAliasTree *Worker = PriorityAliasTree;
	
	if (!PriorityAliasTree)
	{
		PriorityAliasTree = malloc(sizeof(struct _PriorityAliasTree));
		PriorityAliasTree->Next = NULL;
		PriorityAliasTree->Prev = NULL;
		Worker = PriorityAliasTree;
	}
	else
	{ /*If we are the first node, it's not possible for the object to already exist.*/
		for (; Worker->Next; Worker = Worker->Next)
		{
			if (!strcmp(Worker->Alias, Alias))
			{
				return;
			}
		}
	}
	
	Worker->Next = malloc(sizeof(struct _PriorityAliasTree));
	Worker->Next->Next = NULL;
	Worker->Next->Prev = Worker;
	
	strncpy(Worker->Alias, Alias, strlen(Alias) + 1);
	Worker->Target = Target;
}

static void PriorityAlias_Shutdown(void)
{
	struct _PriorityAliasTree *Worker = PriorityAliasTree, *TmpFree = NULL;
	
	if (!PriorityAliasTree) return;
	
	while (Worker != NULL)
	{
		TmpFree = Worker;
		Worker = Worker->Next;
		
		free(TmpFree);
	}
	
	PriorityAliasTree = NULL;
}

static unsigned PriorityAlias_Lookup(const char *Alias)
{ /*Return 0 if we cannot find anything.*/
	struct _PriorityAliasTree *Worker = PriorityAliasTree;
	
	if (!Worker) return 0;
	
	for (; Worker->Next; Worker = Worker->Next)
	{
		if (!strcmp(Worker->Alias, Alias))
		{
			return Worker->Target;
		}
	}
	
	return 0;
}

static void RLInheritance_Add(const char *Inheriter, const char *Inherited)
{
	struct _RunlevelInheritance *Worker = RunlevelInheritance;
	
	if (!Worker)
	{
		RunlevelInheritance = malloc(sizeof(struct _RunlevelInheritance));
		memset(RunlevelInheritance, 0, sizeof(struct _RunlevelInheritance));
		
		Worker = RunlevelInheritance;
	}
	
	while (Worker->Next) Worker = Worker->Next;
	
	Worker->Next = malloc(sizeof(struct _RunlevelInheritance));
	memset(Worker->Next, 0, sizeof(struct _RunlevelInheritance));
	
	Worker->Next->Prev = Worker;
	
	strncpy(Worker->Inheriter, Inheriter, strlen(Inheriter) + 1);
	strncpy(Worker->Inherited, Inherited, strlen(Inherited) + 1);
	
}

static Bool RLInheritance_Check(const char *Inheriter, const char *Inherited)
{ /*Check if Inheriter inherits inherited.*/
	struct _RunlevelInheritance *Worker = RunlevelInheritance;
	
	if (!Worker) return false;
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		if (!strcmp(Inheriter, Worker->Inheriter) && !strcmp(Inherited, Worker->Inherited))
		{
			return true;
		}
	}
	
	return false;
}

static void RLInheritance_Shutdown(void)
{
	struct _RunlevelInheritance *Worker = RunlevelInheritance, *TDel = NULL;
	
	for (; Worker != NULL; Worker = TDel)
	{
		TDel = Worker->Next;
		free(Worker);
	}
	
	RunlevelInheritance = NULL;
}

ObjTable *GetObjectByPriority(const char *ObjectRunlevel, ObjTable *LastNode, Bool WantStartPriority, unsigned ObjectPriority)
{ /*The primary lookup function to be used when executing commands.*/
	ObjTable *Worker = LastNode ? LastNode->Next : ObjectTable;
	unsigned WorkerPriority = 0;
	
	if (!ObjectTable)
	{
		return (void*)-1; /*Error.*/
	}
	
	for (; Worker->Next != NULL; Worker = Worker->Next)
	{
		WorkerPriority = (WantStartPriority ? Worker->ObjectStartPriority : Worker->ObjectStopPriority);
		
		if ((ObjectRunlevel == NULL || ((WantStartPriority || !Worker->Opts.HaltCmdOnly) &&
			(ObjRL_CheckRunlevel(ObjectRunlevel, Worker, true) || (CurrentBootMode == BOOT_BOOTUP && KCmdLineObjCmd_Check(Worker->ObjectID, true))) )) && WorkerPriority == ObjectPriority)
		{
			return Worker;
		}
	}
	
	return NULL;
}

void ShutdownConfig(void)
{
	ObjTable *Worker = ObjectTable, *Temp;
	unsigned Inc = 1;
	
	EnvVarList_Shutdown(&GlobalEnvVars);
	
	for (; Worker != NULL; Worker = Temp)
	{
		if (Worker->Next)
		{
			if (Worker->ObjectID) free(Worker->ObjectID);
			
			if (Worker->ObjectDescription &&
				Worker->ObjectDescription != Worker->ObjectID) free(Worker->ObjectDescription);
				
			if (Worker->ObjectStartCommand) free(Worker->ObjectStartCommand);
			if (Worker->ObjectStopCommand) free(Worker->ObjectStopCommand);
			if (Worker->ObjectReloadCommand) free(Worker->ObjectReloadCommand);
			if (Worker->ObjectPrestartCommand) free(Worker->ObjectPrestartCommand);
			if (Worker->ObjectPIDFile) free(Worker->ObjectPIDFile);
			if (Worker->ObjectWorkingDirectory) free(Worker->ObjectWorkingDirectory);
			if (Worker->ObjectStdout) free(Worker->ObjectStdout);
			if (Worker->ObjectStderr) free(Worker->ObjectStderr);
			
			ObjRL_ShutdownRunlevels(Worker);
			EnvVarList_Shutdown(&Worker->EnvVars);
		}
		
		Temp = Worker->Next;
		free(Worker);
	}
	
	RLInheritance_Shutdown();
	ObjectTable = NULL;
	
	/*Release all config file names.*/
	for (; Inc < MAX_CONFIG_FILES && ConfigFileList[Inc] != NULL; ++Inc)
	{ /*Inc is initialized to ONE. Do not try to free 0, that points to an array on the stack!*/
		free(ConfigFileList[Inc]);
		ConfigFileList[Inc] = NULL;
	}
}

ReturnCode ReloadConfig(void)
{ /*This function is somewhat hard to read, but it does the job well.*/
	ObjTable *Worker = ObjectTable;
	ObjTable *TRoot = malloc(sizeof(ObjTable)), *SWorker = TRoot, *Temp = NULL;
	struct _RLTree *RLTemp1 = NULL, *RLTemp2 = NULL;
	Bool GlobalOpts[3], ConfigOK = true;
	struct _RunlevelInheritance *RLIRoot = NULL, *RLIWorker[2] = { NULL };
	char RunlevelBackup[MAX_DESCRIPT_SIZE];
	void *TempPtr = NULL, *TempPtr2 = NULL;
	struct _EnvVarList *GlobalEnvWorker, *GlobalEnvRoot = NULL;
	char *BackupConfigFileList[MAX_CONFIG_FILES] = { ConfigFile };
	int Inc = 1;
	
	WriteLogLine("CONFIG: Reloading configuration.\n", true);
	WriteLogLine("CONFIG: Backing up current configuration.", true);
	
	/*Backup the current runlevel.*/
	snprintf(RunlevelBackup, MAX_DESCRIPT_SIZE, "%s", CurRunlevel);
	
	/*Backup the config file names.*/
	for (; Inc < MAX_CONFIG_FILES; ++Inc)
	{
		BackupConfigFileList[Inc] = ConfigFileList[Inc];
		ConfigFileList[Inc] = NULL;
	}
	
	/*Backup the global environment variables.*/
	if (GlobalEnvVars)
	{
		for (GlobalEnvWorker = GlobalEnvVars; GlobalEnvWorker->Next; GlobalEnvWorker = GlobalEnvWorker->Next)
		{
			EnvVarList_Add(GlobalEnvWorker->EnvVar, &GlobalEnvRoot);
		}
		GlobalEnvVars = NULL;
	}
	
	for (; Worker->Next != NULL; Worker = Worker->Next, SWorker = SWorker->Next)
	{
		*SWorker = *Worker; /*Direct as-a-unit copy of the main list node to the backup list node.*/
		SWorker->Prev = TempPtr;
		SWorker->Next = malloc(sizeof(ObjTable));
		SWorker->Next->Next = NULL;
		TempPtr = SWorker;
		
		/*Backup the dynamically allocated strings.*/
		SWorker->ObjectID = Worker->ObjectID;
		Worker->ObjectID = NULL;
		
		SWorker->ObjectDescription = Worker->ObjectDescription;
		Worker->ObjectDescription = NULL;
		
		SWorker->ObjectStartCommand = Worker->ObjectStartCommand;
		Worker->ObjectStartCommand = NULL;
		
		SWorker->ObjectStopCommand = Worker->ObjectStopCommand;
		Worker->ObjectStopCommand = NULL;
		
		SWorker->ObjectPrestartCommand = Worker->ObjectPrestartCommand;
		Worker->ObjectPrestartCommand = NULL;
		
		SWorker->ObjectReloadCommand = Worker->ObjectReloadCommand;
		Worker->ObjectReloadCommand = NULL;
		
		SWorker->ObjectPIDFile = Worker->ObjectPIDFile;
		Worker->ObjectPIDFile = NULL;
		
		SWorker->ObjectWorkingDirectory = Worker->ObjectWorkingDirectory;
		Worker->ObjectWorkingDirectory = NULL;
		
		SWorker->ObjectStdout = Worker->ObjectStdout;
		Worker->ObjectStdout = NULL;
		
		SWorker->ObjectStderr = Worker->ObjectStderr;
		Worker->ObjectStderr = NULL;
	
		SWorker->EnvVars = Worker->EnvVars;
		Worker->EnvVars = NULL;
		
		if (!Worker->ObjectRunlevels)
		{
			continue;
		}
		
		RLTemp2 = SWorker->ObjectRunlevels = malloc(sizeof(struct _RLTree));
		
		TempPtr2 = NULL;
		for (RLTemp1 = Worker->ObjectRunlevels; RLTemp1->Next; RLTemp1 = RLTemp1->Next)
		{
			*RLTemp2 = *RLTemp1;
			RLTemp2->Prev = TempPtr2;
			RLTemp2->Next = malloc(sizeof(struct _RLTree));
			RLTemp2->Next->Next = NULL;
			TempPtr2 = RLTemp2;
			
			RLTemp2 = RLTemp2->Next;
		}
	}

	/*Back up the runlevel inheritance table.*/
	if (RunlevelInheritance != NULL)
	{
		RLIRoot = RLIWorker[1] = malloc(sizeof(struct _RunlevelInheritance));
		memset(RLIWorker[1], 0, sizeof(struct _RunlevelInheritance));
		TempPtr = NULL;
		
		for (RLIWorker[0] = RunlevelInheritance; RLIWorker[0]->Next; RLIWorker[0] = RLIWorker[0]->Next)
		{
			*RLIWorker[1] = *RLIWorker[0];
			RLIWorker[1]->Prev = TempPtr;
			TempPtr = RLIWorker[1];
			RLIWorker[1]->Next = malloc(sizeof(struct _RunlevelInheritance));
			memset(RLIWorker[1]->Next, 0, sizeof(struct _RunlevelInheritance));
			
			RLIWorker[1] = RLIWorker[1]->Next;
		}
	}
	
	WriteLogLine("CONFIG: Shutting down configuration.", true);
	
	/*Actually do the reload of the config.*/
	ShutdownConfig();
	
	/*Do this to prevent some weird options from being changeable by a config reload.*/
	GlobalOpts[0] = EnableLogging;
	GlobalOpts[1] = DisableCAD;

	WriteLogLine("CONFIG: Initializing new configuration.", true);
	
	if (!InitConfig(ConfigFile))
	{
		WriteLogLine("CONFIG: " CONSOLE_COLOR_RED "FAILED TO RELOAD CONFIGURATION." CONSOLE_ENDCOLOR 
					" Restoring previous configuration from backup.", true);
		SpitError("ReloadConfig(): Failed to reload configuration.\n"
					"Restoring old configuration to memory.\n"
					"Please check Epoch's configuration file for syntax errors.");
		
		ShutdownConfig();
		
		GlobalEnvVars = GlobalEnvRoot;
		ObjectTable = TRoot; /*Point ObjectTable to our new, identical copy of the old tree.*/
		RunlevelInheritance = RLIRoot; /*Restore runlevel inheritance.*/
		
		/*Restore config file names.*/
		for (Inc = 1; Inc < MAX_CONFIG_FILES; ++Inc)
		{
			ConfigFileList[Inc] = BackupConfigFileList[Inc];
		}
		
		/*Restore current runlevel*/
		snprintf(CurRunlevel, MAX_DESCRIPT_SIZE, "%s", RunlevelBackup);
		
		ConfigOK = false;
		
		FinaliseLogStartup(false); /*Write any logs to disk.*/
	}
	
	/*And then restore those options to their previous states.*/
	EnableLogging = GlobalOpts[0];
	DisableCAD = GlobalOpts[1];
	
	if (!ConfigOK) return ConfigOK;
	
	WriteLogLine("CONFIG: Restoring object statuses and deleting backup configuration.", true);
	
	for (SWorker = TRoot; SWorker != NULL; SWorker = Temp)
	{ /*Add back the Started states, so we don't forget to stop services, etc.*/
		
		if (SWorker->Next)
		{
			if ((Worker = LookupObjectInTable(SWorker->ObjectID)))
			{
				Worker->Started = SWorker->Started;
				Worker->ObjectPID = SWorker->ObjectPID;
				Worker->StartedSince = SWorker->StartedSince;
			}
			
			ObjRL_ShutdownRunlevels(SWorker);
			
			if (SWorker->ObjectID) free(SWorker->ObjectID);
			if (SWorker->ObjectDescription &&
				SWorker->ObjectDescription != SWorker->ObjectID) free(SWorker->ObjectDescription);
			if (SWorker->ObjectStartCommand) free(SWorker->ObjectStartCommand);
			if (SWorker->ObjectStopCommand) free(SWorker->ObjectStopCommand);
			if (SWorker->ObjectReloadCommand) free(SWorker->ObjectReloadCommand);
			if (SWorker->ObjectPrestartCommand) free(SWorker->ObjectPrestartCommand);
			if (SWorker->ObjectPIDFile) free(SWorker->ObjectPIDFile);
			if (SWorker->ObjectWorkingDirectory) free(SWorker->ObjectWorkingDirectory);
			if (SWorker->ObjectStdout) free(SWorker->ObjectStdout);
			if (SWorker->ObjectStderr) free(SWorker->ObjectStderr);
			EnvVarList_Shutdown(&SWorker->EnvVars);
		}
		
		Temp = SWorker->Next;
		free(SWorker);
	}
	
	/*Release the backup runlevel inheritance table.*/
	for (; RLIRoot != NULL; RLIRoot = RLIWorker[0])
	{
		RLIWorker[0] = RLIRoot->Next;
		free(RLIRoot);
	}
	
	/*Release the backup global envvars.*/
	EnvVarList_Shutdown(&GlobalEnvRoot);
	
	WriteLogLine("CONFIG: " CONSOLE_COLOR_GREEN "Configuration reload successful." CONSOLE_ENDCOLOR, true);
	puts(CONSOLE_COLOR_GREEN "Epoch: Configuration reloaded." CONSOLE_ENDCOLOR);
	
	FinaliseLogStartup(false); /*Clean up logs in memory.*/
	
	return SUCCESS;
}
