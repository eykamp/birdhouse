#!/usr/bin/env python
import json
import web   # sudo pip3 install git+https://github.com/webpy/webpy#egg=web.py

# pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
# sudo pip install git+git://github.com/eykamp/thingsboard_api_tools.git --upgrade
import thingsboard_api_tools as tbapi

from redlight_greenlight_config import motherShipUrl, username, password, data_encoding


tbapi.set_mothership_url(motherShipUrl)


urls = (
    '/', 'set_led_color',
)

app = web.application(urls, globals())



class set_led_color:        
    def POST(self):
        # Decode request data

        print("Received data: ", web.data().decode(data_encoding))
        incoming_data = json.loads(str(web.data().decode(data_encoding)))

        temperature = incoming_data["temperature"]
        device_id = incoming_data["device_id"]

        if temperature < 50:
            color = 'GREEN'
        elif temperature < 80:
            color = 'YELLOW'
        else:
            color = 'RED'

        outgoing_data = {"LED": color}

        token = tbapi.get_token(username, password)
        tbapi.set_shared_attributes(token, device_id, outgoing_data)

        return "OK"


if __name__ == "__main__":
    app.run()
