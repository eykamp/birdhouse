from thingsboard_api_tools import TbApi         # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

import birdhouse_utils

from provision_config import motherShipUrl, username, password, dashboard_template_name, sensor_type

birdhouse_number = "002"



tbapi = TbApi(motherShipUrl, username, password)


print(birdhouse_utils.geocode("2101 SE TIbbetts", None, "Portland", "OR", "97202", "USA"))
exit()


def main():
    device_name = birdhouse_utils.make_device_name(birdhouse_number)
    calibrationData={"periodStart":1525287600000, "periodEnd":1525978800000, "reference":"DEQ"}
    start_ts = calibrationData["periodStart"]
    end_ts   = calibrationData["periodEnd"]

    device = tbapi.get_device_by_name(device_name)
    reference_device = tbapi.get_device_by_name("DEQ")

    print(device)

    telemetry = get_telemetry(device, start_ts, end_ts)
    reference = get_telemetry(reference_device, start_ts, end_ts)
    print(reference)


def get_telemetry(device, start_ts, end_ts):
    return tbapi.get_telemetry(device, "plantowerPM25conc", start_ts, end_ts)
#self, device, telemetry_keys, startTime=None, endTime=None, interval=None, limit=None, agg=None):
main()


# calibrationData={"periodStart":1525287600000, "periodEnd":1525978800000, "reference":"DEQ"}
# birdhouse 002