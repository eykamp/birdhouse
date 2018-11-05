import geopy        # pip install geopy

# Import some geocoders
from geopy.geocoders import Nominatim
from geopy.geocoders import Bing
from geopy.geocoders import GoogleV3

from config import google_geocoder_api_key, bing_geocoder_api_key

import serial                       # pip install serial
import serial.tools.list_ports      # pip install pyserial
import re
# pip install serial geopy pyserial

# These are IDs that are associated with NodeMCU boards
ESP8266_VID_PIDS = ['1A86:7523', '10C4:EA60']



def make_device_name(birdhouse_number):
    # If birdhouse_number is not in fact a number, let's assume we already have a name, and just return it
    if re.match(r'^\d+$', str(birdhouse_number)):
        return 'Birdhouse ' + str(birdhouse_number).zfill(3)
    else:
        return birdhouse_number
    

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


# Untested!
def update_customer(tbapi, cust_info):
    tbapi.update_customer(cust_info["cust_id"], cust_info["name"], cust_info["address"], cust_info["address2"], cust_info["city"], cust_info["state"], cust_info["zip"], cust_info["country"], cust_info["email"], cust_info["phone"])

    server_attributes = {
        "latitude": cust_info["lat"],
        "longitude": cust_info["lon"],
        "address": one_line_address(cust_info)
    }

    tbapi.set_server_attributes(cust_info["cust_id"], server_attributes)


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

    return cust_info


def purge_server_objects_for_device(tbapi, device_name):
    print("Cleaning up server objects for '" + device_name + "'...", end='')

    device = tbapi.get_device_by_name(device_name)
    dash = tbapi.get_dashboard_by_name(device_name)
    cust = tbapi.get_customer_by_name(device_name)

    if dash is not None:
        tbapi.delete_dashboard(tbapi.get_id(dash))
        print(" dash...", end='')

    if device is not None:
        tbapi.delete_device(tbapi.get_id(device))
        print(" device...", end='')

    if cust is not None:
        tbapi.delete_customer_by_id(tbapi.get_id(cust))
        print(" cust...", end='')

    print(" ok")


class Customer:

    def __init__(self, name=None, address=None, address2=None, city=None, state=None, zip=None, country=None, email=None, 
                       phone=None, first_name=None, last_name=None, lat=None, lon=None, cust_id=None, device_id=None):
        self.name       = name
        self.address    = address
        self.address2   = address2
        self.city       = city
        self.state      = state
        self.postcode   = zip
        self.country    = country
        self.email      = email
        self.phone      = phone
        self.first_name = first_name
        self.last_name  = last_name
        self.lat        = lat
        self.lon        = lon
        self.cust_id    = cust_id
        self.device_id  = device_id
        self._tbapi     = None



    def load(self, tbapi, name):
        cust = tbapi.get_customer(name)
        dev  = tbapi.get_device_by_name(name)

        if cust is None or dev is None:
            return None

        cust_id   = tbapi.get_id(cust)
        device_id = tbapi.get_id(dev)

        first_name = ""
        last_name = ""

        print(cust)
        if "additionalInfo" in cust and cust["additionalInfo"] is not None:
            if "firstName" in cust["additionalInfo"]:
                first_name = cust["additionalInfo"]["firstName"]
            if "lastName"  in cust["additionalInfo"]:
                last_name  = cust["additionalInfo"]["lastName"]

        lat = None
        lon = None

        attribs = tbapi.get_server_attributes(dev)
        for attrib in attribs:
            if attrib["key"] == "latitude":
                lat = attrib["value"]
                break

        for attrib in attribs:
            if attrib["key"] == "longitude":
                lon = attrib["value"]
                break

        customer = Customer()
        customer._tbapi = tbapi      # Hold on to this for future reference
        customer.init(name, cust["address"], cust["address2"], cust["city"], cust["state"], cust["zip"], cust["country"], cust["email"], cust["phone"], first_name, last_name, lat, lon, cust_id, device_id)

        return customer


    def save(self):

        if self._tbapi is None:
            raise Exception("Please define tbapi!")

        if self.device_id is None or self.cust_id is None:
            raise Exception("Need to define device_id and cust_id!")

        name = None

        if self.first_name is not None:
            name = self.first_name

            if self.last_name is not None:
                name = self.first_name + " " + self.last_name

        additional_info = {}
        additional_info["firstName"] = self.first_name
        additional_info["lastName"]  = self.last_name

        if name is not None: 
            additional_info["description"]  = name


        self._tbapi.update_customer(self.cust_id, self.name, self.address, self.address2, self.city, self.state, self.postcode, self.country, self.email, self.phone, additional_info)

        server_attributes = {
            "latitude":  self.lat,
            "longitude": self.lon,
            "address":   self.one_line_address()
        }

        self._tbapi.set_server_attributes(self.device_id, server_attributes)



    def update(self, name=None, address=None, address2=None, city=None, state=None, postcode=None, country=None, email=None, phone=None, first_name=None, last_name=None, lat=None, lon=None):
        if name is not None:       self.name = name
        if address is not None:    self.address = address
        if address2 is not None:   self.address2 = address2
        if city is not None:       self.city = city
        if state is not None:      self.state = state
        if postcode is not None:   self.postcode = postcode
        if country is not None:    self.country = country
        if email is not None:      self.email = email
        if phone is not None:      self.phone = phone
        if first_name is not None: self.first_name = first_name
        if last_name is not None:  self.last_name = last_name
        if lat is not None:        self.lat = lat
        if lon is not None:        self.lon = lon

        # Did "location" change?  If it did, and lat/lon/zip were not specified, then let's automatically update those
        if address is not None or address2 is not None or city is not None or state is not None or country is not None:
            if lat is None or lon is None or postcode is None:

                results = geocode(self.address, self.address2, self.city, self.state, self.postcode, self.country)
                if results is not None:
                    if lat      is None: self.lat      = results["lat"]
                    if lon      is None: self.lon      = results["lon"]
                    if postcode is None: self.postcode = results["zip"]


    def init(self, name, address, address2, city, state, postcode, country, email, phone, first_name, last_name, lat, lon, cust_id, device_id):
        self.name       = name
        self.address    = address
        self.address2   = address2
        self.city       = city
        self.state      = state
        self.postcode   = postcode
        self.country    = country
        self.email      = email
        self.phone      = phone
        self.first_name = first_name
        self.last_name  = last_name
        self.lat        = lat
        self.lon        = lon
        self.cust_id    = cust_id
        self.device_id  = device_id


    def one_line_address(self):
        return self.address + ", " + ((self.address2 + ", ") if self.address2 is not None and self.address2 != "" else "") + self.city + ", " + self.state 


def get_cust(tbapi, name):
    cust = tbapi.get_customer(name)
    dev  = tbapi.get_device_by_name(name)

    if cust is None or dev is None:
        return None

    cust_id   = tbapi.get_id(cust)
    device_id = tbapi.get_id(dev)

    attribs = tbapi.get_server_attributes(dev)
    descr = None
    first_name = ""
    last_name = ""
    if "additionalInfo" in cust and "description" in cust["additionalInfo"]:
        descr = cust["additionalInfo"]["description"]

    if descr is not None:
        words = descr.split()

    if len(words) > 0:
        first_name = words[0]
    if len(words) > 1:
        last_name = words[1]

    lat = None
    lon = None

    for attrib in attribs:
        if attrib["key"] == "latitude":
            lat = attrib["value"]
            break

    for attrib in attribs:
        if attrib["key"] == "longitude":
            lon = attrib["value"]
            break

    return Customer(name, cust["address"], cust["address2"], cust["city"], cust["state"], cust["zip"], cust["country"], cust["email"], cust["phone"], first_name, last_name, lat, lon, cust_id, device_id)


''' Extract fields from a customer record '''
def get_cust_info(tbapi, name):
    cust = tbapi.get_customer(name)
    dev = tbapi.get_device_by_name(name)
    

    if cust is None or dev is None:
        return None

    cust_id   = tbapi.get_id(cust)
    device_id = tbapi.get_id(dev)

    attribs = tbapi.get_server_attributes(dev)
    lat = None
    lon = None

    for attrib in attribs:
        if attrib["key"] == "latitude":
            lat = attrib["value"]
            break

    for attrib in attribs:
        if attrib["key"] == "longitude":
            lon = attrib["value"]
            break

    return make_cust_info(name, cust["address"], cust["address2"], cust["city"], cust["state"], cust["zip"], cust["country"], cust["email"], cust["phone"], lat, lon, cust_id, device_id)


def make_cust_info(name, address, address2, city, state, postcode, country, email, phone, lat, lon, cust_id, device_id):
    cust_info = {}
    cust_info["name"]      = name
    cust_info["address"]   = address
    cust_info["address2"]  = address2
    cust_info["city"]      = city
    cust_info["state"]     = state
    cust_info["zip"]       = postcode
    cust_info["country"]   = country
    cust_info["email"]     = email
    cust_info["phone"]     = phone
    cust_info["lat"]       = lat
    cust_info["lon"]       = lon
    cust_info["cust_id"]   = cust_id
    cust_info["device_id"] = device_id

    return cust_info


def is_empty(s):
    ''' Returns true if a string is None or looks empty '''
    return s is None or s.strip() == ""


def geocode(address, address2, city, state, postcode, country):
    ''' Geocode an address; returns dict with lat, lon, and postcode '''

    if is_empty(address):
        print("Need an address to geocode!")
        return None

    geoaddress = address

    if not is_empty(address2):
        geoaddress += "," + address2

    if not is_empty(city):
        geoaddress += "," + city

    if not is_empty(state):
        geoaddress += "," + state

    if not is_empty(postcode):
        geoaddress += "," + postcode

    if not is_empty(country):
        geoaddress += "," + country


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


# location looks like this: {'place_id': '153499388', 'licence': 'Data (c) OpenStreetMap contributors, ODbL 1.0. http://www.openstreetmap.org/copyright', 'osm_type': 'way', 'osm_id': '367938871', 'boundingbox': ['45.4914508', '45.4915745', '-122.6234954', '-122.623365'], 'lat': '45.49151265', 'lon': '-122.623430224919', 'display_name': '3814, Southeast Cora Street, Creston-Kenilworth, Portland, Multnomah County, Oregon, 97202, United States of America', 'class': 'building', 'type': 'yes', 'importance': 0.421, 'address': {'house_number': '3814', 'road': 'Southeast Cora Street', 'suburb': 'Creston-Kenilworth', 'city': 'Portland', 'county': 'Multnomah County', 'state': 'Oregon', 'postcode': '97202', 'country': 'United States of America', 'country_code': 'us'}}
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
