import birdhouse_utils
from birdhouse_utils import Customer
from thingsboard_api_tools import TbApi    # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from config import motherShipUrl, username, password, dashboard_template_name, default_device_local_password, wifi_ssid, wifi_password, esptool_exe_location

tbapi = TbApi(motherShipUrl, username, password)




# This to be passed in
# birdhouse_number = 9
# address = "23 NE 24th Ave."
# address2 = None
# city = "Portland"
# state = "OR"
# country = "United States"
# email = "hagemanm@gmail.com"
# first_name = "Mark"
# last_name = "Hageman"
# postcode = None
# lat = None
# lon = None




def changeAddress(birdhouse_number, address=None, address2=None, city=None, state=None, postcode=None, country=None, email=None, phone=None, first_name=None, last_name=None, lat=None, lon=None):
    name = birdhouse_utils.make_device_name(birdhouse_number) 
    cust = Customer.load(tbapi, name)
    if cust is None:
        print("Cannot find customer for birdhouse " + str(birdhouse_number) + "!")
        exit()
    cust.update(address=address, address2=address2, city=city, state=state, postcode=postcode, country=country, email=email, first_name=first_name, last_name=last_name, lat=lat, lon=lon)
    cust.save()


# changeAddress(1,  "6926 SE Yamhill St",     city="Portland", state="OR", country="United States")
# changeAddress(2,  "4819 SW STONEBROOK CT",  city="Portland", state="OR", country="United States")
# changeAddress(3,  "205 NE 79TH AVE",        city="Portland", state="OR", country="United States")
# changeAddress(4,  "2743 NE 137TH AVE",      city="Portland", state="OR", country="United States")
# changeAddress(5,  "4007 NE 22ND AVE",       city="Portland", state="OR", country="United States")
# changeAddress(6,  "330 NW BRYNWOOD LN",     city="Portland", state="OR", country="United States")
# changeAddress(7,  "1851 SE EXETER DR",      city="Portland", state="OR", country="United States")
# changeAddress(8,  "917 SW WASHINGTON ST",   city="Portland", state="OR", country="United States")
# changeAddress(10, "2356 NW WESTOVER RD",    city="Portland", state="OR", country="United States")
# changeAddress(11, "2541 N TERRY ST",        city="Portland", state="OR", country="United States")
# changeAddress(12, "4804 SE SHERMAN ST",     city="Portland", state="OR", country="United States")
# changeAddress(13, "6020 SW ORCHID DR",      city="Portland", state="OR", country="United States")
# changeAddress(14, "2810 NE 32ND AVE",       city="Portland", state="OR", country="United States")
# changeAddress(15, "11919 N JANTZEN DR",     city="Portland", state="OR", country="United States")
# changeAddress(16, "3021 NE 29TH AVE",       city="Portland", state="OR", country="United States")
# changeAddress(17, "3024 NE M L KING BLVD",  city="Portland", state="OR", country="United States")
# changeAddress(18, "3931 SE 140TH PL",       city="Portland", state="OR", country="United States")
# changeAddress(19, "8004 N CHICAGO AVE",     city="Portland", state="OR", country="United States")
# changeAddress(20, "5831 SE TENINO ST",      city="Portland", state="OR", country="United States")
# changeAddress(21, "5308 NE MULTNOMAH ST",   city="Portland", state="OR", country="United States")
# changeAddress(22, "6302 SE 128TH AVE",      city="Portland", state="OR", country="United States")
# changeAddress(23, "1445 NW 26TH AVE",       city="Portland", state="OR", country="United States")
# changeAddress(24, "5800 NE COLUMBIA BLVD",  city="Portland", state="OR", country="United States")
# changeAddress(25, "2344 NE 9TH AVE",        city="Portland", state="OR", country="United States")

# changeAddress(27, "734 SE 6TH AVE",         city="Portland", state="OR", country="United States")
# changeAddress(28, "6612 N KERBY AVE",       city="Portland", state="OR", country="United States")
# changeAddress(29, "5774 N VANCOUVER AVE",   city="Portland", state="OR", country="United States")
# changeAddress(30, "2137 E BURNSIDE ST",     city="Portland", state="OR", country="United States")

changeAddress(33, "9844 SW 6TH AVE",        city="Portland", state="OR", country="United States")
changeAddress(35, "6922 SE WOODSTOCK BLVD", city="Portland", state="OR", country="United States")
changeAddress(36, "3725 NE LIBERTY TER",    city="Portland", state="OR", country="United States")
changeAddress(37, "7828 N WASHBURNE AVE",   city="Portland", state="OR", country="United States")
changeAddress(38, "2309 N KERBY AVE",       city="Portland", state="OR", country="United States")






