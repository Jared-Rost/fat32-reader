# FAT32 File System Reader

A command-line utility written in C that reads and parses FAT32 disk images, providing functionality to view file system information, list directory contents, and extract files.

## Overview

This project implements a FAT32 file system parser that can: 
- Read and validate FAT32 boot sectors and file system structures
- Display detailed volume information
- Recursively list all files and directories (with long filename support)
- Extract individual files from the disk image

The tool operates directly on FAT32 disk image files without mounting them, making it useful for forensics, file system analysis, and educational purposes.

## Features

### Core Functionality
- **Volume Information**: Display boot sector and file system metadata
- **Directory Listing**: Recursive directory traversal with hierarchical display
- **Long Filename Support**: Parses and displays both long and short (8.3) filenames
- **File Extraction**: Copy files from the disk image to local storage
- **FAT32 Validation**: Comprehensive validation of FAT32 structures

### Technical Implementation
- Proper handling of cluster chains via FAT traversal
- Unicode support for long filenames (VFAT)
- Checksum verification for long filename entries
- Hidden and system file filtering
- Deleted file entry detection

## Building

### Prerequisites
- GCC or compatible C compiler
- GNU Make
- Standard C library with POSIX support

### Compilation

```bash
make
```

This produces the `fat32` executable.

## Usage

### General Syntax

```bash
./fat32 <image_file> <command> [arguments]
```

### Commands

#### 1. Display Volume Information

```bash
./fat32 diskimage. img info
```

**Output includes:**
- Drive name and OEM identifier
- Free space and total capacity
- Cluster size in sectors and bytes
- Usable space calculations

**Example Output:**
```
Drive name: MYDRIVE
OEM name: MSWIN4.1
Free space is 512000 KB
Total space is 1048576 KB
Total usable space 1040000 KB
Cluster size in sectors 8
Cluster size is 4096 bytes
```

#### 2. List Directory Contents

```bash
./fat32 diskimage.img list
```

**Features:**
- Recursive listing of all files and directories
- Hierarchical display with dash prefixes (depth indicators)
- Shows both long and short names for files with VFAT entries
- Filters out hidden and system files

**Example Output:**
```
Directory:  DOCS
-Long Name File: My Document.txt
-Short Name File: MYDOCU~1.TXT
-Directory: SUBDIR
--Long Name File: Another File. docx
--Short Name File: ANOTHE~1.DOC
```

#### 3. Extract a File

```bash
./fat32 diskimage.img get <path/to/file>
```

**Important Notes:**
- Use the **short name** (8.3 format, uppercase) from the disk image
- Long filenames are **not supported** for the `get` command
- Extracted files are placed in the `output/` directory
- Do not delete the `output/` folder

**Example:**
```bash
./fat32 diskimage.img get DOCS/MYDOCU~1.TXT
```

This extracts the file to `output/MYDOCU~1.TXT`.

## FAT32 Validation

The program performs extensive validation before processing: 

1. **Info Sector Signature**: Verifies lead signature (0x41615252)
2. **Jump Boot Signature**:  Checks for valid boot jump instruction (0xEB or 0xE9)
3. **Root Cluster**:  Ensures root cluster number ≥ 2
4. **FAT Size**: Validates non-zero FAT size (BPB_FATSz32)
5. **Total Sectors**: Confirms minimum cluster count (≥ 65,525 sectors)
6. **Reserved Bytes**: Verifies reserved fields are zeroed
7. **FAT[0] Validation**: Checks low byte matches media type
8. **FAT[1] Validation**: Ensures FAT[1] contains 0x0FFFFFFF

Any validation failure will terminate the program with an error message. 

## Project Structure

```
fat32-reader/
├── fat32.c          # Main implementation
├── fat32.h          # Structure definitions and constants
├── Makefile         # Build configuration
├── output/          # Directory for extracted files (required)
└── README.md        # This file
```

## Technical Details

### File System Structures

The implementation defines several key structures: 
- `fat32BS`: Boot sector with BIOS Parameter Block (BPB)
- `fat32FSInfo`: File system information sector
- `DirInfo`: Standard 32-byte directory entry
- `LongNameDirInfo`: VFAT long filename entry

### Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SIZE_OF_FAT_ENTRY` | 32 bits | FAT32 entry size |
| `END_OF_CLUSTER_CHAIN` | 0x0FFFFFF8 | Marks end of cluster chain |
| `ATTR_LONG_NAME` | 0x0F | Long filename attribute mask |

### Algorithm Overview

1. **Boot Sector Parsing**:  Read and validate BPB at offset 0
2. **FAT Traversal**: Follow cluster chains via FAT entries
3. **Directory Parsing**: Read 32-byte entries per cluster
4. **Long Name Assembly**: Reconstruct Unicode filenames from VFAT entries
5. **File Extraction**: Follow cluster chain and copy bytes to output file

## Limitations

- **Long filename extraction**: The `get` command only works with short names (8.3 format)
- **Read-only**: Cannot modify or write to FAT32 images
- **Single-threaded**: No concurrent operations
- **FAT32 only**: Does not support FAT12, FAT16, exFAT, or other file systems
- **Basic error handling**: Limited recovery from corrupted file systems

## Implementation Notes

### Long Filename Support

Long filenames are stored across multiple directory entries in reverse order.  The implementation:
1. Detects the last entry (LDIR_Ord with 0x40 bit set)
2. Reads Unicode characters from three fields (LDIR_Name1, LDIR_Name2, LDIR_Name3)
3. Validates ordering and checksum
4. Assembles complete filename for display

### Cluster Chain Following

Files spanning multiple clusters are tracked by:
1. Reading initial cluster from directory entry (dir_first_cluster_hi: lo)
2. Following FAT entries until reaching EOC (End of Cluster Chain)
3. Reading cluster contents from data region with offset calculation

## References

- Microsoft FAT32 File System Specification
- VFAT Long Filename Specification
- FAT:  General Overview of On-Disk Format
