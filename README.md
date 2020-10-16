# Blind Control

This program allows you to control motorized RF blinds. Such as the BOFU EY2512.

It's built on top of the work done by akirjavainen here: https://github.com/akirjavainen/markisol

The program uses [Homie ESP8266](https://github.com/homieiot/homie-esp8266) to manage the WiFi and the Homie protocol via MQTT. For this to work, you'll need an MQTT broker running such as http://mosquitto.org/ .

I also have a [Blind Manager](https://github.com/sillyfrog/blindsmanager) program designed to integrate this with Home Assistant that will add the blinds as "covers" to Home Assistant, and take care of translating Home Assistant commands to the controller (and vice versa).

## Setup and installation

It's designed to be built using [PlatformIO](https://platformio.org/) and installed on to an ESP8266 chip (I use the D1 mini with 4MB of flash), and have an 433MHz controller attached. The transmitter connects to D5 (GPIO 14) and (optionally) the receiver to D6 (GPIO 12). I use a transmitter/receiver pair such as [this](https://www.aliexpress.com/item/2024422377.html).

Optionally, a SHT3x temperature sensor can be connected to D1 and D2 (GPIO 5 and 4), I used a [SHT31](https://www.aliexpress.com/item/4000186501433.html).

To disable receiving and/or the temperature sensor, comment out the relevant `#define` in main.cpp.

Once the main program is flashed, upload the data directory, this is done by running:

```
platformio run -t uploadfs
```

For myself, I needed to run the correct instance of `platformio` as I have several installed from over the years I'm yet to clean up, so the command was: `~/.platformio/penv/bin/platformio run -t uploadfs`

## First boot

Once installed, upon the first boot, the device will go into AP mode. Connect to this and run through the setup process (entering WiFi details). It should then reboot, connect to WiFi and your MQTT broker.

## Using the controller

The device runs a small web server, you can browse to, and it will give you an interface such as this:
![Web GUI UI Screenshot](https://raw.githubusercontent.com/sillyfrog/homie_blind_control/master/images/blindcontrol.png)

From here you can select or enter the remote ID, and press a button to send that command. If you have the receiver attached, pressing button on a remote next to the receiver should cause it to broadcast a message to MQTT, and start to populate the dropdown with previously seen remote ID's. Each remote has a unique ID, which is paired with the blind motor during the initial setup. Each remote can then have up to 15 channels.

If you don't have an existing remote, just make up a 4 digit hex number, and use this, you can optionally also select a different channel for each blind motor (rather than using a different hex number). You can then pair the ESP Remote with the blind controller using the onscreen buttons.

## Controlling blinds

Once everything is setup, you can then control the blinds using MQTT commands.

To send a command to a blind, use the following MQTT topic:
`homie/{topic}/command/send/set`
With a value in the format:
`{command},{blindid}[,{channel}]`
Where `command` is the command to send, one of:

- DOWN
- UP
- STOP
- LIMIT
- DIRECTION
- PAIR

The `{blindid}` is a 4 character hex number (with an optional `0x` at the start). `{channel}` is the optional channel on the remote (defaulting to 1). If the special channel `0` is used, that means all channels for that specific `blindid`.

For example, to open a blind, run the following command:

```
mosquitto_pub -t homie/test-blinds/command/send/set -m down,748f,1
```

You can also subscribe to events to see what's messages have been recived. For the example above, there would be a corresponding message with the topic `homie/test-blinds/command/received`, and a payload of `DOWN,748f,1` (as there is a receiver right next to the transmitter, this message will "loop back" to itself).
