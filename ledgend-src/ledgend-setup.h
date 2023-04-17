#ifndef LEDGEND_SETUP
#define LEDGEND_SETUP

// Wifi settings
const char* ssid = "<ssid>";
const char* password = "<password>";

// if there is significant package loss you can try to allow this
// but usually ~900-1000 packets/second should be fine
// which is around 5000-5500 leds @ 30fps
#define FASTLED_ALLOW_INTERRUPTS 0

// this buffer is used to prevent that frame data is not overridden before it
// is actually written to led strips first. this will smoothen out stuff if wifi
// burst in lots of packets very fast.
#define INCOMING_DMX_RINGBUFFER_SIZE 3

#endif