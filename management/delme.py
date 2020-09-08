import birdhouse_utils
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade


from config import thingsboard_username, thingsboard_password

mothership_url = birdhouse_utils.make_mothership_url()
tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)



bhs = ["Birdhouse 001", "Birdhouse 002", "Birdhouse 003", "Birdhouse 004", "Birdhouse 005", "Birdhouse 006", "Birdhouse 007", "Birdhouse 008", "Birdhouse 009", "Birdhouse 010", "Birdhouse 011", "Birdhouse 012", "Birdhouse 013", "Birdhouse 014", "Birdhouse 015", "Birdhouse 016", "Birdhouse 017", "Birdhouse 018", "Birdhouse 019", "Birdhouse 020", "Birdhouse 021", "Birdhouse 022", "Birdhouse 023", "Birdhouse 024", "Birdhouse 025", "Birdhouse 027", "Birdhouse 028", "Birdhouse 029", "Birdhouse 030", "Birdhouse 033", "Birdhouse 035", "Birdhouse 036", "Birdhouse 037", "Birdhouse 038", "Birdhouse 039", "Birdhouse 041", "Birdhouse 042", "Birdhouse 044", "Birdhouse 046", "Birdhouse 047", "Birdhouse 048", "Birdhouse 090", "Birdhouse 100", "Birdhouse 101", "Birdhouse 102", "Birdhouse 103", "Birdhouse 104", "Birdhouse 105", "Birdhouse 106", "Birdhouse 107", "Birdhouse 108", "Birdhouse 109", "Birdhouse 110", "Birdhouse 111", "Birdhouse 112", "Birdhouse 113", "Birdhouse 114", "Birdhouse 115", "Birdhouse 116", "Birdhouse 117", "Birdhouse 118", "Birdhouse 119", "Birdhouse 120", "Birdhouse 121", "Birdhouse 122", "Birdhouse 123", "Birdhouse 125", "Birdhouse 126", "Birdhouse 170", "Birdhouse 171", "Birdhouse 172", "Birdhouse 173", "Birdhouse 174", "Birdhouse 175", "Birdhouse 176", "Birdhouse 177", "Birdhouse 178", "Birdhouse 179", "Birdhouse 180", "Birdhouse 181", "Birdhouse 182", "Birdhouse 999", "DEQ (PCH)", "DEQ (SEL)", "PurpleAir 1568", "PurpleAir 1606", "PurpleAir 2037", "PurpleAir 2043", "PurpleAir 2045", "PurpleAir 2053", "PurpleAir 2055", "PurpleAir 2057", "PurpleAir 2065", "PurpleAir 2317", "PurpleAir 2566", "PurpleAir 3233", "PurpleAir 3281", "PurpleAir 3357", "PurpleAir 3404", "PurpleAir 3519", "PurpleAir 3684", "PurpleAir 3707", "PurpleAir 3775", "PurpleAir 3786", ]

for bh in bhs:
    print(bh, end="", flush=True)
    device = tbapi.get_device_by_name(bh)
    print(" ok")
