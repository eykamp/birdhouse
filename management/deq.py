# Copyright 2018, Chris Eykamp

# MIT License

# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
# persons to whom the Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
# WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import requests
import json
import re
import datetime, time
import pytz
import sys

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi
from provision_config import motherShipUrl, username, password

tbapi = TbApi(motherShipUrl, username, password)
device_name = 'DEQ (SEL)'


device_token = tbapi.get_device_token(tbapi.get_id(tbapi.get_device_by_name(device_name)))
deq_tz_name = 'America/Los_Angeles'


from_ts = "2018/05/03T00:00"        # ISO datetime format: YYYY/MM/DDTHH:MM
to_ts   = "2019/05/09T23:59"
station_id = 2              # 2 => SE Lafayette, 7 => Sauvie Island, 51 => Gresham Learning Center.  See bottom of this file more more stations.



def main():

    data = get_deq_data(station_id, from_ts, to_ts)

    for d in data:
        pm25 = data[d]["PM2.5 Est"]
        temp = data[d]["Ambient Temperature"]
        pres = data[d]["Barometric Pressure"]
        outgoing_data = {"temperature" : temp, "pm25" : pm25, "pressure" : pres}

        (month, day, year, hour, mins) = re.split('[/ :]', d)
        if(hour == '24'):
            hour = '0'

        pst = pytz.timezone(deq_tz_name)

        date_time = pst.localize(datetime.datetime(int(year), int(month), int(day), int(hour), int(mins)))
        ts = int(time.mktime(date_time.timetuple()) * 1000)

        try:
            tbapi.send_telemetry(device_token, outgoing_data, ts)
        except Exception as ex:
            print("Error sending telemetry (%s)" % outgoing_data)
            raise(ex)


    print("Done")


''' 
resolution: 60 for hourly data, 1440 for daily averages.  Higher resolutions don't work, sorry, but lower-resolutions, such as 120, 180, 480, 720 will.
agg_method: These will *probably* all work: Average, MinAverage, MaxAverage, RunningAverage, MinRunningAverage, MaxRunningAverage, RunningForword, MinRunningForword, MaxRunningForword

'''
def get_deq_data(station_id, from_timestamp, to_timestamp, resolution=60, agg_method="Average"):
    station_url = "https://oraqi.deq.state.or.us/report/RegionReportTable"
    data_url    = "https://oraqi.deq.state.or.us/report/stationReportTable"
    count = 99999               # This should be greater than the number of reporting periods in the data range specified above
    # Get station data:
    # req = requests.get(station_url)

    # station_data = json.loads(req.text)["Data"]
    # print(json_response)
    # sys.exit()

    params = "Sid=" + str(station_id) + "&FDate=" + from_timestamp + "&TDate=" + to_timestamp + "&TB=60&ToTB=" + str(resolution) + "&ReportType=" + agg_method + "&period=Custom_Date&first=true&take="+ str(count) + "&skip=0&page=1&pageSize=" + str(count)

    req = requests.get(data_url + "?" + params)
    (status, reason) = (req.status_code, req.reason)

    json_response = json.loads(req.text)

    response_data = json_response["Data"]
    field_descr = json_response["ListDicUnits"]

    titles = {}
    units = {}

    for d in field_descr:
        name = d["field"]
        words = re.split('<br/>', d["title"])       # d["title"] ==> Wind Direction <br/>Deg
        titles[name] = words[0].strip()
        if len(words) > 1:
            units[name] = words[1].strip()
        else:
            units[name] = ""


    data = {}       # Restructured sensor data retrieved from DEQ

    for d in response_data:
        dt = d["datetime"]
        data[dt] = {}
        # print(d)
        for key, val in d.items():
            if key != "datetime":
                data[dt][titles[key]] = val

    return data

main()


'''
Station IDs from the stations file:
 1  ==> Tualatin Bradbury Court
 2  ==> Portland SE Lafayette
 7  ==> Sauvie Island
 8  ==> Beaverton Highland Park
 9  ==> Hillsboro Hare Field
 10 ==> Carus Spangler Road
 51 ==> Gresham Learning Center
 11 ==> Salem State Hospital
 12 ==> Turner Cascade Junior HS
 14 ==> Albany Calapooia School
 15 ==> Sweet Home Fire Department
 16 ==> Corvallis Circle Blvd
 56 ==> Amazon Park
 57 ==> Cottage Grove City Shops
 58 ==> Springfield City Hall
 59 ==> Delight Valley School
 60 ==> Willamette Activity Center
 61 ==> Wilkes Drive
 17 ==> Roseburg Garden Valley
 19 ==> Grants Pass Parkside School
 20 ==> Medford TV
 22 ==> Provolt Seed Orchard
 23 ==> Shady Cove School
 24 ==> Talent
 48 ==> Cave Junction Forest Service
 49 ==> Medford Welch and Jackson
 50 ==> Ashland Fire Department
 26 ==> Klamath Falls Peterson School
 27 ==> Lakeview Center and M
 28 ==> Bend Pump Station
 39 ==> Bend Road Department
 41 ==> Prineville Davidson Park
 42 ==> Burns Washington Street
 46 ==> John Day Dayton Street
 47 ==> Sisters Forest Service
 30 ==> Baker City Forest Service
 31 ==> Enterprise Forest Service
 32 ==> La Grande Hall and N
 33 ==> Pendleton McKay Creek
 37 ==> Hermiston Municipal Airport
 34 ==> The Dalles Cherry Heights School
 53 ==> The Dalles Wasco Library
 '''