"""
provision.py: The birdhouse and bottlebot provisioning script

Usage:
        provision.py [<number> --token TOKEN --ledstyle LEDSTYLE --wifissid SSID --wifipass PASSWORD --devicepass PASSWORD]
        provision.py upload <number> --token TOKEN --ledstyle LEDSTYLE --wifissid SSID --wifipass PASSWORD --devicepass PASSWORD
        provision.py create <number> --addr ADDRESS [--addr2 ADDRESS2] --city CITY --state STATE [--country COUNTRY] [--zip ZIP] --email EMAIL --phone PHONE [--lat LAT --lon LON] [--baseurl URL] [--testmode]
        provision.py create upload <number> --addr ADDRESS [--addr2 ADDRESS2] --city CITY --state STATE [--country COUNTRY] [--zip ZIP] --email EMAIL --phone PHONE [--lat LAT --lon LON] --ledstyle LEDSTYLE --wifissid SSID --wifipass PASSWORD --devicepass PASSWORD [--baseurl URL]
        provision.py delete_from_server <number>
        provision.py --help

        provision.py create upload number ledstyle address address2 city state country zip email phone lat lon <number> <ledstyle> <wifissid> <wifipass> <devicepass>

Commands:
    upload
    create
    delete_from_server

Options:
    -s --serveronly          Only configure Thingsboard account for this device
    -u --uploadonly          Only upload firmware
    --token TOKEN            Optional Thingsboard access token; if not provided, will attempt lookup (requires Thinigsboard credentials)
    --ledstyle STYLE         "4PIN", "BUILTIN_ONLY", "RYG", "RYG_REVERSED", "DOTSTAR", "UNKNOWN"
    --testmode               Delete device and related objects from the server
    --wifissid SSID          SSID of local wifi you will be connecting to (SSIDs are case sensitive!)
    --wifipass PASSWORD      Password of local wifi you will be connecting to
    --baseurl URL            Base URL of Sensorbot Thingsboard installation (do not include http:// prefix!) (e.g. 'www.sensorbot.org')
    --devicepass PASSWORD    Password for local access to device wifi hotspot

Server options:
    --addr ADDRESS           Address where device will be deployed
    --addr2 ADDRESS2         Address (2nd line)
    --city CITY              City where device will be deployed
    --state STATE            State where device will be deployed
    --zip ZIP                Postcode where device will be deployed (if not provided, will be calculated from address)
    --country COUNTRY        Country where device will be deployed [default: United States]
    --email EMAIL            Email address of host
    --phone PHONE            Phone number of host
    --lat LAT                Deployment latitude (if not provided, will be calculated from address)
    --lon LON                Deployment longitude (if not provided, will be calculated from address)
 """

import json
import os
import re
import subprocess
import requests                             # pip install requests
import tempfile
import time
import esptool                              # pip install esptool
from docopt import docopt                   # pip install docopt
from types import SimpleNamespace
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from asciimatics.screen     import Screen   # pip install aciimatics
from asciimatics.scene      import Scene
from asciimatics.exceptions import ResizeScreenError, NextScene, StopApplication
from asciimatics.widgets    import Frame, Layout, Divider, Text, Button, Label, RadioButtons  # , TextBox, Widget, ListBox

from functools import partial
from timeit import default_timer as timer

import birdhouse_utils

# We don't want to fail in the absence of a config file -- most users won't actually need one
try:
    import config
except ModuleNotFoundError:
    config = {}

# pip install pyserial geopy requests docopt esptool aciimatics
# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# Requirements: Python 3.7
# optional config file

__version__ = "0.9"

#################################################
# Get our arguments/configuration sorted out
args = docopt(__doc__)

# print(str(args))
# exit()


settings = SimpleNamespace(**{
    'birdhouse_number': None, 'led_style': None, 'port': None, 'device_token': None,
    'dashboard_template_name': None, 'local_pass': None, 'wifi_ssid': None, 'wifi_pass': None,
})


settings.birdhouse_number = args['<number>']
settings.led_style        = args['--ledstyle']
settings.device_token     = args['--token']
settings.wifi_ssid        = args['--wifissid']   or config.wifi_ssid       if 'wifi_ssid'       in dir(config) else None
settings.wifi_pass        = args['--wifipass']   or config.wifi_password   if 'wifi_password'   in dir(config) else None
settings.local_pass       = args['--devicepass'] or config.device_password if 'device_password' in dir(config) else None
base_url                  = args['--baseurl']    or config.base_url        if 'base_url'        in dir(config) else "www.sensorbot.org"

thingsboard_username = config.thingsboard_username if 'thingsboard_username' in dir(config) else None
thingsboard_password = config.thingsboard_password if 'thingsboard_password' in dir(config) else None

settings.dashboard_template_name = config.dashboard_template_name if 'dashboard_template_name' in dir(config) else None

cust_info = {}
cust_info["address"]  = args["--addr"]
cust_info["address2"] = args["--addr2"]
cust_info["city"]     = args["--city"]
cust_info["state"]    = args["--state"]
cust_info["country"]  = args["--country"]
cust_info["zip"]      = args["--zip"]      # Will be populated by geocoder if empty
cust_info["lat"]      = args["--lat"]      # Will be populated by geocoder if empty
cust_info["lon"]      = args["--lon"]      # Will be populated by geocoder if empty
cust_info["email"]    = args["--email"]
cust_info["phone"]    = args["--phone"]

# These are our basic commands, and define which "mode" we're in
thingsboard_only   = args["create"] and not args['upload']
upload_only        = args['upload'] and not args['create']
should_delete_only = args['delete_from_server']
ui_mode            = not args['upload'] and not args['create'] and not args['delete_from_server']

testmode           = args['--testmode']

if thingsboard_only and settings.device_token is not None:
    print("Can't pass a token if we're only creating the server objects!")
    exit()

#################################################

base_url           = birdhouse_utils.get_base_url(args)
mothership_url     = birdhouse_utils.make_mothership_url(args)
firmware_url       = "http://" + base_url + ":8989/firmware"
key_validation_url = "http://" + base_url + ":8989/validatekey"


led_styles = [("Multicolor LED with 4 legs (typical Birdhouse)", "4PIN"), ("ESP8266 built-in LED (typical Bottlebot)", "BUILTIN_ONLY"), ("Separate R, Y, G LEDs", "RYG"),
              ("Separate R, Y, G LEDs, reversed polarity", "RYG_REVERSED"), ("DotStar multicolor LED", "DOTSTAR"), ("Unknown (no LED functionality)", "UNKNOWN")]  # These are all the valid values
token_validation_pattern = "^[a-zA-Z0-9]{20}$"

# For readability?
should_set_up_thingsboard = not upload_only
should_upload_firmware = not thingsboard_only and not should_delete_only
should_create_thingsboard_objects = should_set_up_thingsboard and not should_delete_only and not ui_mode

# Just to further clarify:
if ui_mode:
    assert should_upload_firmware


def main(settings):
    print("Sensorbot Device Provisioning Script Version " + __version__)

    validate_led_style(settings.led_style)

    # Instantiate our API helper if needed
    if should_set_up_thingsboard:
        tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)

    if should_delete_only:
        birdhouse_utils.purge_server_objects_for_device(tbapi, settings.birdhouse_number)
        exit()


    device_name = birdhouse_utils.make_device_name(settings.birdhouse_number)
    if settings.birdhouse_number is not None:
        print("Configuring device '" + device_name + "'\n")

    # Get the port first to ensure our local house is in order before we validate things over the network
    validated_token = False
    if should_upload_firmware:
        if settings.birdhouse_number is not None and settings.device_token is not None:
            if not validate_device_token(settings.birdhouse_number, settings.device_token):
                print("Passed an invalid device token for the specified device.")
                print("Aborting.")
                exit()
            validated_token = True

        settings.esp = find_birdhouse_port()
        settings.port = settings.esp._port.portstr

        bhserial = settings.esp._port
        if bhserial is None:
            print("Could not establish serial connection to device on port " + settings.port)
            exit()

    if should_create_thingsboard_objects:
        settings.device_token = create_server_objects(tbapi, settings.birdhouse_number)         # This will fail if server objects already exist

    if testmode:
        input("Press Enter to cleanup created objects...")
        settings.birdhouse_utils.purge_server_objects_for_device(tbapi, settings.birdhouse_number)
        exit()

    # assign_device_to_public_user(token, device_id)

    if ui_mode:
        start_ui(tbapi, bhserial)
        exit()

    if not validated_token:
        settings.device_token = get_or_validate_device_token(tbapi, settings.birdhouse_number, settings.device_token, should_create_thingsboard_objects)

    if should_upload_firmware:
        upload_firmware_and_configure(bhserial, settings)


def upload_firmware_and_configure(bhserial, settings):
    # birdhouse_utils.hard_reset(settings.esp)
    # time.sleep(1)
    upload_firmware(settings.esp)

    time.sleep(2)

    set_params(bhserial, settings)


def find_birdhouse_port():
    # If we are uploading firmware, we need to figure out which port the device is on.
    # If we can't find the right port, we'll abort.
    print("Scanning ports for a connected birdhouse...", end='')

    try:
        esp = birdhouse_utils.get_best_guess_port()
    except esptool.FatalError:
        print(" error")
        return None

    if esp is None:
        print(" none found")
        print("Could not find a USB port with a birdhouse on it.\n\t* Is your device plugged in?\n\t* Is another program using the port?")
        # list_hwids(port)   <-- Not using hwids anymore!
        exit()

    print("Using " + esp._port.portstr)

    return esp


def get_or_validate_device_token(tbapi, birdhouse_number, token_to_validate, create_thingsboard_objects):
    # Ensure we have a valid device token, unless we don't know our birdhouse_number yet, in which case there's nothing we can do
    if birdhouse_number is None:
        return None

    device_name = birdhouse_utils.make_device_name(birdhouse_number)

    if token_to_validate is None:
        # Does this device already exist?
        device = tbapi.get_device_by_name(device_name)

        # Does device exist?
        if device is not None:
            print("Fetching device token...", end='')
            device_token = tbapi.get_device_token(device)
            print(" ok")
            return device_token

        # Device does not exist... should we create it?
        if create_thingsboard_objects:
            return create_server_objects(tbapi, birdhouse_number)   # Create the device and return its token; will fail if server objects exist

        return None

    else:
        print("Validating device token...", end='')
        if not validate_device_token(birdhouse_number, token_to_validate):
            print(" failed\nDevice token appears invalid for specified device. Please check the number and dial again.")
            exit()
        print(" ok")
        return token_to_validate


def validate_device_token(birdhouse_number, token):
    if birdhouse_number is None or token is None:
        return False

    device_name = birdhouse_utils.make_device_name(birdhouse_number)

    # Return whether the device token is valid
    url = key_validation_url + "?name=" + device_name + "&key=" + token

    tries = 3

    while tries > 0:
        resp = requests.get(url)

        # Server should always return 200 regardless of whether the token is valid or not.
        if resp.status_code == 200:
            break

        print(resp)

        tries -= 1

    if tries == 0:
        print("\nError validating device token; server returned code " + str(resp.status_code))
        print("Aborting!")
        exit()

    if resp.text == 'bad_device':
        print("\nThe device number you specified is unknown or invalid.")
        print("Aborting!")
        exit()


    return resp.text == 'true'


def create_server_objects(tbapi, birdhouse_number):
    ''' Returns secret token of created device '''
    if settings.dashboard_template_name is None:
        print("Please ensure you have a local file called config.py that defines the following variables:")
        print("\tdashboard_template_name:     The name of an existing dashboard to be used as a template for new devices")
        exit()

    device_name, cust_name, dash_name = birdhouse_utils.get_server_object_names_for_birdhouse(birdhouse_number)

    print("Creating server objects for '" + device_name + "'...", end='')

    if (tbapi.get_customer_by_name(cust_name) is not None or tbapi.get_device_by_name(device_name) is not None or tbapi.get_dashboard_by_name(dash_name) is not None):
        print(" error")
        print("Objects already exist for device '" + device_name + "'")
        print("Aborting!")
        exit()

    # Get a definition of our template dashboard, returns device_token for the created device
    template_dash = tbapi.get_dashboard_by_name(settings.dashboard_template_name)
    if template_dash is None:
        print(" error")
        print("Cannot retrieve template dashboard '%s'.  Is that the correct name?" % settings.dashboard_template_name)
        exit()

    dash_def = tbapi.get_dashboard_definition(template_dash)

    customer = create_customer(tbapi, cust_name, cust_info)

    server_attributes = {
        "latitude": cust_info["lat"],
        "longitude": cust_info["lon"],
        "address": birdhouse_utils.one_line_address(cust_info)
    }

    shared_attributes = None  # ??? Not sure

    sensor_type = birdhouse_utils.get_sensor_type(birdhouse_number)[0]
    device = tbapi.add_device(device_name, sensor_type, shared_attributes, server_attributes)
    device_id = tbapi.get_id(device)

    # We need to store the device token as a server attribute so our REST services can get access to it
    device_token = tbapi.get_device_token(device_id)

    server_attributes = {
        "device_token": device_token
    }

    tbapi.set_server_attributes(device_id, server_attributes)

    try:
        # Upate the dash def. to point at the device we just created (modifies dash_def)
        update_dash_def(dash_def, device_id)
    except Exception as ex:
        print(" error")
        print("Exception encountered: Cleaning up...")
        birdhouse_utils.purge_server_objects_for_device(tbapi, birdhouse_number)
        raise ex

    # Create a new dash with our definition, and assign it to the new customer
    dash = tbapi.create_dashboard_for_customer(dash_name, dash_def)
    tbapi.assign_dash_to_user(dash, customer)

    print(" ok")
    print("Device token for " + device_name + " (set device token: setparams?deviceToken=" + device_token + ")")

    return device_token


def create_customer(tbapi, cust_name, cust_info):
    birdhouse_utils.update_customer_data(cust_info)     # Lookup missing fields, such as zip, lat, and lon

    if cust_info["lat"] is None or cust_info["lon"] is None:
        print("Must have valid lat/lon in order to add device!")
        exit()

    # Create new customer and device records on the server
    return tbapi.add_customer(cust_name, cust_info["address"], cust_info["address2"], cust_info["city"], cust_info["state"],
                              cust_info["zip"], cust_info["country"], cust_info["email"], cust_info["phone"])


def upload_firmware(esp):
    serial_port = esp._port.portstr
    try:
        firmware = fetch_firmware()

        try:
            start = timer()

            print("Uploading firmware to device on " + serial_port + "...")

            # esptool.write_flash(esp, ('0x00000', firmware))   <=== what are args?

            # run(["python", "-u", "esptool.py", '--port', serial_port, 'write_flash', '0x00000', firmware])
            # print("ret " + str(ret))

            elapsed = timer() - start   # in seconds

            if elapsed < 3:
                print("The firmware upload process went suspcisiously fast, meaning something went wrong.  It ran in %0.1f seconds.\nAborting!" % elapsed)
                exit()

            print("Upload complete")
        except subprocess.CalledProcessError as ex:
            output = ex.output

            # Check for specific known failure modes
            search = re.search(r"error: Failed to open (COM\d+)", output)
            print("Failed to upload firmware:\n" + output)

            if search:
                port = search.group(1)
                print()
                print("Could not open " + port + ": it appears to be in use by another process!")

            exit()

    finally:
        try:
            folder.cleanup()     # Cleanup, cleanup
        except Exception as ex:
            print(f"Could not clean up folder {folder.name}: {ex}.")


def run(cmd):
    subprocess.run(cmd, shell=True, stderr=subprocess.PIPE)


def validate_led_style(led_style):
    styles = [i[1] for i in led_styles]
    if led_style not in styles and led_style is not None:
        print("'" + led_style + "' is not a valid value for ledstyle parameter.  Please specify one of the following:")
        print("    " + ", ".join(styles))
        exit()


# The keys in the following dict are the names of data entry and display fields on our form;
# The values are tuples with:
#   First: paths for extracting the desired value from the JSON string returned by the birdhouse
#   Second: params for writing data back to the birdhouse (None means the element is not writable)
#   Third: The associated command line override, if any
parse_list = {
        'birdhouse_number'       : ('["serialNumber"]',                               'serialNumber',           'number'),
        'uptime'                 : ('["uptime"]',                                     None,                     None),
        'wifi_status'            : ('["wifiStatus"]',                                 None,                     None),
        'led_style'              : ('["ledStyle"]',                                  'ledStyle',                'ledstyle'),
        'plantowerSensorDetected': ('["sensorsDetected"]["plantowerSensorDetected"]', None,                     None),
        'temperatureSensor'      : ('["sensorsDetected"]["temperatureSensor"]',       None,                     None),
        'mqtt_status'            : ('["mqttStatus"]',                                 None,                     None),
        'device_token'           : ('["deviceToken"]',                                'deviceToken',            'token'),
        'local_ssid'             : ('["localSsid"]',                                  'localSsid',              'localssid'),
        'local_pass'             : ('["localPass"]',                                  'localPass',              'localpass'),
        'wifi_ssid'              : ('["wifiSsid"]',                                   'wifiSsid',               'wifissid'),
        'wifi_pass'              : ('["wifiPass"]',                                   'wifiPass',               'wifipass'),
        'mqtt_url'               : ('["mqttUrl"]',                                    'mqttUrl',                'mqtturl'),
        'mqtt_port'              : ('["mqttPort"]',                                   'mqttPort',               'mqttport'),
    }


def get_default_values(settings):
    device_name = birdhouse_utils.make_device_name(settings.birdhouse_number)
    return {
        'temperatureCalibrationFactor': (1,                          '["calibrationFactors"]["temperatureCalibrationFactor"]', None,               'float'),
        #      Parameter name                  Initial value              Path to item in JSON returned from REST server     Corresponding settings var
        'temperatureCalibrationOffset': (0,                          '["calibrationFactors"]["temperatureCalibrationOffset"]', None,               'float'),
        'humidityCalibrationFactor':    (1,                          '["calibrationFactors"]["humidityCalibrationFactor"]',    None,               'float'),
        'humidityCalibrationOffset':    (0,                          '["calibrationFactors"]["humidityCalibrationOffset"]',    None,               'float'),
        'pressureCalibrationFactor':    (1,                          '["calibrationFactors"]["pressureCalibrationFactor"]',    None,               'float'),
        'pressureCalibrationOffset':    (0,                          '["calibrationFactors"]["pressureCalibrationOffset"]',    None,               'float'),
        'pm10CalibrationFactor':        (1,                          '["calibrationFactors"]["pm10CalibrationFactor"]',        None,               'float'),
        'pm10CalibrationOffset':        (0,                          '["calibrationFactors"]["pm10CalibrationOffset"]',        None,               'float'),
        'pm25CalibrationFactor':        (1,                          '["calibrationFactors"]["pm25CalibrationFactor"]',        None,               'float'),
        'pm25CalibrationOffset':        (0,                          '["calibrationFactors"]["pm25CalibrationOffset"]',        None,               'float'),
        'pm1CalibrationFactor':         (1,                          '["calibrationFactors"]["pm1CalibrationFactor"]',         None,               'float'),
        'pm1CalibrationOffset':         (0,                          '["calibrationFactors"]["pm1CalibrationOffset"]',         None,               'float'),
        'sampleDuration':               (30,                         '["sampleDuration"]',                                     None,               'int'),
        'mqttPort':                     (1883,                       '["mqttPort"]',                                           None,               'int'),
        'mqttUrl':                      (base_url,                   '["mqttUrl"]',                                            None,               'str'),
        'ledStyle':                     (settings.led_style,         '["ledStyle"]',                                           'led_style',        'str'),
        'deviceToken':                  (settings.device_token,      '["deviceToken"]',                                        'device_token',     'str'),
        'localSsid':                    (device_name,                '["localSsid"]',                                          None,               'str'),
        'localPass':                    (settings.local_pass,        '["localPass"]',                                          'local_pass',       'str'),
        'wifiSsid':                     (settings.wifi_ssid,         '["wifiSsid"]',                                           'wifi_ssid',        'str'),
        'wifiPass':                     (settings.wifi_pass,         '["wifiPass"]',                                           'wifi_pass',        'str'),
        'serialNumber':                 (settings.birdhouse_number,  '["serialNumber"]',                                       'birdhouse_number', 'int'),
    }



def set_params(bhserial, settings):

    defaults = get_default_values(settings)

    # Process in batches because if we do too many, the values don't stick.  Likely a bug in aREST.
    batch = 0
    batch_size = 5

    birdhouse_utils.hard_reset(settings.esp)

    items = list(defaults.items())

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

        print("Sending command:", cmd)

        tries = 3
        done = False

        while tries > 0 and not done:
            resp = send_line_to_serial(bhserial, cmd)
            print("Got line back:", resp)

            resp = send_line_to_serial(bhserial, "")
            print("Got response:", resp)
            
            if resp == '':
                tries -= 1
                continue

            done = True

        if not done:
            print("It appears the device isn't accepting commands.  Try again?")
            exit()

        batch += 1

    params = read_params_from_device(bhserial)

    print(params)

    for item in items:
        field, val, path, fn = item[0], item[1][0], item[1][1], item[1][3]
        checkval = eval("params" + path)

        if eval(fn + "('" + str(checkval) + "')") != eval(fn + "('" + str(val) + "')"):
            print("Value didn't stick: " + field + " expected " + str(val) + ", but got " + str(checkval))
            exit()


def read_params_from_device(bhserial):
    tries = 10
    while tries > 0:
        resp = send_line_to_serial(bhserial, "")
        print("try %d: verify resp >>> %s <<<" % (tries, str(resp)))
        tries -= 1

        if resp == "":
            print("Response was empty")
            next

        try:
            print("Parsing response >>>", resp, "<<<")
            parsed_json = json.loads(resp)
            return parsed_json["variables"]
        except Exception as ex:
            print("Exception:", ex)


def send_line_to_serial(bhserial, cmd):
    # To get the full variable list back, call this with an empty cmd
    bhserial.write(bytes((cmd + '\r\n').encode('UTF-8')))   
    time.sleep(0.5)
    buff = bhserial.read_until()
    return buff


def fetch_firmware():
    file = tempfile.NamedTemporaryFile(delete=False)

    r = requests.get(firmware_url)

    # Save received content in binary format
    file.write(r.content)
    file.close()

    return file.name


def update_dash_def(dash_def, device_id):
    ''' Modifies dash_def '''
    device_name = birdhouse_utils.make_device_name(settings.birdhouse_number)

    aliases = dash_def["configuration"]["entityAliases"].keys()
    for a in aliases:
        try:
            dash_def["configuration"]["entityAliases"][a]["alias"] = device_name
            if "singleEntity" in dash_def["configuration"]["entityAliases"][a]["filter"]:
                dash_def["configuration"]["entityAliases"][a]["filter"]["singleEntity"]["id"] = device_id
        except Exception as e:
            print('Alias: %s\n dash_def["configuration"]["entityAliases"][a]["filter"]: %s' % (a, dash_def["configuration"]["entityAliases"][a]["filter"]))
            raise e


def start_ui(tbapi, serial):
    last_scene = None
    while True:
        try:
            Screen.wrapper(singleton, catch_interrupt=True, arguments=[last_scene, tbapi, serial])
            exit(0)
        except ResizeScreenError as ex:
            last_scene = ex.scene


class MainMenu(Frame):

    overwrite_params_with_cmd_line_values = True

    def __init__(self, screen, settings, tbapi, serial):

        self.has_connection_to_birdhouse = False
        self.initializing = True
        self.orig_data = {}
        self.tbapi = tbapi
        self.token_ok = False
        self.bhserial = serial
        self.settings = settings

        # if not port:
        #     raise Exception("I don't know what port to use!")

        super(MainMenu, self).__init__(screen,
                                   screen.height * 2 // 3,
                                   screen.width * 2 // 3,
                                   hover_focus=True,
                                   title="Birdhouse Configurator")

        layout = Layout([100], fill_frame=True)
        self.add_layout(layout)
        self.layout = layout    # For use when detecting changes

        # Add some colors to our palette
        self.palette["changed"] = (Screen.COLOUR_RED, Screen.A_NORMAL, Screen.COLOUR_BLUE)

        self._status_msg = Label("Welcome!")

        # User supplied data:
        layout.add_widget(Text("Birdhouse Number:", "birdhouse_number", validator=r'^\d+$', on_change=self.on_birdhouse_number_changed, on_blur=partial(self.on_blurred, "birdhouse_number", True)))
        layout.add_widget(Text("Device Token:", "device_token", validator=token_validation_pattern, on_blur=partial(self.on_blurred, "device_token", True)))

        layout.add_widget(Text("Local Password:", "local_pass", on_blur=partial(self.on_blurred, "local_pass", False)))
        layout.add_widget(Text("WiFi SSID:", "wifi_ssid", on_blur=partial(self.on_blurred, "wifi_ssid", False)))
        layout.add_widget(Text("WiFi Password:", "wifi_pass", on_blur=partial(self.on_blurred, "wifi_pass", False)))

        layout.add_widget(RadioButtons(led_styles, "LED Style:", "led_style", on_change=self.on_led_style_changed))


        # provision.py [<number> --token TOKEN --ledstyle LEDSTYLE --wifissid SSID --wifipass PASSWORD --devicepass PASSWORD]



        # layout.add_widget(RadioButtons([("Red", "Red"), ("Yellow", "Yellow"), ("Green", "Green"), ("Cycle", "all"), ("Off", "off")], "Test LEDs", "led_test", on_change=self.on_test_leds_changed))
        # self.layout.find_widget("led_test").value = "off"


        # Parameters
        layout.add_widget(Text("Using device on Port:", "port"))
        layout.find_widget("port").disabled = True
        # layout.find_widget("port").value = settings.port or "Device not found! Please plug it in!"

        layout.add_widget(Text("Local SSID:", "local_ssid", on_change=self.input_changed))
        layout.find_widget("local_ssid").disabled = True

        layout.add_widget(Divider())


        # Buttons
        layout2 = Layout([1, 1])
        self.add_layout(layout2)
        layout2.add_widget(Button("[Install firmware]", self.install_firmware, add_box=False), 0)
        layout2.add_widget(Button("[Exit]", self._quit, add_box=False), 1)

        layout3 = Layout([16, 15, 8])
        self.add_layout(layout3)
        layout3.add_widget(Button("[Write settings]", self.write_values, add_box=False), 0)
        layout3.add_widget(Button("[Load settings]", self.reload_values, add_box=False), 1)
        layout3.add_widget(Button("[Reboot]", self.reboot, add_box=False), 2)

        # Status message
        layout_status = Layout([100])
        self.add_layout(layout_status)
        layout_status.add_widget(self._status_msg)

        self.fix()  # Calculate positions of widgets

        self.initializing = False

        # self.query_birdhouse()

        self.has_connection_to_birdhouse = False

        self.input_changed()
        self.overwrite_params_with_cmd_line_values = False

        self.set_widgets_from_settings()


    def set_widgets_from_settings(self):
        ''' Finds any widgets that have the same names as our settings vars, and sets their value '''

        for item in vars(settings).items():
            settings_param_name, val = item[0], item[1]

            widget = self.layout.find_widget(settings_param_name)
            if widget is None:
                continue

            widget.value = str(val) if val is not None else ""


    def revalidate_device_token(self):
        ''' Check if device token is valid for specified birdhouse; update ui accordingly '''
        if self.initializing:
            return

        token = self.layout.find_widget("device_token").value
        self.token_ok = False

        if not re.match(token_validation_pattern, token):
            return

        if settings.birdhouse_number is None:
            return

        if validate_device_token(settings.birdhouse_number, token):
            self.set_status_msg("Token OK")
            self.token_ok = True
        else:
            self.set_status_msg("Token validation failed; appears invalid")


    def on_birdhouse_number_changed(self):
        ''' Immediate response to birdhouse number changing '''
        if self.initializing: return

        num = self.layout.find_widget('birdhouse_number').value
        self.layout.find_widget('local_ssid').value = birdhouse_utils.make_device_name(num)


    def on_blurred(self, name=None, change_triggers_device_token_validation=False):
        if name is None:
            return

        print("name=", name)

        changed = self.update_settings_param(name)
        if changed and change_triggers_device_token_validation:
            self.revalidate_device_token()

        self.set_status_msg(name + " set to " + eval("settings." + name))


    def update_settings_param(self, item):
        val = self.layout.find_widget(item).value
        # if val == "":
        #     return False
        if val != eval("self.settings." + item):
            exec("self.settings." + item + " = '" + val + "'")
            return True
        return False


    def input_changed(self):
        if self.initializing:
            return

        self.set_status_msg("")

        if self.orig_data:
            for key, value in self.orig_data.items():
                widget = self.layout.find_widget(key)

                if widget is None:      # This can happen if on_change is called during form construction
                    pass
                elif not widget.is_valid:
                    widget.custom_colour = "invalid"
                elif widget.value != value and parse_list[key][1] is not None:
                    widget.custom_colour = "changed"
                elif widget.disabled:
                    widget.custom_colour = "disabled"
                else:
                    widget.custom_colour = "edit_text"


    def on_led_style_changed(self):
        if self.initializing:
            return

        val = self.layout.find_widget("led_style").value
        self.settings.led_style = val


    def on_test_leds_changed(self):
        if self.initializing:
            return

        if self.has_connection_to_birdhouse:
            val = self.layout.find_widget("led_test").value
            send_line_to_serial(self.bhserial, '/leds?color=' + val)   # We'll ignore the response

            if val == 'all':
                self.set_status_msg('LEDs should be cycling')
            elif val == 'off':
                self.set_status_msg('LEDS should be off')
            else:
                self.set_status_msg(val + " LED should be on")


    def query_birdhouse(self):
        ''' Updates the UI and settings object based on values retrieved from an attached device '''
        defaults = get_default_values(settings)
        params = read_params_from_device(self.bhserial)

        for item in list(defaults.items()):
            field, val, path, settingspath = item[0], item[1][0], item[1][1], item[1][2]

            checkval = eval("params" + path)

            self.set_status_msg(checkval)
            if settingspath is not None:
                exec('self.settings.' + settingspath + ' = "' + str(checkval) + '"')

            self.set_widgets_from_settings()


    def set_status_msg(self, msg):
        self._status_msg.text = msg
        print(msg)


    def write_values(self):
        set_params(self.bhserial, self.settings)
        self.set_status_msg("Parameters successfully written to device")


    def install_firmware(self):
        upload_firmware_and_configure(self.bhserial, self.settings)


    def reload_values(self):
        self.set_status_msg("Reading values from device")
        birdhouse_utils.hard_reset(self.settings.esp)
        self.set_status_msg("Done")
        self.query_birdhouse()
        self.set_status_msg("Imported saved values from device")


    def reboot(self):
        self.set_status_msg("Rebooting birdhouse...")

        send_line_to_serial(self.bhserial, '/restart')  # We'll ignore the response

        time.sleep(3)
        self.query_birdhouse()


    def make_device_name(self, base_name):
        return base_name

    # Retrieve token from SB server using Local SSID field as device name

    def retrieve_token(self):
        device_name = self.make_device_name(self.layout.find_widget('local_ssid').value)
        print("Looking for device %s" % device_name)
        device = tbapi.get_device_by_name(device_name)
        if device is None:
            self.layout.find_widget('device_token').value = 'DEVICE_NOT_FOUND'
            return

        device_id = tbapi.get_id(device)
        device_token = tbapi.get_device_token(device_id)
        self.layout.find_widget('device_token').value = device_token

    def _quit(self):
        self.save()
        print("Bye!")
        print(self.settings)
        raise StopApplication("User pressed quit")


class ServerSetup(Frame):
    def __init__(self, screen, model):
        super(ServerSetup, self).__init__(screen,
                                          screen.height * 2 // 3,
                                          screen.width * 2 // 3,
                                          hover_focus=True,
                                          title="Contact Details",
                                          reduce_cpu=True)
        # Save off the model that accesses the contacts database.
        self._model = model

        # Create the form for displaying the list of contacts.
        layout = Layout([4, 2, 1], fill_frame=True)
        self.add_layout(layout)

# This to be passed in
# cust_name = "Hank Williams 3"
# cust_address = "4000 SE 39th"
# cust_address2 = None
# cust_city = "Portland"
# cust_state = "OR"
# cust_zip = None     # Will be populated by geocoder if empty
# cust_country = "USA"
# cust_email = "hank@email.com"
# cust_phone = "555-1212"
# cust_lat = None     # Will be populated by geocoder if empty
# cust_lon = None     # Will be populated by geocoder if empty

        layout.add_widget(Text("Name:", "name"))
        layout.add_widget(Text("Address:", "address"))
        layout.add_widget(Text("Address 2:", "address2"))
        layout.add_widget(Text("City:", "city"))
        layout.add_widget(Text("State:", "state"))
        layout.add_widget(Text("Zip:", "zip"))
        layout.add_widget(Text("Country:", "country"))
        layout.add_widget(Text("Email address:", "email"))
        layout.add_widget(Text("Phone number:", "phone"))
        layout.add_widget(Text("Lat:", "lat"))
        layout.add_widget(Text("Lon:", "lon"), 1)
        layout.add_widget(Button("From Address", self._ok), 2)
        layout2 = Layout([1, 1, 1, 1])
        self.add_layout(layout2)
        layout2.add_widget(Button("OK", self._ok), 0)
        layout2.add_widget(Button("Cancel", self._cancel), 3)
        self.fix()

    def reset(self):
        # Do standard reset to clear out form, then populate with new data.
        super(ServerSetup, self).reset()
        self.data = self._model.get_current_contact()

    def _ok(self):
        self.save()
        self._model.update_current_contact(self.data)
        raise NextScene("Main")

    @staticmethod
    def _cancel():
        raise NextScene("Main")


class ServerSetupModel(object):
    def __init__(self):

        # Current contact when editing.
        self.current_id = None

    def add(self, contact):
        pass

    def get_summary(self):
        pass

    def get_contact(self, contact_id):
        pass

    def get_current_contact(self):
        pass

    def update_current_contact(self, details):
        pass

    def delete_contact(self, contact_id):
        pass


def singleton(screen, scene, tbapi, serial):
    server_config = ServerSetupModel()

    scenes = [
        Scene([MainMenu(screen, settings, tbapi, serial)], -1, name="Main"),
        Scene([ServerSetup(screen, server_config)], -1, name="Server Configuration")
    ]

    screen.play(scenes, stop_on_resize=False, start_scene=scene)



main(settings)
