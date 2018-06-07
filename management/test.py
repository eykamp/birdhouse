import birdhouse_utils

import sys

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi


from provision_config import motherShipUrl, username, password


# This to be passed in
birdhouse_number = "019"
cust_info = {}

cust_info["address"]  = "2101 SE Tibbetts"
cust_info["address2"] = ""
cust_info["city"]     = "Portland"
cust_info["state"]    = "OR"
cust_info["zip"]      = None     # Will be populated by geocoder if empty
cust_info["country"]  = "USA"
cust_info["lat"]      = None     # Will be populated by geocoder if empty
cust_info["lon"]      = None     # Will be populated by geocoder if empty



def main():
    tbapi = TbApi(motherShipUrl, username, password)

    devices = tbapi.get_devices_by_name("Birdhouse")
    print(devices)
    for d in devices:
        print(d["name"])
    exit()

    # Lookup missing fields, such as zip, lat, and lon
    birdhouse_utils.update_customer_data(cust_info)

    if cust_info["lat"] is None or cust_info["lon"] is None:
        print("Must have valid lat/lon to continue!")
        exit(1)


    name = birdhouse_utils.make_device_name(birdhouse_number)

    cust = tbapi.get_customer(name)
    devices = tbapi.get_customer_devices(cust)

    print(devices)

    device = tbapi.get_device_by_name(name)

    customer = tbapi.update_customer(cust, None, cust_info["address"], cust_info["address2"], cust_info["city"], cust_info["state"], cust_info["zip"], cust_info["country"])
    server_attributes = {
        "latitude": cust_info["lat"],
        "longitude": cust_info["lon"],
        "address": birdhouse_utils.one_line_address(cust_info)
    }
    tbapi.set_server_attributes(device, server_attributes)

    exit()


    cust = tbapi.get_customer(name)

    # Update device coordinates


main()        