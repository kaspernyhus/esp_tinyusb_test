# Test project for additional TinyUSB functionality for esp-idf projects

### New features in component:
* USB Audio
* USB Network

in addition to the following from the esp-idf implementation:
* USB Serial Device (CDC-ACM) with optional Virtual File System support
* Input and output streams through USB Serial Device
* Other USB classes (MIDI, MSC, HID…) support directly via TinyUSB
* VBUS monitoring for self-powered devices


NB: Work in progress!

#### TCP Server test
Listening on port 1234. To test run:
```
$ nc 10.0.0.1 1234
```

#### Websocket server test
To test run:
```
$ websocat ws://10.0.0.1/ws
```
