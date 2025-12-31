/**
 * fat32.c
 *
 * PURPOSE: Reads a FAT32 image and provides functionality to print various details about it.
 **/

#define SIZE_OF_FAT_ENTRY 32
#define BITS_PER_BYTE 8
#define BYTES_PER_KB 1024
#define END_OF_CLUSTER_CHAIN 0x0ffffff8
#define MASK_FIRST_HEX 0x0fffffff
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>
#include <wchar.h> /* wint_t */
#include <locale.h>
#include "fat32.h" // .h file that has all the structs

// function forward declarations
void printInfo(void);
void readCluster(uint32_t clusterNum, int depth);
char *removeTrailingSpace(char *string);
uint32_t getNextFatValue(uint32_t currentCluster);
void copyFile(struct DirInfo *targetDir, uint32_t startingCluster, char *givenName, char *nameExtension);
void fetchFile(uint32_t clusterNum, char *target, int numLeft);
unsigned char ChkSum(unsigned char *pFcbName);

// variables
int fd; // error code
char *imageName;
bool foundFile;
off_t fatSectorStart;
off_t dataSectorLocationInSectors;
off_t entriesPerCluster;
off_t bytesPerCluster;
fat32BS bootSector;
fat32FSInfo infoSector;

/**
 * main
 *
 * Initializes variables, validates FAT, checks input, and calls functions
 * @param int argc - number of parameters
 * @param char* argv - command line arguments
 * @returns int - Error or success code
 */
int main(int argc, char *argv[])
{

	uint32_t fatValidation;

	// read in parameters and decide which function we will be performing
	if (argc < 3)
	{
		printf("Incorrect parameters, exiting program.");
		exit(EXIT_FAILURE);
	}

	// get arguments
	imageName = argv[1];

	fd = open(argv[1], O_RDONLY);

	// read in the Boot sector
	read(fd, &bootSector, sizeof(fat32BS));

	// read in FS info
	lseek(fd, bootSector.BPB_BytesPerSec * bootSector.BPB_FSInfo, SEEK_SET);
	read(fd, &infoSector, sizeof(infoSector));

	// calculate the starting point of the data sector
	dataSectorLocationInSectors = bootSector.BPB_RsvdSecCnt + (bootSector.BPB_FATSz32 * bootSector.BPB_NumFATs);

	// calculate FAT starting location
	fatSectorStart = bootSector.BPB_RsvdSecCnt * bootSector.BPB_BytesPerSec;

	// calculate number of directory entries per cluster
	entriesPerCluster = (bootSector.BPB_SecPerClus * bootSector.BPB_BytesPerSec) / sizeof(struct DirInfo);

	// calculate the number of bytes per cluster
	bytesPerCluster = bootSector.BPB_SecPerClus * bootSector.BPB_BytesPerSec;

	// check to see if info sector the signatures match
	if (infoSector.lead_sig != 0x41615252)
	{
		printf("Info sector does not exist, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check to see if jmpboot signatures match
	if ((uint8_t)bootSector.BS_jmpBoot[0] != 0xEB && (uint8_t)bootSector.BS_jmpBoot[0] != 0xE9)
	{
		printf("Jump validation failed, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check to see if root clus >=2
	if (bootSector.BPB_RootClus < 2)
	{
		printf("BPB_RootClus validation failed, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check to see if FATz32 is non 0
	if (bootSector.BPB_FATSz32 == 0)
	{
		printf("BPB_FATSz32 validation failed, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check to see if total sectors less than min clusters
	if (bootSector.BPB_TotSec32 < 65525)
	{
		printf("BPB_TotSec32 validation failed, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check to see if total sectors less than min clusters
	for (int i = 0; i < 12; i++)
	{
		if ((int)bootSector.BPB_reserved[i] != 0)
		{
			printf("BPB_reserved validation failed, exiting program.");
			close(fd);
			exit(EXIT_FAILURE);
		}
	}

	// check to see if low byte of FAT[0] = BPB_Media
	lseek(fd, fatSectorStart + ((SIZE_OF_FAT_ENTRY / BITS_PER_BYTE) * 0), SEEK_SET);
	read(fd, &fatValidation, SIZE_OF_FAT_ENTRY / BITS_PER_BYTE);

	if ((fatValidation & MASK_FIRST_HEX) != (bootSector.BPB_Media + 0x0FFFFF00))
	{
		printf("FAT validation 0 failed, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check to see if FAT[1] is all Fs
	lseek(fd, fatSectorStart + ((SIZE_OF_FAT_ENTRY / BITS_PER_BYTE) * 1), SEEK_SET);
	read(fd, &fatValidation, SIZE_OF_FAT_ENTRY / BITS_PER_BYTE);

	if ((fatValidation & MASK_FIRST_HEX) != 0x0FFFFFFF)
	{
		printf("FAT validation 1 failed, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	// check command line arguments
	if (strcmp(argv[2], "info") == 0)
	{
		printInfo();
	}
	else if (strcmp(argv[2], "list") == 0)
	{
		// skip straight to reading the root cluster, treating it as another directory as Franklin's video said to do
		readCluster(bootSector.BPB_RootClus & MASK_FIRST_HEX, 0);
	}
	else if (strcmp(argv[2], "get") == 0)
	{
		if (argc != 4)
		{
			printf("Incorrect parameters, exiting program. num parameters: %i", argc);
			close(fd);
			exit(EXIT_FAILURE);
		}

		char *path = argv[3];
		char *token;
		int i, count;

		// count number of tokens
		for (i = 0, count = 0; path[i]; i++)
			count += (path[i] == '/');

		token = strtok(path, "/");

		foundFile = false;

		fetchFile(bootSector.BPB_RootClus & MASK_FIRST_HEX, token, count);

		if (foundFile == true)
		{
			printf("File copied into output folder.\n");
		}
		else
		{
			printf("Error, file could not be found. Exiting.");
			fflush(stdout);
			close(fd);
			exit(EXIT_FAILURE);
		}
	}
	else
	{
		printf("Incorrect parameters, exiting program.");
		close(fd);
		exit(EXIT_FAILURE);
	}

	close(fd);
	printf("Done");
}

/**
 * printInfo
 *
 * Prints information regarding the FAT32 volume found in boot and info sector
 * @returns void - NA
 */
void printInfo(void)
{
	long freeSpace;
	long totalSpace;
	long totalUsableSpace;
	long clusterSizeBytes;
	char *driveName;
	char *OEMName;

	// drive name
	driveName = malloc((BS_VolLab_LENGTH + 1) * sizeof(char));
	memcpy(driveName, bootSector.BS_VolLab, 11);
	driveName[BS_VolLab_LENGTH] = '\0';

	// OEM name
	OEMName = malloc((BS_OEMName_LENGTH + 1) * sizeof(char));
	memcpy(OEMName, bootSector.BS_OEMName, 8);
	OEMName[BS_OEMName_LENGTH] = '\0';

	// free space
	freeSpace = (infoSector.free_count * bootSector.BPB_BytesPerSec * bootSector.BPB_SecPerClus) / BYTES_PER_KB;

	// total space
	totalSpace = (bootSector.BPB_TotSec32 * bootSector.BPB_BytesPerSec) / BYTES_PER_KB;

	// total usable space
	totalUsableSpace = ((bootSector.BPB_TotSec32 - bootSector.BPB_RsvdSecCnt - (bootSector.BPB_FATSz32 * bootSector.BPB_NumFATs)) * bootSector.BPB_BytesPerSec) / BYTES_PER_KB;

	// cluster size in bytes
	clusterSizeBytes = bootSector.BPB_BytesPerSec * bootSector.BPB_SecPerClus;

	printf("Drive name: %s\n", driveName);
	printf("OEM name: %s\n", OEMName);
	printf("Free space is %ld KB\n", freeSpace);
	printf("Total space is %ld KB\n", totalSpace);
	printf("Total usable space %ld KB\n", totalUsableSpace);
	printf("Cluster size in sectors %i\n", bootSector.BPB_SecPerClus);
	printf("Cluster size is %ld bytes\n", clusterSizeBytes);

	free(driveName);
	free(OEMName);
}

/**
 * readCluster
 *
 * Reads a given cluster and prints out all files and directories inside it (including long names), if it encounters a subdirectory it recursively calls itself
 * @param uint32_t clusterNum - The cluster number in data region to read from
 * @param int depth - What level of directory we are at (starts at 0), used for printings dashes and for excluding . and .. entries
 * @returns void - NA
 */
void readCluster(uint32_t clusterNum, int depth)
{
	unsigned char *entryName; // name of the directory that we will print
	char *givenName;		  // non extension part of name
	char *nameExtension;	  // extension part of name
	uint32_t newCluster;
	struct DirInfo currentDir;

	// variables related to long names
	struct LongNameDirInfo currentLongDir;
	bool longNameStarted = false;	// boolean that stores whether this is first long name in chain
	uint16_t *totalLongName = NULL; // memory that will hold long name
	int charsAdded;					// number of chars added to long name memory so far
	int numLongNameEntries;			// number of long name entries read in so far
	unsigned char checkSum;			// verification checksum variable
	uint8_t previousNameOrder;		// verify ordering

	// allocate space
	entryName = malloc(12 * sizeof(char));
	givenName = malloc(9 * sizeof(char));
	nameExtension = malloc(4 * sizeof(char));

	// loop through all entries in the cluster
	for (int i = 0; i < entriesPerCluster; i++)
	{
		// lseek to the beginning of the next entry
		// clusterNum - 2 because the first 2 slots of the FAT table (slots 0 and 1) are occupied by something else and thus do not count
		lseek(fd, (dataSectorLocationInSectors * bootSector.BPB_BytesPerSec) + ((clusterNum - 2) * bytesPerCluster) + (i * sizeof(struct DirInfo)), SEEK_SET);

		// read in the next entry
		read(fd, &currentDir, sizeof(struct DirInfo));

		// read in the name
		memcpy(entryName, currentDir.dir_name, 11);
		entryName[11] = '\0';

		// check to see whether we are at the end
		if ((uint8_t)entryName[0] == 0x00)
		{
			// if it is then there is nothing else for us to read so we just end the loop
			break;
		}
		// make sure we are not looking at dot or dotdot entries and file has not been deleted
		else if ((i > 1 || depth == 0) && (uint8_t)entryName[0] != 0xE5)
		{
			// seperate name into extension and given name
			memcpy(givenName, &entryName[0], 8);
			givenName[8] = '\0';

			memcpy(nameExtension, &entryName[8], 3);
			nameExtension[3] = '\0';

			// get rid of blanks from normal name
			givenName = removeTrailingSpace(givenName);

			// check to see if it is a directory
			if (((currentDir.dir_attr & (ATTR_LONG_NAME_MASK)) != (ATTR_LONG_NAME)) && ((currentDir.dir_attr & (ATTR_DIRECTORY)) == (ATTR_DIRECTORY)) && ((currentDir.dir_attr & (ATTR_HIDDEN)) != (ATTR_HIDDEN)) && ((currentDir.dir_attr & (ATTR_SYSTEM)) != (ATTR_SYSTEM)) && ((currentDir.dir_attr & (ATTR_VOLUME_ID)) != (ATTR_VOLUME_ID)))
			{

				// print a set number of dashes depending on the depth
				for (int j = 0; j < depth; j++)
				{
					printf("-");
				}

				// check to see if it is long name and check sum matches then print long name
				if (longNameStarted && checkSum == ChkSum(entryName))
				{
					// print long name

					// some obscure code I found online for printing unicode
					setlocale(LC_ALL, "");
					printf("Long Name Directory: ");

					// loops through all long name characters in reverse order (for structs) and forward order (within structs)
					for (int i = (numLongNameEntries - 1); i >= 0; i--)
					{
						for (int j = 0; j < 13; j++)
						{
							// verifies that we are not printing padding or null characters
							int unicodeIndex = (i * 13) + j;
							if (totalLongName[unicodeIndex] != 0x0000 && totalLongName[unicodeIndex] != 0xFFFF)
							{
								printf("%lc", (wint_t)totalLongName[unicodeIndex]);
							}
						}
					}
					printf("\n");

					// print short name

					// print a set number of dashes depending on the depth
					for (int j = 0; j < depth; j++)
					{
						printf("-");
					}

					printf("Short Name Directory: %s\n", givenName);

					// free all resources
					longNameStarted = false;
					free(totalLongName);
					totalLongName = NULL;
				}
				// otherwise if it just says long name started then free resources and print normal name
				else if (longNameStarted)
				{
					printf("Directory: %s\n", givenName);
					// free all resources
					longNameStarted = false;
					free(totalLongName);
					totalLongName = NULL;
				}
				// if no long name then just print short name
				else
				{
					printf("Directory: %s\n", givenName);
				}

				// make a new variable storing location of new directory
				uint32_t newDirectoryCluster = 0;

				// combine the bits
				newDirectoryCluster = ((uint32_t)currentDir.dir_first_cluster_hi << 16) | currentDir.dir_first_cluster_lo;
				newDirectoryCluster = newDirectoryCluster & MASK_FIRST_HEX;

				// read sub directory
				readCluster(newDirectoryCluster, depth + 1);
			}
			// check to see if it is a visible file and not a long name entry
			else if (((currentDir.dir_attr & (ATTR_LONG_NAME_MASK)) != (ATTR_LONG_NAME)) && ((currentDir.dir_attr & (ATTR_DIRECTORY)) != (ATTR_DIRECTORY)) && ((currentDir.dir_attr & (ATTR_HIDDEN)) != (ATTR_HIDDEN)) && ((currentDir.dir_attr & (ATTR_SYSTEM)) != (ATTR_SYSTEM)) && ((currentDir.dir_attr & (ATTR_VOLUME_ID)) != (ATTR_VOLUME_ID)))
			{
				// print a set number of dashes depending on the depth
				for (int j = 0; j < depth; j++)
				{
					printf("-");
				}

				// get rid of white space
				nameExtension = removeTrailingSpace(nameExtension);

				// check to see if it is long name then print name
				if (longNameStarted && checkSum == ChkSum(entryName))
				{
					// print long name

					// obscure code for printing unicode
					setlocale(LC_ALL, "");
					printf("Long Name File: ");

					// loops through all long name characters in reverse order (for structs) and forward order (within structs)
					for (int i = (numLongNameEntries - 1); i >= 0; i--)
					{
						for (int j = 0; j < 13; j++)
						{
							// verifies that we are not printing padding or null characters
							int unicodeIndex = (i * 13) + j;
							if (totalLongName[unicodeIndex] != 0x0000 && totalLongName[unicodeIndex] != 0xFFFF)
							{
								printf("%lc", (wint_t)totalLongName[unicodeIndex]);
							}
						}
					}
					printf("\n");

					// print a set number of dashes depending on the depth
					for (int j = 0; j < depth; j++)
					{
						printf("-");
					}

					// avoid printing . if it is only whitespace
					if (strlen(nameExtension) == 1 && nameExtension[0] == ' ')
					{
						printf("Short Name File: %s\n", givenName);
					}
					else
					{
						printf("Short Name File: %s.%s\n", givenName, nameExtension);
					}

					// free all resources
					longNameStarted = false;
					free(totalLongName);
					totalLongName = NULL;
				}
				// otherwise if it just says long name started then free resources and print normal name
				else if (longNameStarted)
				{
					// avoid printing . if it is only whitespace
					if (strlen(nameExtension) == 1 && nameExtension[0] == ' ')
					{
						printf("Short Name File: %s\n", givenName);
					}
					else
					{
						printf("Short Name File: %s.%s\n", givenName, nameExtension);
					}

					// free all resources
					longNameStarted = false;
					free(totalLongName);
					totalLongName = NULL;
				}
				// if no long name then just print short name
				else
				{
					// avoid printing . if it is only whitespace
					if (strlen(nameExtension) == 1 && nameExtension[0] == ' ')
					{
						printf("Short Name File: %s\n", givenName);
					}
					else
					{
						printf("Short Name File: %s.%s\n", givenName, nameExtension);
					}
				}
			}
			// check to see if it is a first entry long name
			else if (((currentDir.dir_attr & (ATTR_LONG_NAME_MASK)) == (ATTR_LONG_NAME)) && !longNameStarted)
			{
				// read the long name into a new struct for long names
				lseek(fd, (dataSectorLocationInSectors * bootSector.BPB_BytesPerSec) + ((clusterNum - 2) * bytesPerCluster) + (i * sizeof(struct LongNameDirInfo)), SEEK_SET);
				read(fd, &currentLongDir, sizeof(struct LongNameDirInfo));

				// verify that it is the last entry, otherwise don't do anything
				if ((currentLongDir.LDIR_Ord & LAST_LONG_ENTRY) == (LAST_LONG_ENTRY) && currentLongDir.LDIR_Type == 0)
				{
					// indicate that we started reading a long name
					longNameStarted = true;
					// allocate enough memory to store the entire long name
					totalLongName = malloc(260 * sizeof(uint16_t));
					// record how many chars we have written in so far
					charsAdded = 0;
					// count first entry
					numLongNameEntries = 1;

					// read in first 5 characters
					memcpy(totalLongName + (charsAdded), currentLongDir.LDIR_Name1, 10);
					charsAdded += 5;

					// read in next 6 characters
					memcpy(totalLongName + (charsAdded), currentLongDir.LDIR_Name2, 12);
					charsAdded += 6;

					// read in last 2 characters
					memcpy(totalLongName + (charsAdded), currentLongDir.LDIR_Name3, 4);
					charsAdded += 2;

					// store verification info
					checkSum = currentLongDir.LDIR_Chksum;
					previousNameOrder = currentLongDir.LDIR_Ord;
				}
			}
			// check to see if it is a second+ entry long name
			else if (((currentDir.dir_attr & (ATTR_LONG_NAME_MASK)) == (ATTR_LONG_NAME)) && longNameStarted)
			{
				// read the long name into a new struct for long names
				lseek(fd, (dataSectorLocationInSectors * bootSector.BPB_BytesPerSec) + ((clusterNum - 2) * bytesPerCluster) + (i * sizeof(struct LongNameDirInfo)), SEEK_SET);
				read(fd, &currentLongDir, sizeof(struct LongNameDirInfo));

				// verify new entry is valid
				if (checkSum == currentLongDir.LDIR_Chksum && currentLongDir.LDIR_Ord < previousNameOrder && currentLongDir.LDIR_Type == 0)
				{
					// update info
					previousNameOrder = currentLongDir.LDIR_Ord;

					// count this entry
					numLongNameEntries++;

					// add more to the array
					// read in first 5 characters
					memcpy(totalLongName + (charsAdded), currentLongDir.LDIR_Name1, 10);
					charsAdded += 5;

					// read in next 6 characters
					memcpy(totalLongName + (charsAdded), currentLongDir.LDIR_Name2, 12);
					charsAdded += 6;

					// read in last 2 characters
					memcpy(totalLongName + (charsAdded), currentLongDir.LDIR_Name3, 4);
					charsAdded += 2;
				}
				// otherwise discard all of it
				else
				{
					longNameStarted = false;
					free(totalLongName);
					totalLongName = NULL;
				}
			}
			// otherwise we just have an orphan and we deallocate all memory
			else if (longNameStarted)
			{
				longNameStarted = false;
				free(totalLongName);
				totalLongName = NULL;
			}
		}
	}

	// get next cluster number from fat
	newCluster = getNextFatValue(clusterNum);
	// clear top 4 bits
	newCluster = newCluster & MASK_FIRST_HEX;

	// check to see if it references a valid cluster or End of Chain
	if (newCluster < END_OF_CLUSTER_CHAIN)
	{
		readCluster(newCluster, depth);
	}

	// free all memory
	free(entryName);
	free(givenName);
	free(nameExtension);
	if (totalLongName != NULL)
	{
		free(totalLongName);
	}
}

/**
 * removeTrailingSpace
 *
 * Takes a string and removes all whitespaces at the end of it, then returns the same string.
 * @param char* string - string to remove whitespaces from
 * @returns char* - string with whitespaces removed
 */
char *removeTrailingSpace(char *string)
{
	char *end;

	// this variable now points to
	end = string + strlen(string) - 1;

	while (end > string && isspace((unsigned char)*end))
		end--;

	end[1] = '\0';

	return string;
}

/**
 * getNextFatValue
 *
 * Takes a FAT cluster number and returns the next fat cluster number in the chain
 * @param uint32_t currentCluster - current FAT cluster number
 * @returns uint32_t - next FAT cluster number in chain
 */
uint32_t getNextFatValue(uint32_t currentCluster)
{
	uint32_t newCluster;
	lseek(fd, fatSectorStart + ((SIZE_OF_FAT_ENTRY / BITS_PER_BYTE) * currentCluster), SEEK_SET);
	read(fd, &newCluster, SIZE_OF_FAT_ENTRY / BITS_PER_BYTE);
	return newCluster;
}

/**
 * fetchFile
 *
 * Takes a file path and attempts to find the file in memory (then call function to copy it to output directory), recursively calling itself to search a new cluster. Returns true if successfully copied.
 * @param uint32_t currentCluster - current cluster being searched
 * @param char* target - path to target file
 * @param int numLeft - number of subdirectories left before finding the file in question
 * @returns NA
 */
void fetchFile(uint32_t clusterNum, char *target, int numLeft)
{
	char *entryName;	 // name of the directory that we will print
	char *givenName;	 // short name except extension
	char *nameExtension; // extension
	char *fullName;		 // full name with . added before extension
	struct DirInfo currentDir;
	uint32_t newCluster;
	bool foundTarget = false;

	// verify target not null
	if (target == NULL)
	{
		return;
	}

	// verify target not 0
	if (strlen(target) == 0)
	{
		return;
	}

	// allocate space
	entryName = malloc(12 * sizeof(char));
	givenName = malloc(9 * sizeof(char));
	nameExtension = malloc(4 * sizeof(char));
	fullName = malloc(13 * sizeof(char));

	// loop through all entries in the cluster
	for (int i = 0; i < entriesPerCluster; i++)
	{
		// lseek to the beginning of the next entry
		// clusterNum - 2 because the first 2 slots of the FAT table (slots 0 and 1) are occupied by something else and thus do not count
		lseek(fd, (dataSectorLocationInSectors * bootSector.BPB_BytesPerSec) + ((clusterNum - 2) * bytesPerCluster) + (i * sizeof(struct DirInfo)), SEEK_SET);

		// read in the next entry
		read(fd, &currentDir, sizeof(struct DirInfo));

		// read in the name
		memcpy(entryName, currentDir.dir_name, 11);
		entryName[11] = '\0';

		// check to see whether we are at the end
		if ((uint8_t)entryName[0] == 0x00)
		{
			// if it is then there is nothing else for us to read so we just end the loop
			break;
		}
		else if ((uint8_t)entryName[0] != 0xE5)
		{

			// seperate name into extension and given name
			memcpy(givenName, &entryName[0], 8);
			givenName[8] = '\0';

			memcpy(nameExtension, &entryName[8], 3);
			nameExtension[3] = '\0';

			// get rid of blanks from normal name
			givenName = removeTrailingSpace(givenName);

			// check to see if it is a directory and that we are not looking for a file
			if (numLeft != 0 && ((currentDir.dir_attr & (ATTR_LONG_NAME_MASK)) != (ATTR_LONG_NAME)) && ((currentDir.dir_attr & (ATTR_DIRECTORY)) == (ATTR_DIRECTORY)) && ((currentDir.dir_attr & (ATTR_HIDDEN)) != (ATTR_HIDDEN)) && ((currentDir.dir_attr & (ATTR_SYSTEM)) != (ATTR_SYSTEM)) && ((currentDir.dir_attr & (ATTR_VOLUME_ID)) != (ATTR_VOLUME_ID)))
			{
				// combine the bits
				newCluster = ((uint32_t)currentDir.dir_first_cluster_hi << 16) | currentDir.dir_first_cluster_lo;
				newCluster = newCluster & MASK_FIRST_HEX;

				// check to see whether we found what we were looking for
				if (strcmp(target, givenName) == 0)
				{
					// if we did then we read the subdirectory and keep looking

					// first we get the next target to search for
					target = strtok(NULL, "/");

					// recursively call itself to try and find what it is looking for in next subdirectory
					foundTarget = true;
					fetchFile(newCluster, target, numLeft - 1);
					break;
				}
			}
			// make sure not long name and we are looking for a file
			else if (numLeft == 0 && ((currentDir.dir_attr & (ATTR_LONG_NAME_MASK)) != (ATTR_LONG_NAME)) && ((currentDir.dir_attr & (ATTR_DIRECTORY)) != (ATTR_DIRECTORY)) && ((currentDir.dir_attr & (ATTR_HIDDEN)) != (ATTR_HIDDEN)) && ((currentDir.dir_attr & (ATTR_SYSTEM)) != (ATTR_SYSTEM)) && ((currentDir.dir_attr & (ATTR_VOLUME_ID)) != (ATTR_VOLUME_ID)))
			{
				// combine the bits
				newCluster = ((uint32_t)currentDir.dir_first_cluster_hi << 16) | currentDir.dir_first_cluster_lo;
				newCluster = newCluster & MASK_FIRST_HEX;

				// assemble full name
				strcpy(fullName, "");
				strcat(fullName, givenName);
				strcat(fullName, ".");
				strcat(fullName, nameExtension);

				// check to see whether we found what we were looking for
				if (strcmp(target, fullName) == 0)
				{
					// if so, then we are done and this is our file
					foundFile = true;
					foundTarget = true;
					copyFile(&currentDir, newCluster, givenName, nameExtension);
					break;
				}
			}
		}
	}

	// free memory
	free(entryName);
	free(givenName);
	free(nameExtension);
	free(fullName);

	// get next cluster number from fat
	newCluster = getNextFatValue(clusterNum);
	// clear top 4 bits
	newCluster = newCluster & MASK_FIRST_HEX;

	// check to see if it references a valid cluster or End of Chain
	if (foundTarget != true && newCluster < END_OF_CLUSTER_CHAIN)
	{
		fetchFile(newCluster, target, numLeft);
	}
}

/**
 * copyFile
 *
 * Takes info about a file in FAT32 image and then copies it to output directory
 * @param struct DirInfo* targetDir - directory entry struct containing info about file we want to copy
 * @param uint32_t startingCluster - cluster to start copying the file bytes from
 * @param char* givenName - short name for file except extension
 * @param char* nameExtension - extension for file from short name
 * @returns void - NA
 */
void copyFile(struct DirInfo *targetDir, uint32_t startingCluster, char *givenName, char *nameExtension)
{
	uint32_t bytesLeft = targetDir->dir_file_size; // bytes left to copy from file
	FILE *fptr;									   // new file pointer
	int *clusterBytes = malloc(bytesPerCluster);   // allocate memory for copying
	char *destination = malloc(sizeof(char) * 50); // allocate memory to store new file path

	// assemble destination path
	strcpy(destination, "output/");
	strcat(destination, givenName);
	strcat(destination, ".");
	strcat(destination, nameExtension);

	// create file
	fptr = fopen(destination, "w");

	// loop until we reacj file size, reach the end of the cluster chain, or something goes wrong with our cluster chain
	while (bytesLeft != 0 && startingCluster < EOC && startingCluster != 0)
	{
		// lseek to the beginning of where we start reading
		// clusterNum - 2 because the first 2 slots of the FAT table (slots 0 and 1) are occupied by something else and thus do not count
		lseek(fd, (dataSectorLocationInSectors * bootSector.BPB_BytesPerSec) + ((startingCluster - 2) * bytesPerCluster), SEEK_SET);
		read(fd, clusterBytes, bytesPerCluster);

		// check to see if we need to copy entire cluster or only part of it
		if (bytesLeft >= bytesPerCluster)
		{
			fwrite(clusterBytes, 1, bytesPerCluster, fptr);
			bytesLeft = bytesLeft - bytesPerCluster;

			// get next cluster to copy from
			startingCluster = getNextFatValue(startingCluster);
			startingCluster = startingCluster & MASK_FIRST_HEX;
		}
		else
		{
			// copy part of cluster, no need to get fat entry since we are done and loop will end
			fwrite(clusterBytes, 1, bytesLeft, fptr);
			bytesLeft = 0;
		}
	}

	fclose(fptr);

	free(clusterBytes);
	free(destination);
}

//-----------------------------------------------------------------------------
// ChkSum()
// Returns an unsigned byte checksum computed on an unsigned byte
// array. The array must be 11 bytes long and is assumed to contain
// a name stored in the format of a MS-DOS directory entry.
// Passed: pFcbName Pointer to an unsigned byte array assumed to be
// 11 bytes long.
// Returns: Sum An 8-bit unsigned checksum of the array pointed
// to by pFcbName.
// (comment copied directly from documentation)
//------------------------------------------------------------------------------
unsigned char ChkSum(unsigned char *pFcbName)
{
	short FcbNameLen;
	unsigned char Sum;
	Sum = 0;
	for (FcbNameLen = 11; FcbNameLen != 0; FcbNameLen--)
	{
		// NOTE: The operation is an unsigned char rotate right
		Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
	}
	return (Sum);
}
