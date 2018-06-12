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

import re
import datetime, time
import pytz
import sys
import deq_tools

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from thingsboard_api_tools import TbApi
from provision_config import motherShipUrl, username, password

tbapi = TbApi(motherShipUrl, username, password)
device_name = 'DEQ (SEL)'
deq_tz_name = 'America/Los_Angeles'


device_token = tbapi.get_device_token(tbapi.get_id(tbapi.get_device_by_name(device_name)))


from_ts = "2018/05/03T00:00"        # ISO datetime format: YYYY/MM/DDTHH:MM
to_ts   = "2019/05/09T23:59"
station_id = 2              # 2 => SE Lafayette, 7 => Sauvie Island, 51 => Gresham Learning Center.  See bottom of this file more more stations.



def main():

    data = deq_tools.get_data(station_id, from_ts, to_ts)

    for d in data:

        outgoing_data = {}

        if "PM2.5 Est"           in data[d]:
            outgoing_data["pm25"] = data[d]["PM2.5 Est"]          
            
        if "Ambient Temperature" in data[d]:
            outgoing_data["temperature"] = data[d]["Ambient Temperature"]
            
        if "Barometric Pressure" in data[d]:
            outgoing_data["pressure"] = data[d]["Barometric Pressure"]

        if len(outgoing_data) == 0:
            continue

        (month, day, year, hour, mins) = re.split('[/ :]', d)
        if(hour == '24'):
            hour = '0'

        pst = pytz.timezone(deq_tz_name)

        date_time = pst.localize(datetime.datetime(int(year), int(month), int(day), int(hour), int(mins)))
        ts = int(time.mktime(date_time.timetuple()) * 1000)

        try:
            print("Sending", outgoing_data)
            tbapi.send_telemetry(device_token, outgoing_data, ts)
        except Exception as ex:
            print("Error sending telemetry (%s)" % outgoing_data)
            raise(ex)


    print("Done")


main()
