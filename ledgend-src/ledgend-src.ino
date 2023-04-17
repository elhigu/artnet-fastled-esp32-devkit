/*
This example will receive multiple universes via Art-Net and control a strip of
WS2812 LEDs via the FastLED library: https://github.com/FastLED/FastLED
This example may be copied under the terms of the MIT license, see the LICENSE file for details

Modified to handle 4 parallel writes to separate strips and to work with ~1000 leds for each channel @ 30fps
also modified algorith to not prevent writing data to leds if there has been lost packages and moved
UDP reading to core 0 to prevent FastLED write from causing packet drop when led 
write operation takes over 5-10ms

TODO: refactor the whole DMX handling to separate .h / .cpp files
      and use the ringbuffer for syncing data read with fasteled output
    
TODO: if multiple controllers are near each other use BT LE to sync frame writing timers
      every now and then
*/

#include <ArtnetWifi.h>
#include <Arduino.h>

#include "ledgend-setup.h"

#include <FastLED.h>

// TODO: add stats about jitter how stable frames are coming in and written out.
struct Stats {
  uint64_t count_start_time;
  uint64_t packets;

  uint64_t packets_late; // count up if old packet of the same frame comes in
  uint64_t packets_lost; // count up if expected packet universe number is skipped
  uint64_t packets_total;
  
  uint64_t led_show_count;
  uint64_t led_show_usec;

  uint64_t dmx_data_copy_count;
  uint64_t dmx_data_copy_usec;

  uint64_t idle_packets;
  uint64_t dmx_packets;
  uint64_t poll_packets;
  uint64_t sync_packets;
};

Stats stats = { 0 };

// LED settings
const int numLeds = 600;

// using array didn't work for template parameter (could have asked from chatbot how to write macro for this but bleh)
const byte dataPin1 = 15;
const byte dataPin2 = 2;
const byte dataPin3 = 4;
const byte dataPin4 = 16;

CRGB leds[numLeds*4];

// Art-Net settings
ArtnetWifi artnet;

// lets read wifi UPD packets with other core, because writing leds may 
// take like 20-40 ms which seems to cause packet drop if udp.read() is 
// not called every couple of milliseconds
TaskHandle_t ReadDmx_core_0_Task;

// connect to wifi – returns true if successful or false if not
bool ConnectWifi(void)
{
  bool state = true;
  int i = 0;

  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.println("Connecting to WiFi");

  // Wait for connection
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if (i > 20)
    {
      state = false;
      break;
    }
    i++;
  }
  if (state)
  {
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("");
    Serial.println("Connection failed.");
  }

  return state;
}

/**
 * Handles reading DMX frame and converting it to led color codes
 *
 * TODO: check if we need a separate buffer (e.g. ring buffer), where this
 *       writed incoming data before it will be copied to actual frame buffers
 *       that fastled uses to write led state
 */

byte frame_ready = false; // signal that we can send frame

void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  static uint16_t next_universe = 0;
  stats.packets_total++;

  if (universe < next_universe) {
    frame_ready = true; // always mark frame ready if universe gets smaller than previous

    if (universe != 0) {
      stats.packets_late++;
    } 
    
  } else if (universe > next_universe) {
      stats.packets_lost++;
  }
  next_universe = universe + 1;

  stats.packets++;

  uint8_t index = universe;

  // read universe and put into the right part of the display buffer
  int64_t before_copy = micros();
  for (int i = 0; i < length / 3; i++)
  {
    int led = i + (index * 170);
    if (led < numLeds*4)
    {
      leds[led] = CRGB(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]);
    }
  }
  int64_t after_copy = micros();

  stats.dmx_data_copy_usec += after_copy - before_copy;
  stats.dmx_data_copy_count++;
}

/**
 * Initialized artnet and starts parsing incoming traffic
 */
void read_dmx_task(void*) {
  // this will be called for each packet received
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame);

  // we ratelimit reading UPD packets so that
  // packets will not come in in big bursts, but at most
  // around 1 packets/ms which may allow to have smaller 
  // ringbuffer for incoming DMX, since IP stack will buffer
  // also some of the data
  #define WAIT_BEFORE_NEXT_PACKET_READ_INTERVAL_USEC 800;
  uint64_t next_read = WAIT_BEFORE_NEXT_PACKET_READ_INTERVAL_USEC;

  for (;;) {

    // read and parse UDP traffic 
    // according to my testing artnet is able to
    // handle ~1000 packets/s so it will also 
    // limit amount of leds connected to single
    // controller
    uint64_t current_time = micros();
    if (current_time > next_read) {
      next_read = current_time + WAIT_BEFORE_NEXT_PACKET_READ_INTERVAL_USEC;
      uint16_t res = artnet.read();

      // consider running led write only on idle packets... 
      if (res == 0) {
        // NO PACKET / ignored.... IDLE
        stats.idle_packets++;
        } else if (res == ART_DMX) {
        stats.dmx_packets++;
      } else if (res == ART_POLL) {
        stats.poll_packets++;
      } else if (res == ART_SYNC) {
        stats.sync_packets++;
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  ConnectWifi();

  // looks like writing data to multiple pins takes longer...
  // 600 leds => 18ms 
  // 2 pins 600 leds each => 18ms
  // 4 pins 600 leds each => 19ms
  // 5 pins 600 leds each => 37ms
  // 6 pins 600 leds each => 37ms
  // 8 pins 600 leds each => ??ms
  // 9 pins 600 leds each => 56ms
  FastLED.addLeds<WS2812, dataPin1, GRB>(&leds[numLeds*0], numLeds);
  FastLED.addLeds<WS2812, dataPin2, GRB>(&leds[numLeds*1], numLeds);
  FastLED.addLeds<WS2812, dataPin3, GRB>(&leds[numLeds*2], numLeds);
  FastLED.addLeds<WS2812, dataPin4, GRB>(&leds[numLeds*3], numLeds);

  // since led writing is very sensitive for jitter in timings it will run on core 1 which 
  // seems to have less interrupt handling causing jitter

  xTaskCreatePinnedToCore(
    read_dmx_task ,             // task function
    "ReadDmx",            // task name
    10000,                // stack size in words
    NULL,                 // task input parameters
    0,                    // priority
    &ReadDmx_core_0_Task, // handle to command the task
    0                     // run on core 0
  );
}

void print_stats() {
  uint64_t current_time = micros();
  
  Serial.println("-------------- Stats ------------------");
  
  Serial.print("Stats running on core: ");
  Serial.println(xPortGetCoreID());

  Serial.print("Packet/sec: ");
  Serial.println(stats.packets * 1000000 / (current_time - stats.count_start_time));

  Serial.print("Idle packets: ");
  Serial.println(stats.idle_packets);
  Serial.print("DMX packets: ");
  Serial.println(stats.dmx_packets);
  Serial.print("Poll packets: ");
  Serial.println(stats.poll_packets);
  Serial.print("Sync packets: ");
  Serial.println(stats.sync_packets);

  Serial.print("Led write out (usec):");
  if (stats.led_show_count != 0) {
    Serial.println(stats.led_show_usec / stats.led_show_count);
  } else {
    Serial.println("<no data>");
  }

  Serial.print("DMX data copy per packet (usec): ");
  if (stats.dmx_data_copy_count != 0) {
    Serial.println(stats.dmx_data_copy_usec / stats.dmx_data_copy_count);
  } else {
    Serial.println("<no data>");
  }

  Serial.print("Print stats (usec): ");
  Serial.println(micros() - current_time);

  Serial.print("Packets total: ");
  Serial.println(stats.packets_total);

  Serial.print("Packets lost: ");
  Serial.println(stats.packets_lost);
  Serial.print("Packets lost (%): ");
  Serial.println(stats.packets_lost*100/(stats.packets_total+1));

  Serial.print("Packets late (%): ");
  Serial.println(stats.packets_late*100/(stats.packets_total+1));

  // reset relevant counters  
  stats.count_start_time = current_time;
  stats.packets = 0;
  stats.led_show_usec = 0;
  stats.led_show_count = 0;
  stats.idle_packets = 0;
  stats.dmx_packets = 0;
  stats.poll_packets = 0;
  stats.sync_packets = 0;

}

/**
 * If this method takes a long time to execute like over 3ms
 * then UPD starts to drop some packages. So we have moved UPD reading
 * to run on a separate task in core 0. 
 */
void send_frame() {
  uint64_t before = micros();

  // this actually seems to take really long time... 
  if (frame_ready) {
    frame_ready = false;
    FastLED.show();
  }

  // update stats
  uint64_t after = micros();
  stats.led_show_usec += (after - before);
  stats.led_show_count++;
}

#define STATS_INTERVAL 3000*1000

// usec to wait before checking if next frame is ready to go
#define SOME_DATA_TO_LEDS_INTERVAL 10*1000 

void loop()
{
  // stats etc. triggers...
  static int next_stats_trigger = STATS_INTERVAL;
  static int next_send_frame_trigger = SOME_DATA_TO_LEDS_INTERVAL;
  uint64_t current_time = micros();

  // send frames to leds with ~static frame rate
  // also trying to make sure that we send stuff when there was
  // no UPD data coming in
  if (current_time > next_send_frame_trigger) {
    next_send_frame_trigger = current_time + SOME_DATA_TO_LEDS_INTERVAL;
    send_frame();
  }  

  // trigger stats printing
  if (current_time > next_stats_trigger) {
    next_stats_trigger = current_time + STATS_INTERVAL;
    print_stats();
  }
}
