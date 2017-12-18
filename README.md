# FATFS image creation for ESP32
A utility to create and populate FATFS image files on the host that can then
be flashed to the ESP32.

All you need to do is add it to your components directory and
run "make menuconfig" to set the required parameters (look for
FATFSIMAGE Configuration).  Once done, "fatfsimage" will be built
the next time you build your project.

When ready, you can then do "make fat" to create the image or
"make fat-flash" to flash it.

The required settings are:

#### Source directory
This is the path to the directory containing any files or other directories
you want to copy to the image.

#### Disk image size in KB
You specify the size of the disk image in KB.  Make sure this matches the
size of your partition.

#### Image name
The filename for the image file.

#### FATFS Partition offset
Specify the start of the partition.  Make sure it matches the actual
partition.

### Usage

You may also run the utility manually if you like:

```
Usage: build/fatfsimage/fatfsimage [-h] [-l <level>] <image> <KB> <paths> [<paths>]...
Create and load a FATFS disk image.

  -h, --help                display this help and exit
  -l, --log=<level>         log level (0-5, 3 is default)
  <image>                   image file name
  <KB>                      disk size in KB
  <paths>                   directories/files to load
```
