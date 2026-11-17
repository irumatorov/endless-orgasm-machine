# Endless Orgasm Machine
**An automated orgasm denial and enhancement device for the DIY community**

### It knows...
What will surprise you most is how accurate it is that you only have a few moments left.  In this day and age algorithms are a respected part of our lives...this one knows you better than yourself.

Have you ever cut-off a sneeze before it happened and found it a conflicting blend of sensations?  Have you ever stopped before your orgasm so you could last a bit longer?  If you're not afraid to use a buttplug and you're ready for an amazing experience this is for you.  

It is not a sex-toy in the traditional sense because it doesn't provide pleasure unless you connect it to other toys.  It is an arousal sensor.  Other devices provide the pleasure, EOM tells them when to stop and when they can start again. (The exception being if you build in an integrated vibrator or e-stim plug)

### The science!
Using an inflatable butt plug to detect pressure changes indicative of pelvic floor contractions, this software, and associated hardware, is used to detect when the user is approaching orgasm and control stimulation accordingly. The net result: automated edging and orgasm denial or enhancement.

### But I like orgasms...

Having to wait for it while being right at the edge makes it more intense!  Trying to resist getting close is a turn-on for some.  When you're ready to end the edging you can use "Control: Orgasm" to allow you to finish after a chosen number of minutes or near-orgasms... if you've had enough simply set it to "Control: Manual" and take control of the toys manually.  It can also be used after orgasms to determine exactly when the refractory period has subsided enough to begin the next round of pleasuring. 

### Bonus feature:
You could use the app by itself to control multiple sex toys at once without am EOM being involved.  The app by itself is a versatile and simple control hub.

## Web UI

![Screenshot](doc/Screenshot3.png)

The UI connects to the EOM in one of two ways:

- **WiFi**: boards with 8MB or more flash serve the UI themselves. With the default settings the
  EOM starts a network called `EOM`. Join it and open http://192.168.4.1, or
  http://eom.local once it is on your own network.
- **Bluetooth**: open the UI from GitHub Pages or a local `npm run dev`, click the Bluetooth
  icon and pick `Libotoy`. Web Bluetooth works in Chrome and Edge on desktop and Android.

The UI can also drive other toys through [Buttplug.io](https://buttplug.io), following the
pleasure level.

## Setup

1. Install [VS Code](https://code.visualstudio.com/) with the
   [pioarduino IDE](https://marketplace.visualstudio.com/items?itemName=pioarduino.pioarduino-ide)
   extension and [Node.js](https://nodejs.org/) 20 or newer, then open this folder.
2. Copy `data/config.json.example` to `data/config.json` and edit it, see
   [doc/Config.json.md](doc/Config.json.md). Set `wifi_on` to true to use the web UI.
3. Pick your board in the PlatformIO toolbar. Supported environments are in
   [platformio.ini](platformio.ini), which also sets the sensor and LED pins.
4. In `eomui`, run `npm install` and `npm run build`. This writes the UI to `data/ui`.
5. Under *PlatformIO > your board > Platform*, run *Upload Filesystem Image*, then under
   *General* run *Upload and Monitor*.

For a new board, copy an environment in `platformio.ini` and change the pins.

## Hardware

See [doc/building/building.md](doc/building/building.md) for wiring and example builds. STLs and
the [OpenSCAD](https://openscad.org/) source for the tubing plug are in [hardware](hardware).

You can try the firmware on a bare board: touching the sensor pin is enough to move the readings.

### Pleasure modes

|ID|Name|Description|
|---|---|---|
|1|Ramp-Stop|Pleasure ramps up, stopping abruptly on arousal threshold crossing.  It's very frustrating.|
|2|Depletion (default)|Pleasure ramps up, but is reduced for even small arousal spikes.  This makes for a constant interaction with your arousal rather than just a sudden cutoff when it's over the limit|
|3|Enhancement|This is the opposite of edging, it will make you orgasm faster.  Pleasure ramps up as arousal increases, holding a peak for ramp_time.  Not compatible with Orgasm mode.|

### XToys
Using Xtoys you can connect to the EOM and let it control your other Xtoys devices.  For Xtoys it emulates a device called [MonsterPub Mr. Devil Kegel](https://monsterpub.com/products/mp2-u-shaped-remote-egg-with-kegel).  Connect to the device called "Libotoy".  The "pressure" reading corresponds to the "pleasure" output of the EOM.  The sliders for vibrator strength will work in manual mode.

XToys sees the pleasure level as the Mr. Devil's pressure reading. Its vibration control sets the
pleasure level while the EOM is in manual mode and is ignored in the automatic modes.

<small>Why Mr Devil?  Because it's one of the few bluetooth devices in Xtoys that has 2 way communication and high resolution (16 bit).</small>

## Protocols

The WebSocket and Bluetooth interfaces are described in [doc/Protocol.md](doc/Protocol.md).

## Relation to the Edge-o-Matic and Nogasm

This is a fork of the Edge-o-Matic 3000 with the goal of keeping hardware and software simple
and DIY friendly, like the original Nogasm. It drops the SD card, menu, console and wired
accessory support, and adds Bluetooth, an orgasm mode with a denial limit, and a phone friendly UI.

## Standing on the shoulders of giants
This is a product of the open source community.  This will always be open source.  Sharing knowledge this way goes back to patent offices, or perhaps cave-man fire.  Sometimes the term can be abused by making things appear open when key information is obfuscated or by taking formerly open repositories offline...that practice of monetizing is also as old as time.  No shame.  Open-source doesn't mean no profiting and I'll try to make some money too. However this particular project intends to focus on information sharing and DIY accessibility as much as functionality.  You should be always be able to build this yourself if you have the free time.  That may not be the most profitable route but I hope the open-source community can elevate this project and make it even more useful than I am capable of.

## Contribution and Inspiration from:

- **Rhobot** with the original [nogasm project](https://github.com/nogasm/nogasm) that started it all
- **TheElims** Gave feedback and works on [a similar device with much more slick hardware](https://github.com/skier233/SmartAssPlug-development) - 
- **Skier23** who curates a [repository](https://github.com/Edging-Machines/Edging-Machines) and [discord](https://discord.gg/EvYbZBf) with all information regarding the topic.
- [Buttplug.io by Qdot](https://buttplug.io/) - Powers the connections from the EOM app to other bluetooth toys.  Him and his discord team are very helpful if irritable.
- **Rudolf**, who came up with the [coiled silicone tube](https://www.thingiverse.com/thing:3543727) as sensor element and even has written a [scientific paper](https://www.degruyter.com/document/doi/10.1515/pjbr-2020-0014/pdf) on that topic.
- **Maus-Tec** who manufactures and sells the [Edge-O-Matic](https://github.com/MausTec/edge-o-matic-3000)
- **Onwrikbaar** with his bullfrog, who refined the simple [nogasm algorithm](https://github.com/Onwrikbaar/Bullfrog) with a peak summation and decay function.
- **night-howler** who contributed a breadboard friendly version of the nogasm called [protogasm](https://github.com/night-howler/protogasm).
