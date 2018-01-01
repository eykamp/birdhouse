#!/usr/bin/env python
import json
import web              # sudo pip3 install git+https://github.com/webpy/webpy#egg=web.py
import googlemaps       # sudo pip install googlemaps
import geopy.distance   # sudo pip install geopy

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi

from redlight_greenlight_config import motherShipUrl, username, password, data_encoding, google_geolocation_key


tbapi = TbApi(motherShipUrl, username, password)
gmaps = googlemaps.Client(key=google_geolocation_key)

urls = (
    '/', 'set_led_color',
    '/hotspots/', 'handle_hotspots'
)

app = web.application(urls, globals())

class handle_hotspots:
    def POST(self):
        try:
            data = web.data()
            decoded = data.decode(data_encoding)
            incoming_data = json.loads(decoded)
            print(incoming_data)

            known_lat = incoming_data["latitude"]
            known_lng = incoming_data["longitude"]
            device_token = incoming_data["device_token"]
        except:
            print("Cannot parse incoming packet:", web.data())

            # Diagnose common configuration problems
            if '$ss' in decoded:
                pos = decoded.find('$ss')
                while(pos > -1):
                    end = decoded.find(" ", pos)
                    word = decoded[pos+4:end].strip(',')
                    print("Missing server attribute", word)

                    pos = decoded.find('$ss', pos + 1)
            return

        results = gmaps.geolocate(wifi_access_points=incoming_data["hotspots"])

        print("Geocoding results:", results)

        if "error" in results:
            print("Received error from Google API!")
            return
 
        try:
            wifi_lat = results["location"]["lat"]
            wifi_lng = results["location"]["lng"]
            wifi_acc = results["accuracy"]
        except:
            print("Error parsing response from Google Location API!")
            return

        print("Calculating distance...")
        try:
            dist = geopy.distance.vincenty((known_lat, known_lng),(wifi_lat, wifi_lng)).m  # In meters!
        except:
            print("Error calculating!")
            return

        outgoing_data = {"wifi_distance" : dist, "wifi_distance_accuracy" : wifi_acc}

        print("Sending ", outgoing_data)
        try:
            tbapi.send_telemetry(device_token, outgoing_data)
        except:
            print("Error sending location telemetry!")
            return


class set_led_color:        
    def POST(self):
        # Decode request data

        print("Received data: ", web.data().decode(data_encoding))
        incoming_data = json.loads(str(web.data().decode(data_encoding)))

        temperature = incoming_data["temperature"]
        device_id = incoming_data["device_id"]

        if float(temperature) < 50:
            color = 'GREEN'
        elif float(temperature) < 80:
            color = 'YELLOW'
        else:
            color = 'RED'

        outgoing_data = {"LED": color}

        tbapi.set_shared_attributes(device_id, outgoing_data)

        web.header('Content-Type', 'application/json')
        data = { "nonce": color }

        print(json.dumps(data))

        return json.dumps(data)


if __name__ == "__main__":
    app.run()
