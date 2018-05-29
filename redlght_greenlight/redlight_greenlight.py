#!/usr/bin/env python
import json
import web              # sudo pip3 install git+https://github.com/webpy/webpy#egg=web.py
import googlemaps       # sudo pip install googlemaps
import geopy.distance   # sudo pip install geopy
import re
import os
import base64
import time
from pprint import pprint
import hashlib          # for md5 

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from redlight_greenlight_config import motherShipUrl, username, password, data_encoding, google_geolocation_key

tbapi = TbApi(motherShipUrl, username, password)
gmaps = googlemaps.Client(key=google_geolocation_key)

urls = (
    '/', 'set_led_color',
    '/hotspots/', 'handle_hotspots',
    '/update/(.*)', 'handle_update',
    '/purpleair/(.*)', 'handle_purpleair'
)

app = web.application(urls, globals())

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
        except:
            web.debug("Cannot parse incoming packet:", web.data())

            # Diagnose common configuration problems
            if '$ss' in decoded:
                pos = decoded.find('$ss')
                while(pos > -1):
                    end = decoded.find(" ", pos)
                    word = decoded[pos+4:end].strip(',')
                    web.debug("Missing server attribute", word)

                    pos = decoded.find('$ss', pos + 1)
            return

        hotspots = str(incoming_data["hotspots"])
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

        outgoing_data = {"wifi_distance" : dist, "wifi_distance_accuracy" : wifi_acc}

        web.debug("Sending ", outgoing_data)
        try:
            tbapi.send_telemetry(device_token, outgoing_data)
        except:
            web.debug("Error sending location telemetry!")
            return

class handle_update:
    # Returns the full file/path of the latest firmware, or None if we are already running the latest
    def find_firmware(self, current_version):
        updates_folder = "/tmp"
        
        v = re.search("(\d+)\.(\d+)", current_version)
        newest_major = int(v.group(1))
        newest_minor = int(v.group(2))
        newest_firmware = None


        for file in os.listdir(updates_folder):
            candidate = re.search("(\d+)\.(\d+).bin", file)
            if candidate:
                c_major = int(candidate.group(1))
                c_minor = int(candidate.group(2))

                if c_major > newest_major or c_minor > newest_minor:
                    newest_major = c_major
                    newest_minor = c_minor
                    newest_firmware = os.path.join(updates_folder, file)

        return newest_firmware


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

        newest_firmware = self.find_firmware(current_version)

        if newest_firmware:
            web.debug("Upgrading birdhouse to " + newest_firmware)
            with open(newest_firmware, 'rb') as file:
                bin_image = file.read()
                byte_count = str(len(bin_image))
                md5 = hashlib.md5(bin_image).hexdigest()

                web.debug("Sending update (" + byte_count + " bytes), with hash " + md5)

                web.header('Content-type','application/octet-stream')
                web.header('Content-transfer-encoding','base64') 
                web.header('Content-length', byte_count)
                web.header('x-MD5', md5)
                return bin_image
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
