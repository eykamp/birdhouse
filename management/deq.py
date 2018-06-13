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
import datetime, time, pytz                                         # pip install pytz
import logging
import deq_tools                                                    # pip install deq_tools

from thingsboard_api_tools import TbApi                             # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from provision_config import motherShipUrl, username, password      # You'll need to create this!

tbapi = TbApi(motherShipUrl, username, password)

device_name = 'DEQ (SEL)'
deq_tz_name = 'America/Los_Angeles'

logging.basicConfig(filename="deq.log", format='%(asctime)s %(message)s', level=logging.INFO)    # WARN, INFO, DEBUG


# Data is stored as if it were coming from one of our devices
device = tbapi.get_device_by_name(device_name)
device_token = tbapi.get_device_token(tbapi.get_id(device))


# This is the earliest timestamp we're interested in.  The first time we run this script, all data since this date will be imported. 
# Making the date unnecessarily early will make this script run very slowly.
earliest_ts = "2018/04/28T00:00"        # DEQ uses ISO datetime format: YYYY/MM/DDTHH:MM

station_id = 2              # 2 => SE Lafayette, 7 => Sauvie Island, 51 => Gresham Learning Center.  See bottom of this file more more stations.


# Mapping of DEQ keys to the ones we'll use for the same data
key_mapping = {
    "PM2.5 Est": "pm25",
    "Ambient Temperature": "temperature",
    "Barometric Pressure": "pressure"
}


def main():

    start = time.time()
    now_ts = make_deq_date_from_ts(int(time.time() * 1000))    
    
    # Date range for the data we're requesting from DEQ
    from_ts = get_from_ts(device)           # Our latest and value, or earliest_ts if this is the inogural run
    to_ts   = now_ts

    # Fetch the data from DEQ
    data = deq_tools.get_data(station_id, from_ts, to_ts)
    records = 0

    for d in data:
        records += 1

        outgoing_data = {}

        for deq_key, our_key in key_mapping.items():
            if deq_key in data[d]:
                outgoing_data[our_key] = data[d][deq_key]          

        if len(outgoing_data) == 0:
            continue

        (month, day, year, hour, mins) = re.split('[/ :]', d)
        if(hour == '24'):
            hour = '0'

        pst = pytz.timezone(deq_tz_name)

        date_time = pst.localize(datetime.datetime(int(year), int(month), int(day), int(hour), int(mins)))
        ts = int(time.mktime(date_time.timetuple()) * 1000)

        try:
            tbapi.send_telemetry(device_token, outgoing_data, ts)
            time.sleep(1)  # Throttle
        except Exception as ex:
            logging.warning("Error sending telemetry (%s)" % outgoing_data)
            logging.warning(ex)

    # Note that the DEQ now seems to be ignoring the time part of the requested timestamps, and is returning the entire day's worth of data.
    # This is not really a problem, because when inserting records with the same datestamp, the new data will overwrite the old, and no
    # duplicate records will be created.  It's just ugly.
    logging.info("Uploaded %s records in %s seconds" % (records, round(time.time() - start, 1)))


# ts is in milliseconds
def make_deq_date_from_ts(ts):
    return datetime.datetime.fromtimestamp(ts / 1000).strftime('%Y/%m/%dT%H:%M')


# This function returns the ts to use as the beginning of the data range in our data request to DEQ.  It will return the
# ts for the most recently inserted DEQ data, or if data hasn't yet been inserted, it will return the value we set in 
# earliest_ts at the top of this file.
def get_from_ts(device):
    # Key used to determine last available telemetry -- this convoluted statement extracts the first value in our key_mapping dict
    # This is a somewhat arbitrary choice, but it will ensure we don't miss any data
    key = key_mapping[list(key_mapping.keys())[0]]        
    telemetry = tbapi.get_latest_telemetry(device, key)

    if telemetry[key][0]["value"] is None:      # We haven't stored any telemetry yet
        logging.info("First run!")
        ts = earliest_ts
    else:
        ts = make_deq_date_from_ts(telemetry[key][0]["ts"])

    return ts


main()
