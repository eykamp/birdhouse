import birdhouse_utils
import subprocess
import re
import sys
import os
import serial
import time
import json

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi


from provision_config import motherShipUrl, username, password, dashboard_template_name, sensor_type, default_device_local_password, wifi_ssid, wifi_password

firmware_folder_name = r"C:\Temp\BirdhouseFirmwareBuildFolder"



# This to be passed in
birdhouse_number = 6
birdhouse_name = birdhouse_utils.make_device_name(birdhouse_number)


cust_info = {}

cust_info["name"]     = birdhouse_name
cust_info["address"]  = "2103 SE Tibbetts"
cust_info["address2"] = ""
cust_info["city"]     = "Portland"
cust_info["state"]    = "OR"
cust_info["zip"]      = None     # Will be populated by geocoder if empty
cust_info["country"]  = "USA"
cust_info["lat"]      = None     # Will be populated by geocoder if empty
cust_info["lon"]      = None     # Will be populated by geocoder if empty
cust_info["email"]    = "chris@sensorbot.org"
cust_info["phone"]    = "555-1212"

led_style = 'RYG'   #  RYG, RYG_REVERSED, DOTSTAR, 4PIN, UNKNOWN

tbapi = TbApi(motherShipUrl, username, password)


def main():
    # for port in birdhouse_utils.get_ports():
    #     print( port.hwid())

    port = birdhouse_utils.get_best_guess_port()

    if port is None:
        print("Could not find a port with a birdhouse on it.  Here is a list of all hwids I could find:")
        print(birdhouse_utils.get_port_hwids())

        exit


    cleanup = False      # If true, deletes everything that is created


    # Do we already have a profile for this birdhouse?
    device = tbapi.get_device_by_name(birdhouse_name)
    if device is None:
        device_token = create_device(cleanup)
    else:
        device_token = tbapi.get_device_token(device)

    print("Using device token " + device_token)

    print("Uploading firmware to birdhouse on " + port + "...")
    upload_firmware(port)

    time.sleep(2)

    set_params(port, device_token)


    # assign_device_to_public_user(token, device_id)

    # userdata = get_users(token)
    # print(userdata)

    # print(get_customer(token, 'Art'))
    # print(get_public_user_uuid(token))
    # delete_customer_by_name(token, 'Hank Williams 3')


def create_customer():
    # Lookup missing fields, such as zip, lat, and lon
    birdhouse_utils.update_customer_data(cust_info)

    if cust_info["lat"] is None or cust_info["lon"] is None:
        print("Must have valid lat/lon in order to add device!")
        return None

    # Create new customer and device records on the server
    return tbapi.add_customer(cust_info["name"], cust_info["address"], cust_info["address2"], cust_info["city"], cust_info["state"], cust_info["zip"], cust_info["country"], cust_info["email"], cust_info["phone"])


    
def create_device(cleanup):

    # Get a definition of our template dashboard
    template_dash = tbapi.get_dashboard_by_name(dashboard_template_name)
    if template_dash is None:
         print("Cannot retrieve template dash %s.  Is that the correct name?" % dashboard_template_name)
         sys.exit()

    dash_def = tbapi.get_dashboard_definition(tbapi.get_id(template_dash))

   
    customer = create_customer()
    if customer is None:
         exit()

    server_attributes = {
        "latitude": cust_info["lat"],
        "longitude": cust_info["lon"],
        "address": birdhouse_utils.one_line_address(cust_info)
    }

    shared_attributes = {
         "LED": "Unknown"
    }

    device = tbapi.add_device(birdhouse_utils.make_device_name(birdhouse_number), sensor_type, shared_attributes, server_attributes)
    device_id = tbapi.get_id(device)


    # We need to store the device token as a server attribute so our REST services can get access to it
    device_token = tbapi.get_device_token(device_id)

    server_attributes = {
         "device_token": device_token
    }

    tbapi.set_server_attributes(device_id, server_attributes)

    try:
        # Upate the dash def. to point at the device we just created (modifies dash_def)
        update_dash_def(dash_def, cust_info["name"], device_id)
    except Exception as ex:
        print("Exception encountered: Cleaning up...")
        tbapi.delete_device(device_id)
        tbapi.delete_customer_by_id(tbapi.get_id(customer))
        raise ex


    # Create a new dash with our definition, and assign it to the new customer    
    dash = tbapi.create_dashboard_for_customer(cust_info["name"] + ' Dash', dash_def)
    tbapi.assign_dash_to_user(tbapi.get_id(dash), tbapi.get_id(customer))

    print("Device token for " + birdhouse_name + " (set device token: setparams?deviceToken=" + device_token + ")")

    if cleanup:
        # input("Press Enter to continue...")   # Don't run from Sublime with this line enabled!!!

        print("Cleaning up! (device token rendered invalid)")
        tbapi.delete_dashboard(tbapi.get_id(dash))
        tbapi.delete_device(device_id)
        tbapi.delete_customer_by_id(tbapi.get_id(customer))

    return device_token


def upload_firmware(serial_port):

    image_name = find_firmware(firmware_folder_name)

    if image_name is None:
        print("Could not find firmware in folder " + firmware_folder_name)
        exit()

    cmd = birdhouse_utils.ESPTOOL_EXE_LOCATION + r' -vv -cd nodemcu -cb 115200 -cp ' + serial_port + r' -ca 0x00000 -cf ' + image_name

    try:
        output = subprocess.check_output(cmd, stderr=subprocess.STDOUT, shell=True, universal_newlines=True)
        success = True
    except subprocess.CalledProcessError as ex:
        output = ex.output
        success = False

        print("Failed to upload firmware: " + output)
        exit()




def set_params(port, device_token):
    defaults = {
        'temperatureCalibrationFactor' : (1,                             '["calibrationFactors"]["temperatureCalibrationFactor"]'),
        'temperatureCalibrationOffset' : (0,                             '["calibrationFactors"]["temperatureCalibrationOffset"]'),
        'humidityCalibrationFactor' :    (1,                             '["calibrationFactors"]["humidityCalibrationFactor"]'),
        'humidityCalibrationOffset' :    (0,                             '["calibrationFactors"]["humidityCalibrationOffset"]'),
        'pressureCalibrationFactor' :    (1,                             '["calibrationFactors"]["pressureCalibrationFactor"]'),
        'pressureCalibrationOffset' :    (0,                             '["calibrationFactors"]["pressureCalibrationOffset"]'),
        'pm10CalibrationFactor' :        (1,                             '["calibrationFactors"]["pm10CalibrationFactor"]'),
        'pm10CalibrationOffset' :        (0,                             '["calibrationFactors"]["pm10CalibrationOffset"]'),
        'pm25CalibrationFactor' :        (1,                             '["calibrationFactors"]["pm25CalibrationFactor"]'),
        'pm25CalibrationOffset' :        (0,                             '["calibrationFactors"]["pm25CalibrationOffset"]'),
        'pm1CalibrationFactor' :         (1,                             '["calibrationFactors"]["pm1CalibrationFactor"]'),
        'pm1CalibrationOffset' :         (0,                             '["calibrationFactors"]["pm1CalibrationOffset"]'),
        'sampleDurations' :              (30,                            '["sampleDurations"]'),
        'mqttPort' :                     (1883,                          '["mqttPort"]'),
        'mqttUrl' :                      ('www.sensorbot.org',           '["mqttUrl"]'),
        'ledStyle' :                     (led_style,                     '["ledStyle"]'),
        'deviceToken' :                  (device_token,                  '["deviceToken"]'),
        'localSsid' :                    (birdhouse_name,                '["localSsid"]'),
        'localPass' :                    (default_device_local_password, '["localPass"]'),
        'wifiSsid' :                     (wifi_ssid,                     '["wifiSsid"]'),
        'wifiPass' :                     (wifi_password,                 '["wifiPass"]'),
        'serialNumber' :                 (birdhouse_number,              '["serialNumber"]'),
    }

    batch = 0

    items = list(defaults.items())      # Coalesce this to a list to make batching easier and ensure a consistent order

    batch_size = 5                      # Do this in batches because if we do too many, they values don't stick.  Likely a bug in aREST.

    bhserial = serial.Serial(port, 115200, timeout=5)
    if bhserial is None:
        print("Could not open serial port " + port)
        exit()

    print("Flushing")
    bhserial.write(bytes('\r\n'.encode('UTF-8')))       # Send blank line so we get something back
    time.sleep(0.1)
    print(bhserial.readlines())

    while batch * batch_size < len(defaults):
        cmd = '/setparams?'
        first = True

        range_start = batch * batch_size
        range_end   = range_start + batch_size
            
        for item in items[range_start : range_end]:
            print(item)
            field, val = item[0], item[1][0]

            if val is None:       # Read-only field; nothing to send back to the server
                continue

            if not first:
                cmd += '&'
            cmd += field + '=' + str(val)
            first = False

        if first:       # This will be true if we added no items to our list because, for example, they all had values of None
            continue

        cmd += '\r\n'
        print(cmd)

        tries = 3
        done = False

        while tries > 0 and not done:
            bhserial.write(bytes(cmd.encode('UTF-8')))
            time.sleep(0.1)
            line = bhserial.readline().decode()
            

            if line == '':
                tries -= 1
                continue

            done = True
        
        if not done:
            print("It appears the birdhouse isn't accepting commands.  Try again?")
            exit()

        batch += 1

    # Verify what we wrote stuck
    bhserial.write(bytes('\r\n'.encode('UTF-8')))       # Send blank line to get back full variable list
    time.sleep(0.1)

    line = bhserial.readline() #.replace(r'\xff', '*').replace(r'\n', '').replace(r'\r', '')
    print("verify line:" , line)
    vars = json.loads(line.decode('Latin-1'))["variables"]   # Latin-1 handles high-order characters properly, which we sometimes get when there's garbage in the EEPROM

    print(vars)

    for item in items:
        field, val, path = item[0], item[1][0], item[1][1]
        var = eval("vars" + path)
        print(field, val, path, var)

        if var != val:
            print("Value didn't stick: " + field + " should have been " + str(val) + ", but was " + var)
            exit()



# TODO: Variant of this fn in redlight_greenlight
def find_firmware(folder, current_version=None):
    
    newest_firmware = None

    if current_version is None:
        newest_major = 0
        newest_minor = 0
    else:
        v = re.search("(\d+)\.(\d+)", current_version)
        newest_major = int(v.group(1))
        newest_minor = int(v.group(2))


    for file in os.listdir(folder):
        candidate = re.search("(\d+)\.(\d+).bin", file)
        if candidate:
            c_major = int(candidate.group(1))
            c_minor = int(candidate.group(2))

            if c_major > newest_major or c_minor > newest_minor:
                newest_major = c_major
                newest_minor = c_minor
                newest_firmware = os.path.join(folder, file)

    return newest_firmware






'''
''
''
''
'''

''' Modifies dash_def '''
def update_dash_def(dash_def, customer_name, device_id):
    aliases = dash_def["configuration"]["entityAliases"].keys()
    for a in aliases:
        try:
            dash_def["configuration"]["entityAliases"][a]["alias"] = birdhouse_utils.make_device_name(birdhouse_number)
            if "singleEntity" in dash_def["configuration"]["entityAliases"][a]["filter"]:
                dash_def["configuration"]["entityAliases"][a]["filter"]["singleEntity"]["id"] = device_id
        except Exception as e:
            print('Alias: %s\n dash_def["configuration"]["entityAliases"][a]["filter"]: %s' % (a, dash_def["configuration"]["entityAliases"][a]["filter"]))
            raise e

main()        