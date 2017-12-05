import geopy        # pip install geopy

# Import some geocoders
from geopy.geocoders import Nominatim
from geopy.geocoders import Bing
from geopy.geocoders import GoogleV3


# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi


from provision_config import motherShipUrl, username, password, google_geocoder_api_key, bing_geocoder_api_key, dashboard_template_name, sensor_type


# This to be passed in
cust_name = "Hank Williams 3"
cust_address = "4000 SE 39th"
cust_address2 = None
cust_city = "Portland"
cust_state = "OR"
cust_zip = None     # Will be populated by geocoder if empty
cust_country = "USA"
cust_email = "hank@email.com"
cust_phone = "555-1212"
cust_lat = None     # Will be populated by geocoder if empty
cust_lon = None     # Will be populated by geocoder if empty



def main():
    tbapi = TbApi(motherShipUrl, username, password)

    # Get a definition of our template dashboard
    template_dash = tbapi.get_dashboard_by_name(dashboard_template_name)
    dash_def = tbapi.get_dashboard_definition(tbapi.get_id(template_dash))

    # Lookup missing fields, such as zip, lat, and lon
    update_customer_data()

    if cust_lat is None or cust_lon is None:
        print("Must have valid lat/lon in order to add device!")
        exit(1)

    # Create new customer and device records on the server
    customer = tbapi.add_customer(cust_name, cust_address, cust_address2, cust_city, cust_state, cust_zip, cust_country, cust_email, cust_phone)


    server_attributes = {
        "latitude": cust_lat,
        "longitude": cust_lon
    }

    shared_attributes = {
        "LED": "Unknown",
        "nonce": 0
    }
    device = tbapi.add_device(make_device_name(cust_name), sensor_type, shared_attributes, server_attributes)


    # Upate the dash def. to point at the device we just created (modifies dash_def)
    update_dash_def(dash_def, cust_name, tbapi.get_id(device))

    # Create a new dash with our definition, and assign it to the new customer    
    dash = tbapi.create_dashboard_for_customer(cust_name + ' Dash', dash_def)
    tbapi.assign_dash_to_user(tbapi.get_id(dash), tbapi.get_id(customer))
    

    # input("Press Enter to continue...")

    tbapi.delete_customer_by_id(tbapi.get_id(customer))
    tbapi.delete_device(tbapi.get_id(device))
    tbapi.delete_dashboard(tbapi.get_id(dash))



    # device_id = tbapi.get_id(device)

    # assign_device_to_public_user(token, device_id)

    # userdata = get_users(token)
    # print(userdata)

    # print(get_customer(token, 'Art'))
    # print(get_public_user_uuid(token))
    # delete_customer_by_name(token, 'Hank Williams 3')



'''
''
''
''
'''

''' Modifies dash_def '''
def update_dash_def(dash_def, customer_name, device_id):
    aliases = dash_def["configuration"]["entityAliases"].keys()
    for a in aliases:
        dash_def["configuration"]["entityAliases"][a]["alias"] = make_device_name(customer_name)
        dash_def["configuration"]["entityAliases"][a]["filter"]["singleEntity"]["id"] = device_id



def make_device_name(customer_name):
    return customer_name + ' Device'



def update_customer_data():    
    global cust_zip, cust_lat, cust_lon

    location = geocode(cust_address, cust_address2, cust_city, cust_state, cust_zip, cust_country)

    if location is not None:
        # Update customer data with cocoder data
        if cust_zip == "" or cust_zip == None:
            cust_zip = location["zip"]
        if cust_lat == "" or cust_lat == None:
            cust_lat = location["lat"]
        if cust_lon == "" or cust_lon == None:
            cust_lon = location["lon"]
    else:
        if cust_zip == "" or cust_zip == None:
            print("Need a zip code to proceed")
            exit(1)
        if cust_lat == "" or cust_lat == None or cust_lon == "" or cust_lon == None:
            print("Need a lat/lon to proceed")
            exit(1)


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


main()        