from asciimatics.screen import Screen   # pip install asciimatics
from asciimatics.scene import Scene
from asciimatics.widgets import Frame, ListBox, Layout, Divider, Text, Button, TextBox, Widget, Label, RadioButtons
from asciimatics.exceptions import ResizeScreenError, NextScene, StopApplication
import json
import sys
import serial
import serial.tools.list_ports
import polling      # pip install polling
import time
import argparse
import subprocess
import os

from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
                                            # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

# Get our passwords and other private data
sys.path.insert(0, "../management/")
import config

motherShipUrl      = config.base_url + ":8080"


ESP8266_VID_PID = '1A86:7523'


ARDUINO_BUILD_EXE_LOCATION = r"C:\Program Files (x86)\Arduino\arduino_debug.exe"
BUILD_FOLDER = r"C:\Temp\BirdhouseFirmwareBuildFolder"
SOURCE_FILE = r"initial\initial.ino"

port = None
    

# Our program starts running from here
def main():
    
    global args, tbapi, port


    tbapi = TbApi(motherShipUrl, config.thingsboard_username, config.thingsboard_password)


    parser = argparse.ArgumentParser(description='Configurate your birdhouse!')

    parser.add_argument('--number', '-n', metavar='NNN', type=str, help='the number of your birdhouse')
    parser.add_argument('--localssid', metavar='ssid', type=str, help='The local SSID for the birdhouse wifi')
    parser.add_argument('--localpass', metavar='pass', type=str, help='The local password for the birdhouse wifi')
    parser.add_argument('--wifissid', metavar='ssid', type=str, help='The SSID for the local wifi')
    parser.add_argument('--wifipass', metavar='pass', type=str, help='The password for the local wifi')
    parser.add_argument('--mqtturl', metavar='url', type=str, help='The url for sending telemetry to')
    parser.add_argument('--mqttport', metavar='port', type=str, help='The server\'s MQTT port')


    parser.add_argument('--nocompile', action='store_true', help="skip compilation, upload stored binary")
    parser.add_argument('--noupload', action='store_true', help="skip compilation and upload, rely on previously uploaded binary")

    args = parser.parse_args()
    
    # Find out what port the Birdhouse is on:
    try:
        port = get_best_guess_port()

        if not port:
            print("Plug birdhouse in, please!")
            port = polling.poll(lambda: get_best_guess_port(), timeout=30, step=0.5)
    except Exception as ex:
        print("Could not find any COM ports")
        raise ex


    # compile_and_upload_firmware(compile=(not args.nocompile), upload=(not args.noupload))

    # Validate
    if args.number is not None:
        if len(args.number) != 3:
            print("Birdhouse number must be 3 digits (include leading 0s)")
            sys.exit(1)
    # else we'll try to get it from the birdhouse itself
    # sys.exit()


    runUi()


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


def run_win_cmd(cmd, args, echo_to_console=True):
    result = []

    cmdline = '"%s" %s' % (cmd, args)

    try:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except FileNotFoundError as ex:
        print("Could not find %s" % cmd)
        sys.exit()
    except Exception as ex:
        print("Command failed: %s" % cmd)
        raise ex

    for line in process.stdout:
        line = line.decode()
        result.append(line)

        if echo_to_console:
            print(line.replace('\r\n',''))

    errcode = process.returncode
    print(errcode)
    # for line in result:
    #     print(line.decode())
    if errcode is not None:
        raise Exception('cmd %s failed, see above for details', cmd)



''' https://github.com/arduino/Arduino/blob/master/build/shared/manpage.adoc '''
def compile_and_upload_firmware(compile=True, upload=True, shell=True):

    if not compile and not upload:
        return

    if compile:
        build_option = "--verify"
    if upload:
        build_option = "--upload"

    if not os.path.exists(SOURCE_FILE):
        raise Exception("Could not find sketch %s" % SOURCE_FILE)

    if BUILD_FOLDER is None:
        raise Exception("No build folder!")
    if os.path.exists(BUILD_FOLDER) and not os.path.isdir(BUILD_FOLDER):
        raise Exception("%s is not a valid folder!" % BUILD_FOLDER)
    if not os.path.exists(BUILD_FOLDER):
        if not os.path.exists(BUILD_FOLDER):
            try:
                os.makedirs(BUILD_FOLDER)
            except Exception:
                pass    # We'll hadle this below

        if not os.path.exists(BUILD_FOLDER) or not os.path.isdir(BUILD_FOLDER):
            raise Exception("Could not create build folder %s!" % BUILD_FOLDER)



    args = '%s --pref build.path="%s" --port %s "%s"' % (build_option, BUILD_FOLDER, port, SOURCE_FILE)

    print(ARDUINO_BUILD_EXE_LOCATION, args)
    run_win_cmd(ARDUINO_BUILD_EXE_LOCATION, args)




def runUi():
    last_scene = None
    while True:
        try:
            Screen.wrapper(singleton, catch_interrupt=True, arguments=[last_scene])
            sys.exit(0)
        except ResizeScreenError as ex:
            last_scene = ex.scene





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

class Main(Frame):

    overwrite_params_with_cmd_line_values = True

    def __init__(self, screen):
        global port

        self.has_connection_to_birdhouse = False
        self.initializing = True
        self.orig_data = { }

        self.bhserial = None
        self.port = port

        if not port:
            raise Exception("I don't know what port to use!")

        super(Main, self).__init__(screen, 
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

        self.reset_form()


        # Connection info
        layout.add_widget(Text("Birdhouse Number:", "birdhouse_number", validator=r"^\d\d\d$", on_change=self.input_changed))
        self.layout.find_widget("birdhouse_number").value = args.number

        layout.add_widget(RadioButtons([("Red", "Red"), ("Yellow", "Yellow"), ("Green", "Green"), ("Cycle", "all"), ("Off", "off")], "Test LEDs", "led_test", on_change=self.on_test_leds_changed))
        self.layout.find_widget("led_test").value = "off"


        # Parameters
        layout.add_widget(Text("Uptime:", "uptime"))
        layout.find_widget("uptime").disabled=True
        layout.add_widget(Text("Temp Sensor:", "temperatureSensor"))
        layout.find_widget("temperatureSensor").disabled=True
        layout.add_widget(Text("Found Plantower Sensor:", "plantowerSensorDetected"))
        layout.find_widget("plantowerSensorDetected").disabled=True
        layout.add_widget(Text("WiFi Status:", "wifi_status"))
        layout.find_widget("wifi_status").disabled=True
        layout.add_widget(Text("MQTT Status:", "mqtt_status"))
        layout.find_widget("mqtt_status").disabled=True

        layout.add_widget(RadioButtons([("Traditional (R/Y/G)", 'RYG'), ("Traditional, wired backward", 'RYG_REVERSED'), ("Single square LED", 'DOTSTAR'), ("Single round LED", '4PIN')], "LED Style:", "led_style", on_change=self.on_led_style_changed))

        layout.add_widget(Text("Device Token:", "device_token", on_change=self.input_changed))

        layout.add_widget(Text("Local SSID:", "local_ssid", on_change=self.input_changed))
        layout.find_widget("local_ssid").disabled=True

        layout.add_widget(Text("Local Password:", "local_pass", on_change=self.input_changed))
        layout.add_widget(Text("WiFi SSID:", "wifi_ssid", on_change=self.input_changed))
        layout.add_widget(Text("WiFi Password:", "wifi_pass", on_change=self.input_changed))
        layout.add_widget(Text("MQTT Url:", "mqtt_url", on_change=self.input_changed))
        layout.add_widget(Text("MQTT Port:", "mqtt_port", on_change=self.input_changed))
        layout.add_widget(Divider())

        # Buttons
        layout2 = Layout([1, 1, 1, 1])
        self.add_layout(layout2)
        layout2.add_widget(Button("Server", self.server_config), 0)
        layout2.add_widget(Button("Commit", self.write_values), 0)
        layout2.add_widget(Button("Refresh", self.reload_values), 1)
        # layout2.add_widget(Button("Rescan Ports", self.scan_ports), 1)
        layout2.add_widget(Button("Reboot Birdhouse", self.reboot), 2)
        layout2.add_widget(Button("Exit", self._quit), 3)


        layout3 = Layout([100])
        self.add_layout(layout3)
        layout3.add_widget(Divider())
        layout3.add_widget(Button("Finalize", self.finalize), 0)
        layout3.add_widget(Button("Get Token from SSID", self.retrieve_token), 0)


        # Status message
        layout4 = Layout([100])
        self.add_layout(layout4)
        layout4.add_widget(Divider())
        layout4.add_widget(self._status_msg)

        self.fix()  # Calculate positions of widgets


        self.initializing = False

        self.bhserial = serial.Serial(port, 115200, timeout=5)
        self.query_birdhouse(port)

        self.port = port
        self.has_connection_to_birdhouse = True


        self.input_changed()
        self.overwrite_params_with_cmd_line_values = False


    def input_changed(self):
        if self.initializing:
            return

        self.set_status_msg("")
        
        # Set dependent field values
        self.layout.find_widget('local_ssid').value = "Birdhouse " + self.layout.find_widget('birdhouse_number').value
        
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

        if self.has_connection_to_birdhouse:
            val = self.layout.find_widget("led_style").value
            cmd = '/setparams?ledStyle=' + val + '\r\n'
            print(cmd)
            self.bhserial.write(bytes(cmd.encode("UTF-8")))
            time.sleep(0.1)
            line = self.bhserial.readline()   # We'll ignore the response
            print(line)

    def on_test_leds_changed(self):
        if self.initializing:
            return

        if self.has_connection_to_birdhouse:
            val = self.layout.find_widget("led_test").value
            cmd = '/leds?color=' + val + '\r\n'
            self.bhserial.write(bytes(cmd.encode("UTF-8")))
            time.sleep(0.1)
            line = self.bhserial.readline()   # We'll ignore the response

            if val == 'all':
                self.set_status_msg('LEDs should be cycling')
            elif val == 'off':
                self.set_status_msg('LEDS should be off')
            else:
                self.set_status_msg(val + " LED should be on")


    def server_config(self):
        raise NextScene("Server Configuration")


    def query_birdhouse(self, port):
        self.has_connection_to_birdhouse = False

        tries = 10

        while tries > 0:
            tries -= 1
            self.bhserial.write(b'/\n')           # write a string
            time.sleep(0.1)
            line = self.bhserial.readline()       # read a '\n' terminated line
            print("read ", line)

            try:         
                resp = json.loads(line)
            except Exception as ex:
                print(ex, "Json Error: ", line)
                pass
                continue

            # If we get here, resp contains at least somewhat valid JSON

            if "connected" in resp and "variables" in resp:     # Make sure we're reading the correct line; there could be garbage in the buffer
                self.has_connection_to_birdhouse = True
                print("tries", tries)
                tries = 0

                self.parse_response(resp)
                # self.orig_data = self.data


            if self.has_connection_to_birdhouse:
                self.set_status_msg("Got response from birdhouse")

                print(self.data)
                self.update(1)
            else:
                self.set_status_msg("Didn't find birdhouse")


    def set_status_msg(self, msg):
        self._status_msg.text = msg
        print(msg)


    def reset_form(self):
        for data_element in parse_list:
            self.data[data_element] = "Unknown"


    def parse_response(self, json):
        for data_element in parse_list:
            # Runs a series of commands that look like: self.data["local_pass"] = (json["variables"]["localPass"])
            cmd = 'self.data["' + data_element + '"] = str(json["variables"]' + parse_list[data_element][0]  + ')'
            try:
                exec(cmd)
            except KeyError as ex:
                print("Could not find expected key in JSON received from Birdhouse.  Looking for: %s, but did not find it.\nJSON: %s" % (data_element, json))
                sys.exit()
            except Exception as ex:
                print("Trying to execute line: %s" % cmd)
                print("JSON: %s" % json)
                print(ex)
                raise ex

            # Special handlers:
            if data_element == 'birdhouse_number':
                self.data[data_element] = str(self.data[data_element]).zfill(3)


            # Do this before clobbering with cmd line params so that the changes will be highlighted on the form
            self.orig_data[data_element] = self.data[data_element]

            # Overwrite any values with those from the cmd line
            if self.overwrite_params_with_cmd_line_values:
                if parse_list[data_element][2] is not None:
                    try:
                        initVal = eval("args." + parse_list[data_element][2])
                    except AttributeError:
                        initVal = None
                    if initVal is not None:
                        self.data[data_element] = initVal
                        print("Setting %s to %s" %( data_element, str(initVal)))


            # Populate the widget
            widget = self.layout.find_widget(data_element)
            if not widget:
                print("Could not find widget %s", data_element)
                sys.exit(1)
            try:
                widget.value =  self.data[data_element]
            except AttributeError as ex:
                print("Failed evaluting element %s" % data_element)
                raise ex

            # Special handlers:
            self.layout.find_widget('birdhouse_number').value = self.data['birdhouse_number']



    def write_values(self):
        self.save()  # Updates self.data

        cmd = '/setparams?'
        first = True

        for data_element in parse_list:
            field = parse_list[data_element][0]
            param = parse_list[data_element][1]

            if param is None:       # Read-only field; nothing to send back to the server
                continue

            val = self.data[data_element]
            if not first:
                cmd += '&'
            cmd += parse_list[data_element][1] + '=' + val
            first = False

        cmd += '\r\n'

        self.bhserial.write(bytes(cmd.encode('UTF-8')))
        time.sleep(0.1)
        line = self.bhserial.readline()     # Read this, but we can ignore it for now

        self.query_birdhouse(self.port)     # Read data back from birdhouse, helps confirm data was written properly
        self.input_changed()                # Resets field changed indicators


    def reload_values(self):
        self.query_birdhouse(self.port)
        self.set_status_msg("Reset")


    def reboot(self):
        self.set_status_msg("Rebooting birdhouse...")

        self.bhserial.write(b'/restart\r\n')

        time.sleep(3)    
        self.query_birdhouse(self.port)     

    def finalize(self):
        self.set_status_msg("Finalizing")
        self.bhserial.write(b'/updateFirmware\r\n')
        time.sleep(3)    
        self.query_birdhouse(self.port)     


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
        print(self.data)
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
        layout = Layout([4,2,1], fill_frame=True)
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





#  TODO: These functions are in birdhouseUtils
# Build a list of ports we can use
def get_ports():
    # for port in list(serial.tools.list_ports.comports()):
    #     print(port.name or port.device, port.hwid)

    # sys.exit()


    ports = []
    for port in list(serial.tools.list_ports.comports()):
        ports.append((port.name or port.device, port.name or port.device))
    
    return ports


def get_best_guess_port():
    for port in list(serial.tools.list_ports.comports()):
        if "VID:PID=" + ESP8266_VID_PID in port.hwid:
            return port.name or port.device
    return None
    



def singleton(screen, scene):
    scenes = [
        Scene([Main(screen)], -1, name="Main"),
        Scene([ServerSetup(screen, server_config)], -1, name="Server Configuration")
    ]

    screen.play(scenes, stop_on_resize=False, start_scene=scene)


# These are global
tbapi = None
args = None

server_config = ServerSetupModel()

main()

