"""
create_paperwork.py: Create paperwork for devices

Usage:
        create_paperwork.py <num>...  [--baseurl=URL]

Parameters:
    num                 Device number

Options:
    --baseurl URL       Base URL of Sensorbot Thingsboard installation (do not include http:// prefix!) (e.g. 'www.sensorbot.org')
 """

import tempfile
import subprocess
import os
import re
from lxml import etree
from copy import deepcopy
from docopt import docopt                   # pip install docopt
from urllib.parse import urlencode
from urllib.request import urlopen

import birdhouse_utils
from thingsboard_api_tools import TbApi     # pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade

from config import thingsboard_username, thingsboard_password


template_folder = "c:/dev/birdhouse/management"
output_folder   = "c:/dev/birdhouse/management"

nameplate_template_file = os.path.join(template_folder, "nameplate_template.svg")
ownership_form_file     = os.path.join(template_folder, "ownership_form.svg")

label_pdf_basename = os.path.join(output_folder, "label")         # Will have _{pagenum}.pdf appended to it
forms_pdf_basename = os.path.join(output_folder, "form")          # Will have _{pagenum}.pdf appended to it

INKSCAPE = r"c:\Program Files\Inkscape\inkscape.exe"        # Full path to inkscape binary (for converting SVGs to PDFs)

ELEMENT_HEIGHT = 470
ELEMENTS_PER_PAGE = 2


args = docopt(__doc__)


def main():
    params = make_params(args["<num>"])

    print("Generating PDF documents... ", end='', flush=True)
    pages = generate_pdfs(params)
    print("Ok")
    pagestr = str(pages).replace('\\\\', '\\')
    print(f"Generated {len(pages)} PDF documents in folder {pagestr}.")


def make_params(nums):
    mothership_url = birdhouse_utils.make_mothership_url(args)

    tbapi = TbApi(mothership_url, thingsboard_username, thingsboard_password)

    params = []

    for num in nums:
        print(f"Retrieving details for device {num}... ", end='', flush=True)
        device_name = birdhouse_utils.make_device_name(num)
        dash_name = birdhouse_utils.make_dash_name(num)
        
        device = tbapi.get_device_by_name(device_name)
        dash = tbapi.get_dashboard_by_name(dash_name)
        dash_url = tbapi.get_public_dash_url(dash)
        tiny_url = make_tiny_url(dash_url)

        if device is None:
            print(f"Failed.\nCould not find device {num}... Aborting.")
            exit()

        token = tbapi.get_device_token(device)

        params.append((
            birdhouse_utils.get_sensor_type(num)[1], 
            birdhouse_utils.make_device_number(num), 
            token,
            tiny_url
        ))
        print("done.")

    return params


def make_tiny_url(url):
    request_url = "http://tinyurl.com/api-create.php?" + urlencode({"url": url})
    with urlopen(request_url) as response:
        return response.read().decode("utf-8")


def generate_pdfs(params):
    return generate_forms(params) + generate_labels(params)


def generate_forms(params):
    # We will print two items per page for now -- how many pages will we be making altogether?
    pages = len(params)
    doclist = list()

    for page in range(pages):
        # Start afresh for each sheet
        parser = etree.XMLParser(remove_blank_text=True)
        doc = etree.parse(ownership_form_file, parser)

        # Grabs the first <g> element
        template_element = doc.xpath('//svg:g', namespaces={'svg': 'http://www.w3.org/2000/svg'})[0]

        # Update our elements
        change_device_name(template_element, params[page][0])
        change_device_number(template_element, params[page][1])
        change_device_key(template_element, params[page][2])
        change_dash_url(template_element, params[page][3])

        tmpfile = get_temp_file()                   # For an intermediate copy of our SVG
        outfile = forms_pdf_basename + f'_{page}.pdf'     # For the final copy of our PDF
        doclist.append(outfile)

        # Convert with Inkscape
        try:
            doc.write(tmpfile)

            subprocess.call([INKSCAPE, "--without-gui", "--export-area-page", f"--export-pdf={outfile}", f"--file={tmpfile}"])
        finally:
            os.remove(tmpfile)

    return doclist


def generate_labels(params):
    # We will print two items per page for now -- how many pages will we be making altogether?
    pages = calc_num_pages(len(params), ELEMENTS_PER_PAGE)
    doclist = list()

    for page in range(pages):
        # Start afresh for each sheet
        parser = etree.XMLParser(remove_blank_text=True)
        doc = etree.parse(nameplate_template_file, parser)

        # Grabs the first <g> element
        template_element = doc.xpath('//svg:g', namespaces={'svg': 'http://www.w3.org/2000/svg'})[0]

        elements = [template_element]

        # We'll have between 1 and ELEMENTS_PER_PAGE elements on this page
        first_element = page * ELEMENTS_PER_PAGE
        last_element = min((page + 1) * ELEMENTS_PER_PAGE, len(params)) - 1
        elements_on_this_page = last_element - first_element + 1

        # Template already has one element, so we get the first on free; we need to create copies for anh additional elements we need
        for i in range(1, elements_on_this_page):
            new_element = create_nudged_copy(template_element, f'layer{i}', (0, ELEMENT_HEIGHT * i))
            elements.append(new_element)        # Add it to our list of elements
            doc.getroot().append(new_element)   # And add it to the XML document tree

        # We need to do this for all our elements, even the original copy from the template
        for i in range(elements_on_this_page):
            # Update our elements
            change_device_name(elements[i],   params[first_element + i][0])
            change_device_number(elements[i], params[first_element + i][1])
            change_device_key(elements[i],    params[first_element + i][2])
            change_dash_url(elements[i],      params[first_element + i][3])


        tmpfile = get_temp_file()                   # For an intermediate copy of our SVG
        outfile = label_pdf_basename + f'_{page}.pdf'     # For the final copy of our PDF
        doclist.append(outfile)

        # Convert with Inkscape
        try:
            doc.write(tmpfile)

            subprocess.call([INKSCAPE, "--without-gui", "--export-area-page", f"--export-pdf={outfile}", f"--file={tmpfile}"])
        finally:
            os.remove(tmpfile)

    return doclist


def calc_num_pages(elements, elements_per_page):
    if elements == 0:   # It won't...
        return 0

    return int((elements - 1) / elements_per_page) + 1


def create_nudged_copy(elem, id, nudge_amount):
    copy = deepcopy(elem)
    trans = copy.attrib["transform"]      # transform="translate(1487.9414,-1538.7065)"
    m = re.search(r"translate\((-?\d+\.?\d*),(-?\d+\.?\d*)\)", trans)
    x = float(m.group(1)) + nudge_amount[0]
    y = float(m.group(2)) + nudge_amount[1]

    copy.attrib["transform"] = f"translate({x},{y})"
    return copy


def changeElement(doc, from_text, to_text):
    els = [element for element in doc.getiterator() if element.text == from_text]
    for el in els:
        el.text = to_text


def change_device_name(doc, name):
    changeElement(doc, "Birdhouse", name)


def change_device_number(doc, number):
    changeElement(doc, "XXX", number)


def change_dash_url(doc, dash_url):
    changeElement(doc, "YYY", dash_url)


def change_device_key(doc, device_key):
    changeElement(doc, "ABCDEFGHIJKLMNOPQRST", device_key)


def get_temp_file():
    f = tempfile.NamedTemporaryFile(delete=False)
    f.close()

    return f.name



main()
