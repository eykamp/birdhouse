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

# xpyright: strict

from deq_tools import StationRecord                                # pip install deq_tools
import deq_tools
import re
import os
import sys
import datetime
import time
# import pytz                                                       # pip install pytz
import logging
from dateutil import parser                                         # pip install python-dateutil
from thingsboard_api_tools import TbApi, Device                     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git@object-orientation --upgrade
from config import thingsboard_username, thingsboard_password, deq_logfile   # You'll need to create this... Be sure to gitignore it!
# from config import thingsboard_username, thingsboard_password   # You'll need to create this... Be sure to gitignore it!
import config
import birdhouse_utils
from typing import List, Any
# from dateutil.parser import parse


mothership_url = birdhouse_utils.make_mothership_url(config=config)
tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)

logging.basicConfig(filename=deq_logfile, format='%(asctime)s %(message)s', level=logging.INFO)    # WARN, INFO, DEBUG

# Data is stored as if it were coming from one of our devices

# earliest_ts = "2018/04/28T00:00"        # DEQ uses ISO datetime format: YYYY/MM/DDTHH:MM


# Mapping of DEQ keys to the ones we'll use for the same data
key_mapping = {
    # Their key            # Our key
    "PM2.5 Est":           "pm25",
    "Sensor A Pm2.5est":   "pm25",      # Humbolt PM key
    "Sensor B Pm2.5est":   "pm25",      # Humbolt PM key
    "Black Carbon":        "blackCarbon",
    "Ambient Temperature": "temperature",
    "Barometric Pressure": "pressure"
}

DEQ_KEY_FOR_CHECKING_LAST_TELEMETRY_DATE = "PM2.5 Est"
KEY_FOR_CHECKING_LAST_TELEMETRY_DATE = key_mapping[DEQ_KEY_FOR_CHECKING_LAST_TELEMETRY_DATE]


SECOND = 1000
MINUTE = 60 * SECOND
HOUR = 60 * MINUTE


def main():

    if len(sys.argv) > 0 and "--test" in sys.argv[1:]:
        test("DEQ (SEL)", 2, "2020/10/05T10:00", "2020/10/05T23:01")
        # test("DEQ (PCH)", 64, "2020/01/24T00:00", "2020/01/30T00:01")

    else:
        # This is the earliest timestamp we're interested in.  The first time we run this script, all data since this date will be imported.
        # Making the date unnecessarily early will make this script run very slowly.  Note also that if the date is too early, the DEQ server will
        # not respond properly.  Since we have different dates for different stations (depending on when they came online), we'll specify them below.
        # These dates were chosen by trial and error.

        # Note -- when adding an item here make sure the device has been defined on the server!
        get_data_for_station(2,  "DEQ (SEL)", "2018/04/28T00:00")   # 2  => SE Lafayette (SEL)               [latitude:45.496640911, longitude:-122.60287735]
        get_data_for_station(64, "DEQ (PCH)", "2018/04/28T00:00")   # 64 => Portland Cully Helensview (PCH)  [latitude:45.562203,    longitude:-122.575624]
        get_data_for_station(78, "DEQ (HUM)", "2019/06/28T00:00")   # 78 => Portland Humboldt (HUM)          [latitude:45.558081,    longitude:-122.670985]


def test(device_name: str, deq_station_id: int, from_time: str, to_time: str):
    """
    Grab some old data and compare it with what we already have in the database and make sure it matches.  Make sure that device_name
    and deq_station_id refer to the same thing.
    """
    print_if_console("Verifying data for %s..." % device_name)

    def make_ts(time_str: str) -> int:
        return int(datetime.datetime.strptime(time_str, "%Y/%m/%dT%H:%M").timestamp() * 1000)

    device = get_device(device_name)

    sb_telemetry = device.get_telemetry(KEY_FOR_CHECKING_LAST_TELEMETRY_DATE, make_ts(from_time), make_ts(to_time))
    deq_telemetry = deq_tools.get_data(deq_station_id, from_time, to_time)      # List[StationRecord]


    channel_num = None
    for channel_num, channel in enumerate(deq_telemetry[0].channels):
        if channel.name == DEQ_KEY_FOR_CHECKING_LAST_TELEMETRY_DATE:
            break

    assert channel_num is not None

    deq_data = {}
    for deq_record in deq_telemetry:
        deq_data[int(deq_record.datetime.timestamp() * 1000)] = deq_record.channels[channel_num].value


    # datetime.tzinfo
    for sb_record in sb_telemetry[KEY_FOR_CHECKING_LAST_TELEMETRY_DATE]:
        ts = sb_record["ts"]        # 1580284800000 == 2020-01-29T00:00:00
        sb_val = sb_record["value"]
        deq_val = deq_data[ts]

        if float(sb_val) != deq_val:
            print(sb_val, deq_val, ts, datetime.datetime.fromtimestamp(ts/1000))

    pass


def get_data_for_station(station_id: int, device_name: str, earliest_ts: str) -> None:
    print_if_console("Retrieving data for %s..." % device_name)

    device = get_device(device_name)

    start = time.time()

    # Date range for the data we're requesting from DEQ
    from_ts = get_from_ts(device, earliest_ts)           # Our latest and value, or earliest_ts if this is the inaugural run
    to_ts   = make_deq_date_from_ts(int(time.time() * 1000) + 24 * HOUR)    # 24 hours from now, to protect against running in other timezones

    # Fetch the data from DEQ
    print_if_console(f"\tRetrieving data from DEQ ({from_ts.replace('T', ' ')} - {to_ts.replace('T', ' ')})")
    try:
        station_records: List[StationRecord] = deq_tools.get_data(station_id, from_ts, to_ts)
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
            return

    print_if_console("\tUploading data to Sensorbot...")
    records = 0

    for station_record in station_records:

        outgoing_data = {}

        for channel in station_record.channels:
            # print(channel)
            # Assemble our payload, including translating names from DEQ's convention to ours
            # DEQ stations report a lot of data, but the only items we're interested in are those listed in key_mapping
            if channel.display_name in key_mapping and channel.valid:
                our_key = key_mapping[channel.display_name]
                outgoing_data[our_key] = channel.value

        if not outgoing_data:       # Will probably never happen
            continue

        records += 1
        ts = int(station_record.datetime.timestamp() * 1000)

        try:
            print_if_console(f"\r\tUploading {records} of {len(station_records)}", end="", flush=True)
            device.send_telemetry(outgoing_data, ts)
            time.sleep(.1)  # Throttle just a little
        except Exception as ex:
            logging.warning("Error sending telemetry (%s)" % outgoing_data)
            logging.warning(ex)
            raise

    print_if_console("")

    # Note that the DEQ now seems to be ignoring the time part of the requested timestamps, and is returning the entire day's worth of data.
    # This is not really a problem, because when inserting records with the same datestamp, the new data will overwrite the old, and no
    # duplicate records will be created.  It's just ugly.
    logging.info("Uploaded %s records for station %s in %s seconds" % (records, station_id, round(time.time() - start, 1)))
    print_if_console("Uploaded %s records in %s seconds" % (records, round(time.time() - start, 1)))


def make_date(deq_date: str) -> datetime.datetime:
    """
    Fix broken DEQ dates... 24:00? Really?
    """
    (month, day, year, hour, mins) = re.split("[/ :]", deq_date)
    if hour == "24":
        hour = "0"

    return datetime.datetime(int(year), int(month), int(day), int(hour), int(mins))


def print_if_console(msg: str, **kwargs: Any) -> None:
    """
    Print msg if we're running from the console
    """
    if os.isatty(sys.stdin.fileno()):
        print(msg, **kwargs)


def get_device(device_name: str) -> Device:
    device = tbapi.get_device_by_name(device_name)
    if not device:
        raise Exception("Could not find device " + device_name + " in Sensorbot database!")

    return device


# ts is epoch time in milliseconds
def make_deq_date_from_ts(ts: int) -> str:
    return datetime.datetime.fromtimestamp(ts / 1000).strftime("%Y/%m/%dT%H:%M")


def get_from_ts(device: Device, earliest_ts: str) -> str:
    """
    # This function returns the ts to use as the beginning of the data range in our data request to DEQ.  It will return the
    # ts for the most recently inserted DEQ data, or if data hasn't yet been inserted, it will return the value we set in
    # earliest_ts at the top of this file.  Returns date in 2020/11/02T04:34 format.
    # We add 1 minute to return time to avoid retrieving data we already have
    """
    telemetry = device.get_latest_telemetry(KEY_FOR_CHECKING_LAST_TELEMETRY_DATE)

    if telemetry[KEY_FOR_CHECKING_LAST_TELEMETRY_DATE][0]["value"] is None:      # We haven't stored any telemetry yet
        logging.info("First run!")
        return earliest_ts
    else:
        ts = telemetry[KEY_FOR_CHECKING_LAST_TELEMETRY_DATE][0]["ts"]           # Returns epoch time in ms
        assert(isinstance(ts, int))

        ts += 1 * MINUTE
        return make_deq_date_from_ts(ts)


if __name__ == "__main__":
    main()
