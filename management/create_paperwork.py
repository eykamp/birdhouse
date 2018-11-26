import tempfile
import subprocess
import os
import re
from lxml import etree
from copy import deepcopy


nameplate_template_file = r'c:\dev\birdhouse\management\nameplate_template.svg'
outfile_base = r'c:\dev\birdhouse\management\test'          # Will have _{pagenum}.pdf appended to it

inkscape = r'c:\Program Files\Inkscape\inkscape.exe'        # Full path to inkscape binary (for converting SVGs to PDFs)

element_height = 235
elements_per_page = 4


params = [("Bott1", "001", "XXXXXXXXXXXXXXXXXXXX"), ("Bott1", "2222", "LO"*5), ("Bott1", "3", "MMMMMMMMMM"),
            ("Bott1", "44-44", "XOXO" * 5), ("Bott1", "55555", "Y"*20), ("Bott1", "666", "8*8*"*5), ("Bott1", "001", "XXXXXXXXXXXXXXXXXXXX"), ("Bott1", "2222", "LO"*5), ("Bott1", "3", "MMMMMMMMMM"),
            ("Bott1", "44-44", "XOXO" * 5), ("Bott1", "55555", "Y"*20), ("Bott1", "666", "8*8*"*5), ("Bott1", "$$$", "8*8*"*5)]


def main():
    pages = generate_pdfs(params)
    print(f"Generated {pages} PDF documents")


def generate_pdfs(params):
    pages = calc_num_pages(len(params), elements_per_page)

    for page in range(pages):
        # Start afresh for each sheet
        parser = etree.XMLParser(remove_blank_text=True)
        doc = etree.parse(nameplate_template_file, parser)

        # Grabs the first <g> element
        template_element = doc.xpath('//svg:g', namespaces={'svg': 'http://www.w3.org/2000/svg'})[0]

        elements = [template_element]

        # We'll have between 1 and elements_per_page elements on this page
        first_element = page * elements_per_page
        last_element = min((page + 1) * elements_per_page, len(params)) - 1
        elements_on_this_page = last_element - first_element + 1

        # Template already has one element, so we get the first on free; we need to create copies for anh additional elements we need
        for i in range(1, elements_on_this_page):
            new_element = create_nudged_copy(template_element, f'layer{i}', (0, element_height * i))
            elements.append(new_element)        # Add it to our list of elements
            doc.getroot().append(new_element)   # And add it to the XML document tree

        # We need to do this for all our elements, even the original copy from the template
        for i in range(elements_on_this_page):
            # Update our elements
            change_device_name(elements[i],   params[first_element + i][0])
            change_device_number(elements[i], params[first_element + i][1])
            change_device_key(elements[i],    params[first_element + i][2])

        tmpfile = get_temp_file()                   # For an intermediate copy of our SVG
        outfile = outfile_base + f'_{page}.pdf'     # For the final copy of our PDF

        # Convert with Inkscape
        try:
            doc.write(tmpfile)

            subprocess.call([inkscape, "--without-gui", "--export-area-page", f"--export-pdf={outfile}", f"--file={tmpfile}"])
        finally:
            os.remove(tmpfile)

    return pages


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
    f = [element for element in doc.getiterator() if element.text == from_text][0]
    f.text = to_text


def change_device_name(doc, name):
    changeElement(doc, "Birdhouse", name)


def change_device_number(doc, number):
    changeElement(doc, "XXX", number)


def change_device_key(doc, device_key):
    changeElement(doc, "ABCDEFGHIJKLMNOPQRST", device_key)


def get_temp_file():
    f = tempfile.NamedTemporaryFile(delete=False)
    f.close()

    return f.name



main()
