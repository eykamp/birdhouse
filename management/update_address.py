import birdhouse_utils
from thingsboard_api_tools import TbApi    # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from config import thingsboard_username, thingsboard_password
import config
import time

mothership_url = birdhouse_utils.make_mothership_url(None, config)
tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)




# This to be passed in
# birdhouse_number = 9
# address = "xx NE 24th Ave."
# address2 = None
# city = "Portland"
# state = "OR"
# country = "United States"
# email = "xxx@gmail.com"
# first_name = "xx"
# last_name = "xx"
# zip = None
# lat = None
# lon = None

my_devices = [1, 2, 3, 4, 5, 10, 11, 12, 13, 15]
known_addr_devices = [9, 18, 25, 46, 123, 182, 105, 106, 107, 108, 109, 122, 123, 125]
clear_names = [6, 7, 8, 14, 16, 17, 19, 20, 21, 22, 23, 24, 27, 29, 30, 33, 35, 36, 37, 38, 39, 41, 42, 44, 47, 48, 90, 100, 101, 102, 103, 104, 110, 111, 113, 121,  127, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 999]


def changeAddress(birdhouse_number, address=None, address2=None, city=None, state=None, zip=None, country=None, email=None, phone="", first_name=None, last_name=None, ):
    name = birdhouse_utils.make_device_name(birdhouse_number)
    device = tbapi.get_device_by_name(name)
    assert device
    cust = tbapi.get_customer(name)
    if cust is None:
        print("Cannot find customer for birdhouse " + str(birdhouse_number) + "!")
        exit()

    if address != "???":
        latlonzip = birdhouse_utils.geocode(address, address2, city, state, zip, country)
        tbapi.set_server_attributes(device, {"latitude": latlonzip["lat"], "longitude": latlonzip["lon"]})
    else:
        latlonzip = {"zip": ""}

    tbapi.update_customer(cust, address=address, address2=address2, city=city, state=state, country=country, email=email, description=first_name + " " + last_name, phone=phone, zip=latlonzip["zip"], additional_info={"firstName": first_name, "lastName": last_name, "description": first_name + " " + last_name})



def get_active_devices():
    clear_names = []

    birdhouses = tbapi.get_devices_by_name("Birdhouse")

    for birdhouse in birdhouses:
        tel = tbapi.get_latest_telemetry(birdhouse, "freeHeap")
        if tel["freeHeap"][0]["value"]:
            ts = tel["freeHeap"][0]["ts"]

            num = int(birdhouse_utils.get_device_number_from_name(birdhouse["name"]))
            if num in my_devices:
                status = "2101"
            elif num in known_addr_devices:
                status = "known"
            else:
                status = "unknown"

            # print(birdhouse["name"], status, time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ts / 1000)))
        else:
            print("fake:", birdhouse["name"])
            clear_names.append(birdhouse)
        pass

    return clear_names

# Need to be able to mark devices out of service


# get_active_devices()

def clear():
    for num in clear_names:
        name = birdhouse_utils.make_device_name(num)
        device = tbapi.get_device_by_name(name)
        assert device
        cust = tbapi.get_customer(name)
        assert cust

        tbapi.update_customer(cust, address="Undeployed Device", address2="", city="", state="", country="", email="", description="Undeployed Device", phone="", zip="", additional_info={"description":"Undeployed Device"})

        tbapi.delete_server_attributes(device, ["latitude", "longitude"])
        # exit()

changeAddress(24,  address="217 S Seymour Ct",      first_name="Callie",     last_name="Meiners",       city="Portland", state="OR", country="United States", email="photocallie@gmail.com", phone="(817) 716-9870")


# changeAddress(1,   address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
# clear()
exit()
# First remove fake contact info from clear_names

changeAddress(2,   address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(3,   address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(4,   address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(5,   address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(10,  address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(11,  address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(12,  address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(13,  address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")
changeAddress(15,  address="2101 SE Tibbetts",      first_name="Chris",     last_name="Eykamp",       city="Portland", state="OR", country="United States", email="chris@sensorbot.org")

# May 17, 2019
changeAddress(9,   address="23 NE 24th Ave",        first_name="Mark",      last_name="Hageman",      email="hagemanm@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(18,  address="4315 SE 16th Ave",      first_name="Wes",       last_name="Ward",         email="wesleytward@comcast.net", city="Portland", state="OR", country="United States")
changeAddress(25,  address="3806 SE 21st Ave",      first_name="Christine", last_name="Denkewalter",  email="cdenke@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(46,  address="3433 SE 21st Ave",      first_name="April",     last_name="Jamison",      email="aprilcjamison@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(123, address="6825 SE Pine Court",    first_name="Mary",      last_name="McWilliams",   email="marymcwilliams61@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(182, address="1836 SE 42nd Ave",      first_name="Mark",      last_name="Wolochuk",     email="mwolochuk@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(105, address="1716 SE Ash Street",    first_name="Karla",     last_name="Zimmerman",    email="kjzimm48@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(106, address="5818 SE Mason",         first_name="Deborah",   last_name="Buckley",      email="sweetwaterhouseplants@yahoo.com", city="Portland", state="OR", country="United States")
changeAddress(107, address="4155 SE Ivon St",       first_name="Jan",       last_name="Snyder",       email="janiceliza@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(108, address="4451 SE Knapp St",      first_name="Jordan",    last_name="Anderson",     email="jla@uchicago.edu", city="Portland", state="OR", country="United States")
changeAddress(109, address="908 SE Cora",           first_name="Don",       last_name="Stephens",     email="shreddad@mac.com", city="Portland", state="OR", country="United States")
changeAddress(125, address="2018 SE Ladd Ave",      first_name="Linda",     last_name="Nettekoven",  email="linda@lnettekoven.com", city="Portland", state="OR", country="United States")

changeAddress(122, address="???",          first_name="Ronda",     last_name="Royal",     email="calicocousin@gmail.com", city="Portland", state="OR", country="United States")

# Known, get from spreadsheet
changeAddress(120, address="???",       first_name="Chris",     last_name="Weiss",     email="chris.weiss@gmail.com", city="Portland", state="OR", country="United States")
changeAddress(121, address="???",       first_name="Justin",    last_name="???",       email="benfleskes@gmail.com", city="Portland", state="OR", country="United States")
# changeAddress(126, address="",          first_name="James",     last_name="Wright",     email="xxxx", city="Portland", state="OR", country="United States")
changeAddress(127, address="???",          first_name="Aaron",     last_name="Arutunian",     email="aaronarutunian@gmail.com", city="Portland", state="OR", country="United States")


# changeAddress(121, address="3216 SE 25th",          first_name="Justin",    last_name="?",     city="Portland", state="OR", country="United States")



# Active, no address yet
# changeAddress(107, address="1716 SE Ash Street",    name="Karla Zimmerman"       city="Portland", state="OR", country="United States")
# changeAddress(108, address="1716 SE Ash Street",    name="Karla Zimmerman"       city="Portland", state="OR", country="United States")




