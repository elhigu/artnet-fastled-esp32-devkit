/*
This example will receive multiple universes via Art-Net and control a strip of
WS2812 LEDs via the FastLED library: https://github.com/FastLED/FastLED
This example may be copied under the terms of the MIT license, see the LICENSE file for details
*/
#include <ArtnetWifi.h>
#include <Arduino.h>
#include <FastLED.h>

struct Stats {
  uint64_t count_start_time;
  uint64_t packets;
  
  uint64_t led_show_count;
  uint64_t led_show_us;

  uint64_t dmx_data_copy_count;
  uint64_t dmx_data_copy_ms;

  uint64_t idle_packets;
  uint64_t dmx_packets;
  uint64_t poll_packets;
  uint64_t sync_packets;
};

Stats stats = { 0 };

// Wifi settings
const char* ssid = "MySSID";
const char* password = "password";

// LED settings
const int numLeds = 600;
const int numberOfChannels = numLeds * 3; // Total number of channels you want to receive (1 led = 3 channels)

// using array didn't work for template parameter (could have asked from chatbot how to write macro for this but bleh)
const byte dataPin1 = 15;
const byte dataPin2 = 2;
const byte dataPin3 = 4;
const byte dataPin4 = 16;
const byte dataPin5 = 17;
const byte dataPin6 = 5;
const byte dataPin7 = 18;
const byte dataPin8 = 19;
const byte dataPin9 = 21;

CRGB leds[numLeds];

// Art-Net settings
ArtnetWifi artnet;

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

void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data)
{
  stats.packets++;

  uint8_t index = universe;

  // read universe and put into the right part of the display buffer
  int64_t before_copy = millis();
  for (int i = 0; i < length / 3; i++)
  {
    int led = i + (index * 170);
    if (led < numLeds)
    {
      leds[led] = CRGB(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]);
    }
  }
  int64_t after_copy = millis();

  stats.dmx_data_copy_ms += after_copy - before_copy;
  stats.dmx_data_copy_count++;
}

void setup()
{
  Serial.begin(115200);
  ConnectWifi();
  artnet.begin();

  // looks like writing data to multiple pins takes longer...
  // 600 leds => 18ms 
  // 2 pins 600 leds each => 18ms
  // 4 pins 600 leds each => 19ms
  // 5 pins 600 leds each => 37ms
  // 6 pins 600 leds each => 37ms
  // 8 pins 600 leds each => ??ms
  // 9 pins 600 leds each => 56ms
  FastLED.addLeds<WS2812, dataPin1, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin2, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin3, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin4, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin5, GRB>(leds, numLeds);
/*
  FastLED.addLeds<WS2812, dataPin6, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin7, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin8, GRB>(leds, numLeds);
  FastLED.addLeds<WS2812, dataPin9, GRB>(leds, numLeds);
*/
  // this will be called for each packet received
  artnet.setArtDmxCallback(onDmxFrame);
}

void print_stats() {
  uint64_t current_time = millis();

  
  Serial.println("-------------- Stats ------------------");

  Serial.print("Packet/sec: ");
  Serial.println(stats.packets * 1000 / (current_time - stats.count_start_time));

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
    Serial.println(stats.led_show_us / stats.led_show_count);
  } else {
    Serial.println("<no data>");
  }

  Serial.print("DMX data copy per packet (usec): ");
  if (stats.dmx_data_copy_count != 0) {
    Serial.println(stats.dmx_data_copy_ms * 1000 / stats.dmx_data_copy_count);
  } else {
    Serial.println("<no data>");
  }

  // reset relevant counters  
  stats.count_start_time = current_time;
  stats.packets = 0;
  stats.led_show_us = 0;
  stats.led_show_count = 0;

}

void send_frame() {
  uint64_t before = millis();

  // this actually seems to take really long time... 
  FastLED.show();

  // update stats
  uint64_t after = millis();
  stats.led_show_us += (after - before)*1000;
  stats.led_show_count++;
}

// TODO: find out how long pauses are fine between UPD reads..
//       and how much there is packet loss in best scenario

#define STATS_INTERVAL 3000
#define FRAME_INTERVAL 100 // should be 33 for 30fps... and 25 for 40fps

void loop()
{
  // read and parse UDP traffic
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

  // stats etc. triggers...
  static int next_stats_trigger = STATS_INTERVAL;
  static int next_send_frame_trigger = FRAME_INTERVAL;
  uint64_t current_time = millis();

  // trigger stats printing
  if (current_time > next_stats_trigger) {
    next_stats_trigger = current_time + STATS_INTERVAL;
    print_stats();
  }

  // send frames to leds with static frame rate
  if (current_time > next_send_frame_trigger) {
    next_send_frame_trigger = current_time + FRAME_INTERVAL;
    send_frame();
  }
}

