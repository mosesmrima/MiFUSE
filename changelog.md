# V.0.1
## 2022-03-04

## Release Highlights
- Accept zip file as root of filesystem

<br>
<br>

## Features

### Zip as Root of FS
MiFUSE v0.1 now accepts a zip file as the root of the filesystem. When you mount the filesystem, the zip file will be automatically unzipped for you.
You can then proceed to use it as normal. On umounting, the zip file will be automatically zipped for you. This feature requires you to have the zip and unzip command to be 
installed on your machine. Else, you can edit the main function and change the command to whatever file compression mechanism you prefer.
<br>

### Issues
The zip/unzip in v.0.1 introduces a potential ***race condition***. Since the zip/unzip feature is handled in a different thread from that of the filesystem, this introduces the risk of your filesystem mounting the root directory while it is still being unzipped.
This will result in the program exiting ungracefully. My dirty little ***hack*** around this was to `sleep` the parent thread for five seconds while the child process runs.
Five seconds is enough to unzip a small directory but it will take more time to unzip larger ones, therefore one has the freedom to modify the wait time by changing the parameter in the `sleep` system call.
I welcome any suggestions to better handle this, of course.
