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
import os
import sys
import datetime
import time
import pytz
import logging
import deq_tools                                                    # pip install deq_tools
from dateutil import parser                                         # pip install python-dateutil
from tenacity import retry, stop_after_attempt, wait_fixed          # pip install tenacity

from thingsboard_api_tools import TbApi                             # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
from config import motherShipUrl, username, password, deq_logfile   # You'll need to create this... Be sure to gitignore it!

tbapi = TbApi(motherShipUrl, username, password)

deq_tz_name = "US/Pacific"

logging.basicConfig(filename=deq_logfile, format='%(asctime)s %(message)s', level=logging.INFO)    # WARN, INFO, DEBUG

# Data is stored as if it were coming from one of our devices

# This is the earliest timestamp we're interested in.  The first time we run this script, all data since this date will be imported.
# Making the date unnecessarily early will make this script run very slowly.
earliest_ts = "2018/04/28T00:00"        # DEQ uses ISO datetime format: YYYY/MM/DDTHH:MM


# Mapping of DEQ keys to the ones we'll use for the same data
key_mapping = {
    # Their key            # Our key
    "PM2.5 Est":           "pm25",
    "Black Carbon":        "blackCarbon",
    "Ambient Temperature": "temperature",
    "Barometric Pressure": "pressure"
}

key_for_checking_last_telemetry_date = "pm25"


def main():
    # Note -- when adding an item here make sure the device has been defined on the server!
    get_data_for_station(2,  "DEQ (SEL)")   # 2  => SE Lafayette (SEL)               [latitude:45.496640911, longitude:-122.60287735]
    get_data_for_station(64, "DEQ (PCH)")   # 64 => Portland Cully Helensview (PCH)  [latitude:45.562203,    longitude:-122.575624]


# This fails a lot, so we'll try tenacity
@retry(stop=stop_after_attempt(7), wait=wait_fixed(10), reraise=True)
def get_data(station_id, from_ts, to_ts):
    result = deq_tools.get_data(station_id, from_ts, to_ts)
    return result


def get_data_for_station(station_id, device_name):
    print_if_ssh("Retrieving data for %s..." % device_name)

    device = tbapi.get_device_by_name(device_name)
    device_token = tbapi.get_device_token(tbapi.get_id(device))

    start = time.time()

    # Date range for the data we're requesting from DEQ
    from_ts = get_from_ts(device)           # Our latest and value, or earliest_ts if this is the inaugural run
    to_ts   = make_deq_date_from_ts(int(time.time() * 1000) + 1000 * 60 * 60 * 24)    # Add 24 hours to protect against running in other timezones

    # Fetch the data from DEQ
    print_if_ssh("Connecting to DEQ...")
    try:
        data = get_data(station_id, from_ts, to_ts)
    except Exception as ex:
        logging.warning("Error retrieving data")
        logging.warning(ex)

        # Swallow exception until things have been down awhile... DEQ servers fail from time to time.  Pretty regularly, actually.
        time_since_last_data = datetime.datetime.fromtimestamp(time.time()) - parser.parse(from_ts)  # gives timedelta
        if time_since_last_data > datetime.timedelta(hours=12):
            logging.error("Persistent error retrieving data (%s)" % str(time_since_last_data))

            raise ex
        else:
            logging.info("DEQ connection failure; last data %s / %s / %s" % (time_since_last_data, from_ts, to_ts))

    print_if_ssh("Uploading data to Sensorbot...")
    records = 0

    # We need to do this in date-sorted order so that when we're done, our most recent date is in ts_kv_latest
    sorted_keys = sorted(data, key=make_date)      # Returns a sorted list of datetimes that are the keys to the dict that we got from deq_tools.get_data()
    
    pst = pytz.timezone(deq_tz_name)

    for key in sorted_keys:
        records += 1

        outgoing_data = {}

        for deq_key, our_key in key_mapping.items():
            if deq_key in data[key]:
                outgoing_data[our_key] = data[key][deq_key]

        if len(outgoing_data) == 0:
            continue

        date_time = pst.localize(make_date(key))
        ts = int((date_time - datetime.datetime(1970, 1, 1, tzinfo=pytz.utc)).total_seconds() * 1000)

        try:
            tbapi.send_telemetry(device_token, outgoing_data, ts)
            time.sleep(1)  # Throttle
        except Exception as ex:
            logging.warning("Error sending telemetry (%s)" % outgoing_data)
            logging.warning(ex)

    # Note that the DEQ now seems to be ignoring the time part of the requested timestamps, and is returning the entire day's worth of data.
    # This is not really a problem, because when inserting records with the same datestamp, the new data will overwrite the old, and no
    # duplicate records will be created.  It's just ugly.
    logging.info("Uploaded %s records for station %s in %s seconds" % (records, station_id, round(time.time() - start, 1)))
    print_if_ssh("Uploaded %s records in %s seconds" % (records, round(time.time() - start, 1)))


def make_date(deq_date):
    """
    Fix broken DEQ dates... 24:00? Really?
    """
    (month, day, year, hour, mins) = re.split("[/ :]", deq_date)
    if hour == "24":
        hour = "0"

    return datetime.datetime(int(year), int(month), int(day), int(hour), int(mins))


def print_if_ssh(msg):
    """
    Print msg if we're running from the console
    """
    if os.isatty(sys.stdin.fileno()):
        print(msg)


# ts is in milliseconds
def make_deq_date_from_ts(ts):
    return datetime.datetime.fromtimestamp(ts / 1000).strftime("%Y/%m/%dT%H:%M")


# This function returns the ts to use as the beginning of the data range in our data request to DEQ.  It will return the
# ts for the most recently inserted DEQ data, or if data hasn't yet been inserted, it will return the value we set in
# earliest_ts at the top of this file.
def get_from_ts(device):
    # Key used to determine last available telemetry -- this convoluted statement extracts the first value in our key_mapping dict
    # This is a somewhat arbitrary choice, but it will ensure we don't miss any data
    telemetry = tbapi.get_latest_telemetry(device, key_for_checking_last_telemetry_date)

    if telemetry[key_for_checking_last_telemetry_date][0]["value"] is None:      # We haven't stored any telemetry yet
        logging.info("First run!")
        ts = earliest_ts
    else:
        ts = make_deq_date_from_ts(telemetry[key_for_checking_last_telemetry_date][0]["ts"])

    return ts


if __name__ == "__main__":
    main()
