import birdhouse_utils


import sys

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi


from provision_config import motherShipUrl, username, password, dashboard_template_name, sensor_type


# This to be passed in
birdhouse_number = "019"
cust_name = "Birdhouse " + birdhouse_number


cust_info = {}

cust_info["address"]  = "5824 SE Lafayette"
cust_info["address2"] = ""
cust_info["city"]     = "Portland"
cust_info["state"]    = "OR"
cust_info["zip"]      = None     # Will be populated by geocoder if empty
cust_info["country"]  = "USA"
cust_info["lat"]      = None     # Will be populated by geocoder if empty
cust_info["lon"]      = None     # Will be populated by geocoder if empty
cust_info["email"]    = "chris@sensorbot.org"
cust_info["phone"]    = "555-1212"



def main():
    cleanup = False      # If true, deletes everything that is created

    tbapi = TbApi(motherShipUrl, username, password)

    # Get a definition of our template dashboard
    template_dash = tbapi.get_dashboard_by_name(dashboard_template_name)
    if template_dash is None:
        print("Cannot retrieve template dash %s.  Is that the correct name?" % dashboard_template_name)
        sys.exit()

    dash_def = tbapi.get_dashboard_definition(tbapi.get_id(template_dash))

    # Lookup missing fields, such as zip, lat, and lon
    birdhouse_utils.update_customer_data(cust_info)

    if cust_info["lat"] is None or cust_info["lon"] is None:
        print("Must have valid lat/lon in order to add device!")
        exit(1)

    # Create new customer and device records on the server
    customer = tbapi.add_customer(cust_info["name"], cust_info["address"], cust_info["address2"], cust_info["city"], cust_info["state"], cust_info["zip"], cust_info["country"], cust_info["email"], cust_info["phone"])

    server_attributes = {
        "latitude": cust_info["lat"],
        "longitude": cust_info["lon"],
        "address": birdhouse_utils.one_line_address(cust_info)
    }

    shared_attributes = {
        "LED": "Unknown"
    }

    device = tbapi.add_device(birdhouse_utils.make_device_name(birdhouse_number), sensor_type, shared_attributes, server_attributes)
    device_id = tbapi.get_id(device)

    
    # We need to store the device token as a server attribute so our REST services can get access to it
    device_token = tbapi.get_device_token(device_id)

    server_attributes = {
        "device_token": device_token
    }

    tbapi.set_server_attributes(device_id, server_attributes)

    try:
        # Upate the dash def. to point at the device we just created (modifies dash_def)
        update_dash_def(dash_def, cust_info["name"], device_id)
    except Exception as ex:
        print("Exception encountered: Cleaning up...")
        tbapi.delete_device(device_id)
        tbapi.delete_customer_by_id(tbapi.get_id(customer))
        raise ex


    # Create a new dash with our definition, and assign it to the new customer    
    dash = tbapi.create_dashboard_for_customer(cust_info["name"] + ' Dash', dash_def)
    tbapi.assign_dash_to_user(tbapi.get_id(dash), tbapi.get_id(customer))
    
    print("Device token for Birdhouse " + birdhouse_number + " (set device token: setparams?deviceToken=" + device_token + ")")

    if cleanup:
        # input("Press Enter to continue...")   # Don't run from Sublime with this line enabled!!!

        print("Cleaning up! (device token rendered invalid)")
        tbapi.delete_dashboard(tbapi.get_id(dash))
        tbapi.delete_device(device_id)
        tbapi.delete_customer_by_id(tbapi.get_id(customer))



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
        try:
            dash_def["configuration"]["entityAliases"][a]["alias"] = birdhouse_utils.make_device_name(birdhouse_number)
            if "singleEntity" in dash_def["configuration"]["entityAliases"][a]["filter"]:
                dash_def["configuration"]["entityAliases"][a]["filter"]["singleEntity"]["id"] = device_id
        except Exception as e:
            print('Alias: %s\n dash_def["configuration"]["entityAliases"][a]["filter"]: %s' % (a, dash_def["configuration"]["entityAliases"][a]["filter"]))
            raise e

main()        