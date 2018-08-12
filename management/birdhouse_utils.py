import geopy        # pip install geopy

# Import some geocoders
from geopy.geocoders import Nominatim
from geopy.geocoders import Bing
from geopy.geocoders import GoogleV3

from config import google_geocoder_api_key, bing_geocoder_api_key

import serial
import serial.tools.list_ports


# These are IDs that are associated with NodeMCU boards
ESP8266_VID_PIDS = ['1A86:7523', '10C4:EA60']



def make_device_name(birdhouse_number):
    return 'Birdhouse ' + str(birdhouse_number).zfill(3)
    

def get_sensor_type(birdhouse_number):
    # 90-99 resered for BottleBot
    if int(birdhouse_number) >= 90 and int(birdhouse_number) <= 99:
        return 'BottleBot'

    # 100-109 reserved for Lair Hill
    if int(birdhouse_number) >= 100 and int(birdhouse_number) <= 109:
        return 'Lair Hill Birdhouse'


    # Everything else is a Sensorbot Birdhouse
    return 'Sensorbot Birdhouse'


def make_dash_name(birdhouse_number):
    return make_device_name(birdhouse_number) + " Dash"


def one_line_address(cust_info):
    return cust_info["address"] + ", " + ((cust_info["address2"] + ", ") if cust_info["address2"] is not None and cust_info["address2"] != "" else "") + cust_info["city"] + ", " + cust_info["state"] 


def update_customer_data(cust_info):    
    location = geocode(cust_info["address"], cust_info["address2"], cust_info["city"], cust_info["state"], cust_info["zip"], cust_info["country"])

    print(location)
    if location is not None:
        # Update customer data with cocoder data
        if cust_info["zip"] == "" or cust_info["zip"] == None:
            cust_info["zip"] = location["zip"]

        if cust_info["lat"] == "" or cust_info["lat"] == None:
            cust_info["lat"] = location["lat"]

        if cust_info["lon"] == "" or cust_info["lon"] == None:
            cust_info["lon"] = location["lon"]
    else:
        if cust_info["zip"] == "" or cust_info["zip"] == None:
            raise ValueError("Need a zip code to proceed")

        if cust_info["lat"] == "" or cust_info["lat"] == None or cust_info["lon"] == "" or cust_info["lon"] == None:
            raise ValueError("Need a lat/lon to proceed")

    print (cust_info)
    return cust_info


''' Geocode an address '''
def geocode(address, address2, city, state, zip, country):

    if address == None or address == "":
        print("Need an address to geocode!")
        return None

    geoaddress = address

    if address2 != None and address2 != "":
        geoaddress += "," + address2

    if city != None and city != "":
        geoaddress += "," + city

    if state != None and state != "":
        geoaddress += "," + state

    if zip != None and zip != "":
        geoaddress += "," + zip

    if country != None and country != "":
        geoaddress += "," + country

    geolocator = Nominatim(timeout=30)
    location = geolocator.geocode(geoaddress, exactly_one=True, addressdetails=True)
    if location is not None:
        return {"lat":location.latitude, "lon":location.longitude, "zip":get_zip_from_nominatim_location(location)}
    else:
        print("Nominatim could not geocode address")


    if bing_geocoder_api_key != None:
        geolocator = Bing(api_key=bing_geocoder_api_key, timeout=30)
        location = geolocator.geocode(geoaddress, exactly_one=True)
        if location is not None:
            return {"lat":location.latitude, "lon":location.longitude, "zip":get_zip_from_bing_location(location)}
        else:
            print("Bing could not geocode address")
    else:
        print("Skipping Bing geocoder because we don't have a free API key")


    if google_geocoder_api_key != None:   
        geolocator = GoogleV3(api_key=google_geocoder_api_key, timeout=30)
        location = geolocator.geocode(geoaddress)
        if location is not None:
            return {"lat":location.latitude, "lon":location.longitude, "zip":get_zip_from_google_location(location)}
        else:   
            print("Google could not geocode address")
    else:
        print("Skipping Google geocoder because we don't have a free API key")

    return None


# location looks like this: {'__type': 'Location:http://schemas.microsoft.com/search/local/ws/rest/v1', 'bbox': [45.4876072824293, -122.630786395813, 45.4953327175707, -122.616093604187], 'name': '3814 SE Cora St, Portland, OR 97202', 'point': {'type': 'Point', 'coordinates': [45.49147, -122.62344]}, 'address': {'addressLine': '3814 SE Cora St', 'adminDistrict': 'OR', 'adminDistrict2': 'Multnomah', 'countryRegion': 'United States', 'formattedAddress': '3814 SE Cora St, Portland, OR 97202', 'locality': 'Portland', 'postalCode': '97202'}, 'confidence': 'High', 'entityType': 'Address', 'geocodePoints': [{'type': 'Point', 'coordinates': [45.49147, -122.62344], 'calculationMethod': 'Rooftop', 'usageTypes': ['Display']}, {'type': 'Point', 'coordinates': [45.4916700040911, -122.623439999312], 'calculationMethod': 'Rooftop', 'usageTypes': ['Route']}], 'matchCodes': ['Good']}
def get_zip_from_bing_location(location):
    return location.raw['address']['postalCode']


# location looks like this: {'place_id': '153499388', 'licence': 'Data © OpenStreetMap contributors, ODbL 1.0. http://www.openstreetmap.org/copyright', 'osm_type': 'way', 'osm_id': '367938871', 'boundingbox': ['45.4914508', '45.4915745', '-122.6234954', '-122.623365'], 'lat': '45.49151265', 'lon': '-122.623430224919', 'display_name': '3814, Southeast Cora Street, Creston-Kenilworth, Portland, Multnomah County, Oregon, 97202, United States of America', 'class': 'building', 'type': 'yes', 'importance': 0.421, 'address': {'house_number': '3814', 'road': 'Southeast Cora Street', 'suburb': 'Creston-Kenilworth', 'city': 'Portland', 'county': 'Multnomah County', 'state': 'Oregon', 'postcode': '97202', 'country': 'United States of America', 'country_code': 'us'}}
def get_zip_from_nominatim_location(location):
    return location.raw['address']['postcode']

# location looks like this: {'address_components': [{'long_name': '3814', 'short_name': '3814', 'types': ['street_number']}, {'long_name': 'Southeast Cora Street', 'short_name': 'SE Cora St', 'types': ['route']}, {'long_name': 'Southeast Portland', 'short_name': 'Southeast Portland', 'types': ['neighborhood', 'political']}, {'long_name': 'Portland', 'short_name': 'Portland', 'types': ['locality', 'political']}, {'long_name': 'Multnomah County', 'short_name': 'Multnomah County', 'types': ['administrative_area_level_2', 'political']}, {'long_name': 'Oregon', 'short_name': 'OR', 'types': ['administrative_area_level_1', 'political']}, {'long_name': 'United States', 'short_name': 'US', 'types': ['country', 'political']}, {'long_name': '97202', 'short_name': '97202', 'types': ['postal_code']}, {'long_name': '3240', 'short_name': '3240', 'types': ['postal_code_suffix']}], 'formatted_address': '3814 SE Cora St, Portland, OR 97202, USA', 'geometry': {'bounds': {'northeast': {'lat': 45.491584, 'lng': -122.6233581}, 'southwest': {'lat': 45.4914341, 'lng': -122.6234858}}, 'location': {'lat': 45.491509, 'lng': -122.6234219}, 'location_type': 'ROOFTOP', 'viewport': {'northeast': {'lat': 45.4928580302915, 'lng': -122.6220729697085}, 'southwest': {'lat': 45.4901600697085, 'lng': -122.6247709302915}}}, 'partial_match': True, 'place_id': 'ChIJYTmEb3eglVQRkRYA8D5F_yA', 'types': ['premise']}
def get_zip_from_google_location(location):
    bits = location.raw['address_components']
    postal_code = None
    postal_code_suffix = None

    for b in bits:
        if 'postal_code' in b['types']:
            postal_code = b['long_name']
        if 'postal_code_suffix' in b['types']:
            postal_code_suffix = b['long_name']

    if postal_code == None:
        return None

    # if postal_code_suffix != None:
    #     return postal_code + "-" + postal_code_suffix 

    return postal_code




# Build a list of ports we can use
def get_ports():
    return list(serial.tools.list_ports.comports())


def get_port_hwids():
    hwids = []
    for port in get_ports():
        hwids.append(port.hwid)

    return hwids


def get_port_names():
    # for port in list(serial.tools.list_ports.comports()):
    #     print(port.name or port.device, port.hwid)

    ports = []
    for port in get_ports():
        ports.append((port.name or port.device, port.name or port.device))
    
    return ports


def get_best_guess_port():
    for port in get_ports():
        for vid in ESP8266_VID_PIDS:
            if "VID:PID=" + vid in port.hwid:
                return port.name or port.device
    return None
    


