# Local copy of arduinoWebSockets 2.6.1

Copied from https://github.com/Links2004/arduinoWebSockets (tag 2.6.1),
licensed under the LGPL 2.1 (see LICENSE).

One change, in `src/WebSockets.h`: the 15 KB limit on a received message,
`WEBSOCKETS_MAX_DATA_SIZE`, can now be raised with a build flag. The car
screen's `platformio.ini` raises it, because the server's `vehicles:update`
messages carry road shapes and can be larger than 15 KB. With the
original limit, the library drops the connection when one arrives.
