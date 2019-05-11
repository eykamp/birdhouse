"""
copy_dash.py: Copy dashboard from one device to another

Usage:
        copy_dash.py <template> <device> <to>

Parameters:
    template    Source dash
    device      Source device
    to          Destination device
 """

from docopt import docopt                   # pip install docopt

import birdhouse_utils
import json
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from config import thingsboard_username, thingsboard_password
import config

# args = docopt(__doc__)
args = {
    "<template>": "004 Template",
    "<device>": "004",
}

template_dash = args["<template>"]
copy_to_pattern = "Birdhouse"      # All birdhoueses
# copy_to_pattern = "Birdhouse 018"  # Just this one


def main():

    mothership_url = birdhouse_utils.make_mothership_url(args, config)
    tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)


    print(f"Retrieving template dashboard {template_dash}... ", end='', flush=True)
    template_dash_def = tbapi.get_dashboard_definition(tbapi.get_dashboard_by_name(template_dash))

    # We also need the id of the device being swapped out
    template_device_id = tbapi.get_id(tbapi.get_device_by_name(birdhouse_utils.make_device_name(args["<device>"])))

    print(" done.")

    all_devices = tbapi.get_devices_by_name(copy_to_pattern)

    for device in all_devices:
        num = birdhouse_utils.get_device_number_from_name(device["name"])

        print(f"Updating dashboard for {device['name']}")
        dash_name_being_replaced = birdhouse_utils.make_dash_name(num)
        device_name = birdhouse_utils.make_device_name(num)
        device = tbapi.get_device_by_name(device_name)
        device_id = tbapi.get_id(device)

        # The dash we are replacing:
        dash_being_replaced = tbapi.get_dashboard_by_name(dash_name_being_replaced)
        dash_id_being_replaced = tbapi.get_id(dash_being_replaced)

        dash_def = tbapi.get_dashboard_definition(tbapi.get_dashboard_by_name(template_dash))    # dash_def will be modified
        birdhouse_utils.reassign_dash_to_new_device(dash_def, dash_name_being_replaced, template_device_id, device_id, device_name)

        dash_def["id"]["id"] = dash_id_being_replaced

        # del_humidity(dash_def)
        # exit()

        tbapi.save_dashboard(dash_def)


def del_humidity(dash_def):
    dash = Dashboard(dash_def)

    humidity = dash.find_widget("Humidity (%)")
    print(humidity)
    # print(dash.get_layout("main"))
    layout = dash.get_layout("main")
    print(layout.get_code())
    # print(layout.get_layouts())
    humidity_row = layout.get_layout_for(humidity).get_pos()[0]
    widgets_in_row = layout.get_widgets_in_row(humidity_row)
    widgets_in_row.remove(humidity.get_id())

    for widget in widgets_in_row:
        wl = layout.get_layout_for(widget)
        width, height = wl.get_size()
        row, col = wl.get_pos()
        wl.set_size(int(width * 3 / 2), height)
        wl.set_pos(row, int(col * 3 / 2))

    print(layout.get_code())
    dash.delete_widget(humidity)


    # for widget in widgets:
    #     print(widget.get_title(), widget.get_size())


# Dash > Layouts > WidgetLayouts
#                > Grid Settings
#      > Widgets


class GridSettings(object):
    def __init__(self, code):
        self.code = code

    def get_code(self):
        return self.code()

    def get_columns(self):
        return self.code.get("columns")


class Layout(object):
    def __init__(self, name, code):
        self.code = code
        self.name = name
        self.grid_settings = GridSettings(self.code.get("gridSettings", {}))
        self.widget_layouts = self._get_widget_layouts()

    def _get_widget_layouts(self):
        layouts = dict()
        for id, layout in self.code.get("widgets", {}).items():
            layouts[id] = WidgetLayout(layout)
        return layouts

    def get_code(self):
        code = self.code
        layout_code = dict()
        for id, wl in self.widget_layouts.items():
            layout_code[id] = wl.get_code()
        code["widgets"] = layout_code
        return code

    def get_name(self):
        return self.name

    def get_grid_settings(self):
        return self.grid_settings

    def get_layouts(self):
        return self.code["widgets"]

    def get_layout_for(self, widget):
        if type(widget) is Widget:
            widget_id = widget.get_id()
        elif type(widget) is str:
            widget_id = widget
        else:
            raise Exception(f"Unexpected object type {type(widget)}")
        return self.widget_layouts.get(widget_id)

    def get_widgets_in_row(self, row):
        found = list()
        for id, layout in self.widget_layouts.items():
            if layout.get_pos()[0] == row:
                found.append(id)
        return found


class WidgetLayout(object):
    def __init__(self, code):
        self.code = code        # {'sizeX': 8, 'sizeY': 5, 'mobileHeight': None, 'row': 10, 'col': 8}

    def get_code(self):
        return self.code

    def get_size(self):
        return (self.code.get("sizeX"), self.code.get("sizeY"))

    def set_size(self, width, height):
        self.code["sizeX"] = width
        self.code["sizeY"] = height

    def set_pos(self, row, col):
        self.code["row"] = row
        self.code["col"] = col

    def get_pos(self):
        return(self.code.get("row"), self.code.get("col"))


class Dashboard(object):
    def __init__(self, code):
        self.code = code
        self.widgets = self._get_widgets()
        self.layouts = self._get_layouts()


    def _get_widgets(self):
        widgets = list()
        for id, code in self.code.get("configuration", {}).get("widgets", {}).items():
            widgets.append(Widget(id, code))
        return widgets

    def _get_layouts(self):
        layouts = list()
        for name, code in self.code.get("configuration", {}).get("states", {}).get("default", {}).get("layouts", {}).items():
            layouts.append(Layout(name, code))
        return layouts

    def get_layout_names(self):
        names = list()
        for layout in self.layouts:
            names.append(layout.get_name())
        return names

    def get_layouts(self):
        return self.layouts

    def get_layout(self, name):
        for layout in self.layouts:
            if layout.get_name() == name:
                return layout
        return None

    def get_widgets(self):
        return self.widgets

    def get_code(self):
        code = self.code
        widget_code = dict()
        for widget in self.widgets:
            widget_code[widget.get_id] = widget.get_code()

        code["configuration"]["widgets"] = widget_code


    def find_widget(self, name):
        for widget in self.widgets:
            if widget.get_title() == name:
                return widget
        return None

    def delete_widget(self, Widget):
        # Delete the widget from the widget list
        # Delete the widget from the layout
        pass


class Widget(object):

    def __init__(self, id, code):
        self.id = id
        self.code = code

    def get_id(self):
        return self.id

    def get_config(self):
        return self.code.get("config", {})

    def get_title(self):
        return self.get_config().get("title")

    def get_size(self):
        return (self.code.get("sizeX"), self.code.get("sizeY"))

    def get_code(self):
        return self.code


if(__name__ == "__main__"):
    main()
