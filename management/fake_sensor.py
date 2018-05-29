import paho.mqtt.client as mqtt
import json, time, random

from fake_sensor_config import host_name, port, device_token



def on_message(client, userdata, message):
    data = json.loads(str(message.payload.decode("utf-8")))
    # print(data["LED"])
    print("message received ", data)


def on_connect(client, userdata, flags, rc):
    print("Connected, subscribing to updates...")
    client.subscribe("v1/devices/me/attributes")


def on_disconnect(client, userdata, rc):
    print("Bye!")


def on_subscribe(client, obj, topic, granted_qos):
    print("Subscribed to " + str(topic) + " with QOS " + str(granted_qos))



# def on_message(mosq, obj, msg):
#     global message
#     print(msg.topic + " " + str(msg.qos) + " " + str(msg.payload))
#     message = msg.payload
#     client.publish("f2",msg.payload);

# def on_publish(mosq, obj, mid):
#     print("mid: " + str(mid))


# def on_log(mosq, obj, level, string):
#     print(string)

client = mqtt.Client()
client.username_pw_set(device_token, "")

# Assign event callbacks
# client.on_message = on_message
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_subscribe = on_subscribe
client.on_message = on_message 
# client.on_publish = on_publish





client.connect(host_name, port)
print("Connected!")


client.loop_start()
i = 0
while i < 100:

    temp = random.randint(10,80)
    data = {
        "temperature": temp
    }

    client.publish("v1/devices/me/telemetry", json.dumps(data))
    print("Publishing %s" %(json.dumps(data)))


    time.sleep(1)
    i += 1

client.loop_stop()
client.disconnect()
