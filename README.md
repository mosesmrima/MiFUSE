# MiFUSE

MiFUSE is a minimal filesystem in user space built in C for hobbyists and minimalists.
MiFUSE is part of my project for ALX Software engeeniring program.

## Supported Platforms

* Linux(fully)
* BSD

## Compiling
Before you compile, make sure you have fuse 3 installed in your system.
To compile run:


    `make`


MiFUSE is not ment to be installed.
Alternativley you can compile directly with gcc using the flags:


        `pkg-config fuse3 --cflags --libs`


## Mounting a MiFUSE filesystem

You can mount MiFUSE by simply running the command `mifuse` and providing the root and mount of your filesystem.


`
./mifuse rootdir mountdir
`

You can provide ather built in fuse ptions if you want,
for example if you want to monitor fileoperations in realtime you can provide
the _-f_ option:

`
./mifuse -f rootdir mountdir
`

You can then traverse to _mountdir_ where you can perform fileperations.
The _rootdir_ and _mountdir_ should both be existing directories.
### Order of Commandline Arguments
Not that all fuse built in options come first, then the root of the filesystem and lastly the mount point.
MiFUSE does not surpport the chaining of options.

## Unmounting

To unmount the filesystem, you can run the command:<br>

` fusermount3 -u mountdir`
