#!/usr/bin/env python
import json
import web              # sudo pip3 install git+https://github.com/webpy/webpy#egg=web.py
import googlemaps       # sudo pip install googlemaps
import geopy.distance   # sudo pip install geopy
import re
import os
import base64
import time
import hashlib          # for md5 

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from redlight_greenlight_config import motherShipUrl, username, password, data_encoding, google_geolocation_key, firmware_images_folder

tbapi = TbApi(motherShipUrl, username, password)
gmaps = googlemaps.Client(key=google_geolocation_key)

urls = (
    '/', 'set_led_color',
    '/hotspots/', 'handle_hotspots',
    '/update/(.*)', 'handle_update',
    '/purpleair/(.*)', 'handle_purpleair'
)

app = web.application(urls, globals())


def get_immediate_subdirectories(a_dir):
    return [name for name in os.listdir(a_dir)
        if os.path.isdir(os.path.join(a_dir, name))]


class handle_purpleair:
    def POST(self, data):
        web.debug("Handling purpleair")
        print("data: ", web.data())
        print("passed: ", data)
        print("ctx: ", web.ctx)
        print("env: ", web.ctx.env)


class handle_hotspots:
    def POST(self):
        web.debug("Handling hotspots")
        try:
            data = web.data()
            decoded = data.decode(data_encoding)
            incoming_data = json.loads(decoded)
            # web.debug(incoming_data)

            known_lat = incoming_data["latitude"]
            known_lng = incoming_data["longitude"]
            device_token = incoming_data["device_token"]
        except Exception as ex:
            web.debug("Cannot parse incoming packet:", web.data(), ex)

            # Diagnose common configuration problem
            if '$ss' in decoded:
                pos = decoded.find('$ss')
                while(pos > -1):
                    end = decoded.find(" ", pos)
                    word = decoded[pos+4:end].strip(',')
                    web.debug("Missing server attribute", word)

                    pos = decoded.find('$ss', pos + 1)
            return

        hotspots = str(incoming_data["visibleHotspots"])
        web.debug("Geolocating for data " + hotspots)

        try:
            results = gmaps.geolocate(wifi_access_points=hotspots)
        except Exception as ex:
            web.debug("Exception while geolocating", ex)
            return


        web.debug("Geocoding results for " + device_token + ":", results)

        if "error" in results:
            web.debug("Received error from Google API!")
            return
 
        try:
            wifi_lat = results["location"]["lat"]
            wifi_lng = results["location"]["lng"]
            wifi_acc = results["accuracy"]
        except:
            web.debug("Error parsing response from Google Location API!")
            return

        web.debug("Calculating distance...")
        try:
            dist = geopy.distance.vincenty((known_lat, known_lng),(wifi_lat, wifi_lng)).m  # In meters!
        except:
            web.debug("Error calculating!")
            return

        outgoing_data = {"wifiDistance" : dist, "wifiDistanceAccuracy" : wifi_acc}

        web.debug("Sending ", outgoing_data)
        try:
            tbapi.send_telemetry(device_token, outgoing_data)
        except:
            web.debug("Error sending location telemetry!")
            return

def get_path_of_latest_firmware(folder, current_major = 0, current_minor = 0):
    newest_firmware = None
    newest_major = current_major
    newest_minor = current_minor

    for file in os.listdir(folder):
        candidate = re.search("(\d+)\.(\d+).bin", file)
        if candidate:
            major = int(candidate.group(1))
            minor = int(candidate.group(2))

            if major > newest_major or (major == newest_major and minor > newest_minor):
                newest_major = major
                newest_minor = minor
                newest_firmware = os.path.join(folder, file)

    return newest_firmware


def get_firmware(full_filename):
    with open(full_filename, 'rb') as file:
        bin_image = file.read()
        byte_count = str(len(bin_image))
        md5 = hashlib.md5(bin_image).hexdigest()

        web.debug("Sending firmware (" + byte_count + " bytes), with hash " + md5)

        web.header('Content-type','application/octet-stream')
        web.header('Content-transfer-encoding','base64') 
        web.header('Content-length', byte_count)
        web.header('x-MD5', md5)

        return bin_image


class handle_update:
    # Returns the full file/path of the latest firmware, or None if we are already running the latest
    def find_firmware_folder(self, current_version, mac_address):
        v = re.search("(\d+)\.(\d+)", current_version)
        current_major = int(v.group(1))
        current_minor = int(v.group(2))

        device_specific_subfolder = None

        # If there is a dedicated folder for this device, search there; if not, use the default firmware_images_folder
        subdirs = get_immediate_subdirectories(firmware_images_folder)

        for subdir in subdirs:
            print(subdir)
            # Folders will have a name matching the pattern SOME_READABLE_PREFIX + underscore + MAC_ADDRESS
            if re.match(".*_" + mac_address.upper(), subdir):
                if device_specific_subfolder is not None:
                    web.debug("Error: found multiple folders for mac address " + mac_address)
                    return None
                device_specific_subfolder = subdir


        if device_specific_subfolder is None:
            folder = firmware_images_folder
        else:
            folder = os.path.join(firmware_images_folder, device_specific_subfolder)

        print("Using firmware folder " + folder)

        if not os.path.isdir(folder):
            print("Error>>> " + folder + " is not a folder!")
            return

        return get_path_of_latest_firmware(folder, current_major, current_minor)


    def GET(self, status):
        web.debug("Handling update request")
        web.debug(status)
        current_version = web.ctx.env.get('HTTP_X_ESP8266_VERSION')
        mac = web.ctx.env.get('HTTP_X_ESP8266_STA_MAC')
        web.debug("Mac %s" % mac)
        # Other available headers
        # 'HTTP_CONNECTION': 'close',
        # 'HTTP_HOST': 'www.sensorbot.org:8989',
        # 'HTTP_USER_AGENT': 'ESP8266-http-Update',
        # 'HTTP_X_ESP8266_AP_MAC': '2E:3A:E8:08:2C:38',
        # 'HTTP_X_ESP8266_CHIP_SIZE': '4194304',
        # 'HTTP_X_ESP8266_FREE_SPACE': '2818048',
        # 'HTTP_X_ESP8266_MODE': 'sketch',
        # 'HTTP_X_ESP8266_SDK_VERSION': '2.2.1(cfd48f3)',
        # 'HTTP_X_ESP8266_SKETCH_MD5': '3f74331d79d8124c238361dcebbf3dc4',
        # 'HTTP_X_ESP8266_SKETCH_SIZE': '324512',
        # 'HTTP_X_ESP8266_STA_MAC': '2C:3A:E8:08:2C:38',
        # 'HTTP_X_ESP8266_VERSION': '0.120',


        # Use passed url params to display a debugging payload -- all will be read as strings; specify defaults in web.input() call to avoid exceptions for missing values
        # params = web.input(mqtt_status='Not specified')
        # mqtt_status = params.mqtt_status
        # web.debug("MQTT status:", mqtt_status)

        newest_firmware = self.find_firmware_folder(current_version, mac)

        if newest_firmware:
            web.debug("Upgrading birdhouse to " + newest_firmware)
            return get_firmware(newest_firmware)
        else:
            web.debug("Birdhouse already at most recent version (" + current_version + ")")

        raise web.NotModified()

class set_led_color:    
    def POST(self):
        # Decode request data

        incoming_data = json.loads(str(web.data().decode(data_encoding)))

        temperature = incoming_data["temperature"]
        device_id = incoming_data["device_id"]

        web.debug("Received data for " + device_id + ": ", web.data().decode(data_encoding))

        if float(temperature) < 8:
            color = 'GREEN'
        elif float(temperature) < 15:
            color = 'YELLOW'
        else:
            color = 'RED'

        outgoing_data = { "LED": color, "lastSeen": int(time.time()) }

        tbapi.set_shared_attributes(device_id, outgoing_data)


if __name__ == "__main__":
    app.run()
