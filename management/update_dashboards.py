import json
import os
import re
import subprocess
import requests                             # pip install requests
import tempfile
import time
import esptool                              # pip install esptool
from docopt import docopt                   # pip install docopt
from types import SimpleNamespace
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from asciimatics.screen     import Screen   # pip install aciimatics
from asciimatics.scene      import Scene
from asciimatics.exceptions import ResizeScreenError, NextScene, StopApplication
from asciimatics.widgets    import Frame, Layout, Divider, Text, Button, Label, RadioButtons, TextBox  #, Widget, ListBox
from asciimatics.effects import Matrix

from functools import partial
from timeit import default_timer as timer

import birdhouse_utils

import threading
import traceback

# We don't want to fail in the absence of a config file -- most users won't actually need one
try:
    import config
except ModuleNotFoundError:
    config = {}

template_name = "018 Template"
target = 4

args = None

thingsboard_username = config.thingsboard_username if 'thingsboard_username' in dir(config) else None
thingsboard_password = config.thingsboard_password if 'thingsboard_password' in dir(config) else None


def main():
    mothership_url = birdhouse_utils.make_mothership_url(args, config)

    tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)

    template_dash = tbapi.get_dashboard_by_name(template_name)

    if template_dash is None:
        print(f"Could not find dash definition for {template_name}")
        exit()

    dash_def = tbapi.get_dashboard_definition(template_dash)
    print(dash_def)

    device_name = birdhouse_utils.make_device_name(target)
    device = tbapi.get_device_by_name(device_name)

    update_dash_def(dash_def, device_name, tbapi.get_id(device))
    out = open(r"c:\temp\xxx.json", "w")

    # j = json.loads(str(dash_def))
    json.dump(dash_def, out)

    out.close()


def update_dash_def(dash_def, device_name, device_id):
    """ Modifies dash_def """
    aliases = dash_def["configuration"]["entityAliases"].keys()
    for a in aliases:
        try:
            dash_def["configuration"]["entityAliases"][a]["alias"] = device_name
            if "singleEntity" in dash_def["configuration"]["entityAliases"][a]["filter"]:
                dash_def["configuration"]["entityAliases"][a]["filter"]["singleEntity"]["id"] = device_id
        except Exception as e:
            print('Alias: %s\n dash_def["configuration"]["entityAliases"][a]["filter"]: %s' % (a, dash_def["configuration"]["entityAliases"][a]["filter"]))
            raise e


if __name__ == "__main__":
    main()
