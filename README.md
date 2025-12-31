How to compile: run make

How to run:

./fat32 imagename info

./fat32 imagename list

./fat32 imagename get path/to/file.txt

Note on list command: long name support is included, for files with long names it will print both the long and short name

Note on get command:
-it will put the retrieved file into the output folder, do not delete this folder
-it uses the short name as stored in the FAT32 image (so all caps), long names do not work with get
