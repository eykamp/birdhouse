from thingsboard_api_tools import TbApi  # sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
import birdhouse_utils

import config

public_user_id = "59b9f4e0-9d90-11e7-ba9a-5108c78814ce"



def main():

    # dash_url = "http://www.sensorbot.org:8080/dashboard/8634f4f0-6457-11e8-a60d-9d9c1f00510b?publicId=59b9f4e0-9d90-11e7-ba9a-5108c78814ce"
    # http://www.sensorbot.org:8080/dashboards/8634f4f0-6457-11e8-a60d-9d9c1f00510b


    base_url = config.base_url
    thingsboard_username = config.thingsboard_username
    thingsboard_password = config.thingsboard_password
    mothership_url = birdhouse_utils.make_mothership_url(base_url)



    tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)
    print(tbapi.get("/api/auth/user", ""))
    exit()

    dev = tbapi.get_device_by_name('Birdhouse 018')
    print(dev["customerId"])
    print(tbapi.is_public_device(dev))
    print(public_user_id)
    print(dev)

    # print(pub_id)
    # print(is_public_device(tbapi, dev))



# def is_public_device(tbapi, device):
#     public_user_id = tbapi.get_public_user_uuid()
    

#     if dashboard["assignedCustomers"] is None:
#         return False

#     for c in dashboard["assignedCustomers"]:
#         if tbapi.get_id(c) == pub_id:
#             return True

#     return False



main()
