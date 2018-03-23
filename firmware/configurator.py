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

ESP8266_VID_PID = '1A86:7523'





# The keys in the following dict are the names of data entry and display fields on our form; 
# The values are paths for extracting the desired value from the JSON string returned by the birdhouse
parse_list = {
        'uptime'                 : '["uptime"]',
        'wifi_status'            : '["wifiStatus"]',
        'traditionalLeds'        : '["ledparams"]["traditionalLeds"]',
        'ledsInstalledBackwards' : '["ledparams"]["ledsInstalledBackwards"]',
        'plantowerSensorDetected': '["sensorsDetected"]["plantowerSensorDetected"]',
        'temperatureSensor'      : '["sensorsDetected"]["temperatureSensor"]',
        'mqtt_status'            : '["mqttStatus"]',
        'device_token'           : '["deviceToken"]',
        'local_ssid'             : '["localSsid"]',
        'local_pass'             : '["localPass"]',
        'wifi_ssid'              : '["wifiSsid"]',
        'wifi_pass'              : '["wifiPass"]',
        'mqtt_url'               : '["mqttUrl"]',
        'mqtt_port'              : '["mqttPort"]'
    }

class Main(Frame):
    def __init__(self, screen):
        self.has_serial = False
        self.initializing = True

        super(Main, self).__init__(screen,
                                   screen.height * 2 // 3,
                                   screen.width * 2 // 3,
                                   hover_focus=True,
                                   title="Birdhouse Configurator")

        # Add some colors to our palette
        self.palette["changed"] = (Screen.COLOUR_RED, Screen.A_NORMAL, Screen.COLOUR_BLUE)

        self.port_list = ListBox(4, [], name="ports", on_select=self.on_select_port)
        self._status_msg = Label("Welcome!")

        self.reset_form()

        try:
            print("Plug birdhouse in, please!")
            polling.poll(lambda: self.scan_ports(), timeout=30, step=0.5)
        except Exception as ex:
            print("Could not find any COM ports")
            raise ex


        layout = Layout([100], fill_frame=True)
        self.add_layout(layout)
        self.layout = layout    # For use when detecting changes

        # Connection info
        layout.add_widget(self.port_list)
        layout.add_widget(Divider())

        layout.add_widget(RadioButtons([("Red", "Red"), ("Yellow", "Yellow"), ("Green", "Green"), ("Cycle", "all"), ("Off", "off")], "Test LEDs", "led_test", on_change=self.on_test_leds_changed))
        self.layout.find_widget("led_test").value = "off"


        # Parameters
        layout.add_widget(Text("Uptime:", "uptime"))
        layout.find_widget("uptime").disabled=True
        layout.add_widget(Text("Temp Sensor:", "temperatureSensor"))
        layout.find_widget("temperatureSensor").disabled=True
        layout.add_widget(Text("Found PM Sensor:", "plantowerSensorDetected"))
        layout.find_widget("Plantower Sensor Detected").disabled=True
        layout.add_widget(Text("WiFi Status:", "wifi_status"))
        layout.find_widget("wifi_status").disabled=True
        layout.add_widget(Text("MQTT Status:", "mqtt_status"))
        layout.find_widget("mqtt_status").disabled=True

        layout.add_widget(Text("traditional Leds:", "traditionalLeds", on_change=self.input_changed))
        layout.add_widget(Text("ledsInstalled Backwards:", "ledsInstalledBackwards", on_change=self.input_changed))
        layout.add_widget(Text("device Token:", "device_token", on_change=self.input_changed))
        layout.add_widget(Text("Local SSID:", "local_ssid", on_change=self.input_changed))
        layout.add_widget(Text("Local Password:", "local_pass", on_change=self.input_changed))
        layout.add_widget(Text("WiFi SSID:", "wifi_ssid", on_change=self.input_changed))
        layout.add_widget(Text("WiFi Password:", "wifi_pass", on_change=self.input_changed))
        layout.add_widget(Text("MQTT Url:", "mqtt_url", on_change=self.input_changed))
        layout.add_widget(Text("MQTT Port:", "mqtt_port", on_change=self.input_changed))
        layout.add_widget(Divider())

        # Buttons
        layout2 = Layout([1, 1, 1, 1])
        self.add_layout(layout2)
        layout2.add_widget(Button("Write Values", self.write_values), 0)
        layout2.add_widget(Button("Reload Values", self.reload_values), 1)
        layout2.add_widget(Button("Rescan Ports", self.scan_ports), 1)
        layout2.add_widget(Button("Reboot Birdhouse", self.reboot), 2)
        layout2.add_widget(Button("Exit", self._quit), 3)


        layout3 = Layout([100])
        self.add_layout(layout3)
        layout3.add_widget(Divider())
        layout3.add_widget(Button("Finalize", self._finalize), 0)


        # Status message
        layout4 = Layout([100])
        self.add_layout(layout4)
        layout4.add_widget(Divider())
        layout4.add_widget(self._status_msg)

        self.fix()  # Calculate positions of widgets


        self.initializing = False

        

    def input_changed(self):
        if self.initializing:
            return

        self.set_status_msg("")
        print("Checking for changes", self, self.orig_data)

        for key, value in self.orig_data.items():
            widget = self.layout.find_widget(key)
            if widget is None:      # This can happen if on_change is called during form construction
                pass
            elif widget.value != value:
                widget.custom_colour = "changed"
            elif widget.disabled:
                widget.custom_colour = "disabled"
            else:
                widget.custom_colour = "edit_text"


    def on_test_leds_changed(self):
        if self.initializing:
            return

        if self.has_serial:
            with serial.Serial(self.port, 115200, timeout=1) as ser:
                val = self.layout.find_widget("led_test").value
                ser.write(b'/leds?color=' + bytes(val.encode('UTF8')) + b'\r\n')
                if val == 'all':
                    self.set_status_msg('LEDs should be cycling')
                elif val == 'off':
                    self.set_status_msg('LEDS should be off')
                else:
                    self.set_status_msg(val + " LED should be on")


    def on_select_port(self, port=None):
        if port is None:
            port = self.port_list.value

        self.query_birdhouse(port)
        self.port = port


    def query_birdhouse(self, port):
        self.set_status_msg("Querying birdhouse on port " + port)
        self.has_serial = False

        tries = 5

        while tries > 0:
            tries -= 1
            with serial.Serial(port, 115200, timeout=5) as ser:
                ser.write(b'/\n')           # write a string
                line = ser.readline()       # read a '\n' terminated line

                try:         
                    resp = json.loads(line)
                    if resp["connected"]:
                        self.has_serial = True
                        tries = 0
                        self.parse_response(resp)
                        self.orig_data = self.data
                except Exception as ex:
                    print("Err: ", line)
                    # raise ex

                if self.has_serial:
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
            cmd = 'self.data["' + data_element + '"] = "Unknown"'
            exec(cmd)


    def parse_response(self, resp):
        for data_element in parse_list:
            # Runs a series of commands that look like: self.data["local_pass"] = (resp["variables"]["localPass"])
            cmd = 'self.data["' + data_element + '"] = str(resp["variables"]' + parse_list[data_element]  + ')'
            exec(cmd)


    def write_values(self):
        self.save()  # Updates self.data
        self.set_status_msg("Wrote values to Birdhouse EEPROM")
        self.orig_data = self.data

    def reload_values(self):
        self.query_birdhouse(self.port)
        self.set_status_msg("Reset")

    def reboot(self):
        self.set_status_msg("Rebooting birdhouse...")

        with serial.Serial(self.port, 115200, timeout=3) as ser:
            ser.write(b'/restart\r\n')

        time.sleep(3)    
        self.query_birdhouse(self.port)     

    def _finalize(self):
        self.set_status_msg("Finalizing")

    def scan_ports(self):
        self.set_status_msg("Scanning")
        self.port_list.options = get_ports()
        port = get_best_guess_port()

        if port == None:
            self.has_serial = False
            if len(self.port_list.options) == 0:
                self.set_status_msg("No serial ports found.  Is a birdhouse plugged in?")
                return False
            else:
                self.set_status_msg("Please select a port!")
                return True

        self.set_status_msg("Autodetected birdhouse on " + port)
        self.on_select_port(port)
        self.port = port
        self.has_serial = True
        return True




    def _quit(self):
        raise StopApplication("User pressed quit")


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
    



def demo(screen, scene):
    scenes = [
        Scene([Main(screen)], -1, name="Main"),
    ]

    screen.play(scenes, stop_on_resize=True, start_scene=scene)



last_scene = None
while True:
    try:
        Screen.wrapper(demo, catch_interrupt=True, arguments=[last_scene])
        sys.exit(0)
    except ResizeScreenError as e:
        last_scene = e.scene