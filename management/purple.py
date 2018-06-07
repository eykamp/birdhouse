import requests
import json
import re
import datetime, time
import pytz
import sys
from datetime import datetime
from pytz import timezone
import calendar

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi
from provision_config import motherShipUrl, username, password

# Bounding box that roughly defines Portland
ll = (45.374361, -122.857000)
ur = (45.637960, -122.399479)


tbapi = TbApi(motherShipUrl, username, password)


update_station_list = False

def main():
    known_stations = tbapi.get_devices_by_name("PurpleAir")

    # Extract the station ID from the name:
    known_station_ids = []
    for s in known_stations:
        search = re.match("^PurpleAir (\d+)$", s["name"])
        if(search):
            known_station_ids.append(int(search.group(1)))


    if update_station_list:
        new_stations = add_new_stations(known_station_ids)
        known_stations = tbapi.get_devices_by_name("PurpleAir")     # Freshen the list since we may have added some new stations

        
    ctr = 0

    for device in known_stations:
        print(device)
        # s ==> {'id': {'entityType': 'DEVICE', 'id': 'c95d1850-53ce-11e8-8563-9d9c1f00510b'}, 'createdTime': 1525900824149, 'additionalInfo': None, 'tenantId': {'entityType': 'TENANT', 'id': '77e06cd0-9d84-11e7-9007-5108c78814ce'}, 'customerId': {'entityType': 'CUSTOMER', 'id': '13814000-1dd2-11b2-8080-808080808080'}, 'name': 'PurpleAir 7274', 'type': 'PurpleAir'}
        attribs = tbapi.get_server_attributes(device)
        # attribs ==> [{'lastUpdateTs': 1525900824336, 'key': 'latitude', 'value': 45.522683}, {'lastUpdateTs': 1525900824336, 'key': 'longitude', 'value': -122.639815}, {'lastUpdateTs': 1525900824336, 'key': 'primary_foreign_id', 'value': '421294'}, {'lastUpdateTs': 1525900824336, 'key': 'primary_foreign_key', 'value': 'S93EJNPS5Z4A5SL7'}, {'lastUpdateTs': 1525900824336, 'key': 'secondary_foreign_id', 'value': '421297'}, {'lastUpdateTs': 1525900824336, 'key': 'secondary_foreign_key', 'value': 'XHC0MXLQYDYEAYUO'}]

        ts_id, ts_key, ts_id_2, ts_key_2 = None, None, None, None

        token = tbapi.get_device_token(device)

        for attrib in attribs:
            if attrib["key"] == "primary_foreign_id":
                ts_id = attrib["value"]
            elif attrib["key"] == "primary_foreign_key":
                ts_key = attrib["value"]
            elif attrib["key"] == "secondary_foreign_id":
                ts_id_2 = attrib["value"]
            elif attrib["key"] == "secondary_foreign_key":
                ts_key_2 = attrib["value"]

        if ts_id is None or ts_key is None or ts_id_2 is None or ts_key_2 is None:
            print("Could not find required key for device", device)
            exit()


        # "field1":"PM1.0 (ATM)","field2":"PM2.5 (ATM)","field3":"PM10.0 (ATM)","field4":"Uptime","field5":"RSSI","field6":"Temperature","field7":"Humidity"
        fields_wanted = "12367"
        req = requests.get("https://api.thingspeak.com/channels/" + ts_id + "/fields/" + fields_wanted + ".json?offset=0&round=2&average=10&results=4800&api_key=" + ts_key)
        data = json.loads(req.text)
        feeds = data["feeds"]
        payloads = []
        for f in feeds:
            # '2018-05-05T14:10:00Z'
            ts_utc = calendar.timegm(time.strptime(f["created_at"], "%Y-%m-%dT%H:%M:%SZ")) * 1000
            values = {}
            if f["field1"] is not None:
                values["pm1"] = float(f["field1"])
            if f["field2"] is not None:
                values["pm25"] = float(f["field2"])
            if f["field3"] is not None:
                values["pm10"] = float(f["field3"])
            if f["field6"] is not None:
                values["temperature"] = float(f["field6"])
            if f["field7"] is not None:
                values["humidity"] = float(f["field7"])

            if len(values) > 0:
                payload = {"ts":ts_utc, "values":values}
                payloads.append(payload)

        try:
            if len(payloads) > 0:
                ctr += 1
                print(ctr)
                # print(payloads)
                tbapi.send_telemetry(token, payloads)
        except:
            print("Sleeping")
            time.sleep(10)
            tbapi.send_telemetry(token, payloads)
            # for p in payloads:
            #     print("Single mode:", p)
            #     tbapi.send_telemetry(token, p)

            # exit()
    # print(new_stations)

    # https://www.purpleair.com/json?show=s["ID"]  -- Basic info
    # https://api.thingspeak.com/channels/421294/fields/123456789.json?offset=0&round=2&average=10&results=4800&api_key=S93EJNPS5Z4A5SL7


def add_new_stations(known_station_ids):
    # Get a list of all PurpleAir sensors
    req = requests.get("https://www.purpleair.com/json?fetchData=true&minimize=true&sensorsActive2=10080&orderby=L")
    stations = json.loads(req.text)

    # Get a list of PA stations already in TB:
    new_stations = {}

    for station in stations["results"]:
        # Reject stations outside our bounding box
        if not (station["Lat"] is not None and station["Lon"] is not None and station["Lat"] > ll[0] and station["Lat"] < ur[0] and station["Lon"] > ll[1] and station["Lon"] < ur[1]):
            continue

        # Skip over known stations
        if station["ID"] in known_station_ids:
            continue

        # Skip child stations
        if station["ParentID"] is not None:
            continue

        print("Found new station:", station["ID"], known_station_ids)

        new_stations[station["ID"]] = (station)


    # Add new stations to the database

    for sid, s in new_stations.items():
        if s["ParentID"] is None:       # This is a primary station
            # print(s["ID"], s["Lat"], s["Lon"])
            req = requests.get("https://www.purpleair.com/json?show=" + str(s["ID"]))

            info_p = json.loads(req.text)["results"][0]
            info_s = json.loads(req.text)["results"][1]
            # print(info_p)
            parent = info_p["ParentID"]
            location = info_p["DEVICE_LOCATIONTYPE"]

            # Skip indoor devices
            if location != 'outside':
                continue

            if info_p["ParentID"] is not None and info_s["ParentID"] is None:
                infp_p, info_s = info_s, info_p     # Swap, should never happen
                print("Swapped primary and secondary", json.loads(req.text)["results"])


            thingspeak_primary_id  = info_p["THINGSPEAK_PRIMARY_ID"]
            thingspeak_primary_key = info_p["THINGSPEAK_PRIMARY_ID_READ_KEY"]

            thingspeak_secondary_id  = info_s["THINGSPEAK_PRIMARY_ID"]
            thingspeak_secondary_key = info_s["THINGSPEAK_PRIMARY_ID_READ_KEY"]

            server_attributes = { "latitude" : s["Lat"], "longitude" : s["Lon"], 
                                  "primary_foreign_id"   : thingspeak_primary_id,   "primary_foreign_key"   : thingspeak_primary_key,
                                  "secondary_foreign_id" : thingspeak_secondary_id, "secondary_foreign_key" : thingspeak_secondary_key
                                }

            print("Adding device %s" % str(sid))
            dev = tbapi.add_device("PurpleAir " + str(sid), "PurpleAir", None, server_attributes)

main()

