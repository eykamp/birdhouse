"""
get_token.py: Retrieve secret token for device

Usage:
        get_token.py <num>...  

Parameters:
    num                 Device number
 """

from docopt import docopt                   # pip install docopt

import birdhouse_utils
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from config import thingsboard_username, thingsboard_password

args = docopt(__doc__)
# print(args)

def main():

    mothership_url = birdhouse_utils.make_mothership_url(args)
    tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)

    for num in args["<num>"]:
        print(f"Retrieving details for device {num}... ", end='', flush=True)
        name = birdhouse_utils.make_device_name(num)
        device = tbapi.get_device_by_name(name)

        if device is None:
            print(f"Failed.\nCould not find device {num}... Aborting.")
            exit()

        token = tbapi.get_device_token(device)
        print("done.")
        print(token)


main()
