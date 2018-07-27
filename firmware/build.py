"""
Build: The birdhouse firmware builder and uploader

Usage:
    build.py <winscpprofile> (all | <devices>) [--clean] 
    build.py build [--clean]
    build.py build --help


Arguments:
    winscpprofile     Profile used to connect to server in WinSCP
    devicelist        List of devices to upload to: e.g. 2,3,4,5 | 2-6 | 4,6-8,13 | all

Options:
    -b --buildonly    Build code, but do not upload
    -c --clean        Overwrite existing built firmware image
 """

import re, os, sys
from docopt import docopt       # pip install docopt
import subprocess

sys.path[0:0] = [r"C:\dev\birdhouse\management"]
import birdhouse_utils


from thingsboard_api_tools import TbApi                             # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from config import motherShipUrl, username, password, deq_logfile   # You'll need to create this... Be sure to gitignore it!


args = docopt(__doc__)

winscp_profile = args['<winscpprofile>']
devices = args['<devices>'] if not args['all'] else "all"
clean = args['--clean']
build_only = args['build']

# clean = True
# build_only = False

tbapi = TbApi(motherShipUrl, username, password)


winscp_program_location = r"c:\Program Files (x86)\WinSCP\WinSCP.com"
arduino_build_exe_location = r"c:\Program Files (x86)\Arduino\arduino_debug.exe"


source_name = "firmware"
source_file = source_name + ".ino"
build_folder = r"C:\Temp\BirdhouseFirmwareBuildFolder"      # Where we store the firmware image we're building
remote_dir = "/sensorbot/firmware_images"                   # Where we store the firmware images on the update server


def main():

    version = extract_version(source_file)
    build_target_file = source_name + "_" + str(version) + ".bin"

    # Important: Use the .com version here, not the .exe
    build_target = os.path.join(build_folder, build_target_file)

    prepare(build_target)

    build(source_file, build_target, build_target_file)

    text = input("Proceed with upload to devices << " + devices + " >>? [Y/N]")
    if text.lower() != "y":
        exit()

    upload(source_file, build_target_file, build_target, remote_dir, devices)


def prepare(build_target):
    # Verify we know where WinSCP is installed
    if not os.path.exists(winscp_program_location):
        print("Could not find WinScp -- is it installed?  If so, check the location and modify this file, changing winscp_program_location accordingly.")
        exit()

    if not os.path.isdir(build_folder):
        print("Could not find build folder " + build_folder)
        exit()      # Should we just create it??

    if os.path.exists(build_target):
        if clean:
            print("Deleting " + build_target)
            os.remove(build_target)
            if os.path.exists(build_target):
                print("Could not delete " + build_target + "... aborting.")
                exit()
        else:
            print("Target " + build_target + " already exists! (use --clean to overwrite)")
            exit()


# Use the Arduino IDE to build our sketch
def build(source_file, build_target, build_target_file):
    print("Building " + build_target)

    output = execute([arduino_build_exe_location, "--verify", "--pref", "build.path=" + build_folder, source_file])

    if binary_too_big(output):
        print("Too much space used -- device will not be able to recieve OTA updates if this firmware is uploaded!")
        exit()


    built_file = source_file + ".bin"

    if build_only:
        print("Successfully built " + built_file + " (but did not rename it)")
        exit()

    # Rename build file
    os.rename(os.path.join(build_folder, built_file), os.path.join(build_folder, build_target_file))


''' Ensure our binary doesn't grow too large for OTA updates '''
def binary_too_big(output):
    for output_line in output:
        # Sketch uses 318064 bytes (30%) of program storage space. Maximum is 1044464 bytes.
        search = re.search('^Sketch uses \d+ bytes \((\d+)%\) of program storage space. Maximum', output_line)
        if search:
            storage_used = search.group(1)
        print(output_line, end="")

    return int(storage_used) > 48     # Why 48?  Why not?


def upload(source_file, source_file_with_version, build_target, remote_dir, devices):

    if devices == "all":
        print("Uploading " + build_target + " for all devices")
        for output_line in execute('"' + winscp_program_location + '" "' + winscp_profile + '" /command "cd ' + remote_dir + '" "put ' + build_target + '" "exit"'):
            print(output_line, end="")
        return
        
    print("Uploading " + build_target + " for devices: " + devices)

    first = True

    device_list = devices.replace(" ", "").split(",")

    prepared_device_list = []
    # Expand any ranges (1-3 to 1,2,3)
    for num in device_list:
        if "-" in num:
            low,high = num.split("-")
            nums = list(range(int(low), int(high) + 1))   # + 1 to ensure we include the high value, which would normally get omitted because of list index semantics

            prepared_device_list.extend(nums)
        else:
            prepared_device_list.append(int(num))


    for num in list(set(prepared_device_list)):
        mac_address = None
        device_name = birdhouse_utils.make_device_name(num)
        device = tbapi.get_device_by_name(device_name)
        attrs = tbapi.get_client_attributes(device)
        for attr in attrs:
            if attr["key"] == "macAddress":
                mac_address = attr["value"]
                break

        if mac_address is None:
            print("Couldn't find MAC address for device " + device_name)
            exit()

        print("Uploading " + build_target + " for device " + device_name + " (MAC addr " + mac_address + ")")

        sanitized_device_name = device_name.replace(" ", "_")   # Strip spaces; makes life easier
        device_specific_remote_dir = remote_dir + "/" + sanitized_device_name + "_" + mac_address
        mkdir_command = 'call mkdir -p ' + device_specific_remote_dir

        if first:       # Upload a new file
            remote_source_dir = device_specific_remote_dir

            for output_line in execute('"' + winscp_program_location + '" "' + winscp_profile + '" /command "' + mkdir_command + '" "cd ' + device_specific_remote_dir + '" "put ' + build_target + '" "exit"'):
                print(output_line, end="")

            first = False

        else:           # Copy file we just uploaded, to reduce bandwidth and go faster
            cp_command    = 'call cp ' + remote_source_dir + '/' + source_file_with_version + ' ' + device_specific_remote_dir + '/'

            for output_line in execute('"' + winscp_program_location + '" "' + winscp_profile + '" /command "' + mkdir_command + '" "' + cp_command + '" "exit"'):
                print(output_line, end="")


def extract_version(source_file):
    # Extract our version from the source
    version = None
    with open(source_file, 'r') as file:
        for line in file:
            # Looking for: #define FIRMWARE_VERSION "0.133" // ...

            search = re.search('^#define +FIRMWARE_VERSION +"(\d+\.\d+)"', line)
            if search:
                version = search.group(1)
                break

    if not version:
        print("Could not find FIRMWARE_VERSION definition in", source_file)
        exit()

    return version


# From https://stackoverflow.com/questions/4417546/constantly-print-subprocess-output-while-process-is-running
def execute(cmd):
    popen = subprocess.Popen(cmd, stdout=subprocess.PIPE, universal_newlines=True)
    for stdout_line in iter(popen.stdout.readline, ""):
        yield stdout_line 
    popen.stdout.close()
    return_code = popen.wait()

    if return_code:
        raise subprocess.CalledProcessError(return_code, cmd)

    # return return_code


main()