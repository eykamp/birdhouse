import fileinput
import re

file = "firmware.ino"

# Does a list of files, and
# redirects STDOUT to the file in question
for line in fileinput.input(file, inplace = 1): 
    version = re.search('^#define FIRMWARE_VERSION "(\d+)\.(\d+)"', line)


    if version:
        major = version.group(1)
        minor = version.group(2)
        new_major = str(int(major))
        new_minor = str(int(minor) + 1)

        line = line.replace(major, new_major, 1).replace(minor, new_minor, 1)

    print(line, end='')
    