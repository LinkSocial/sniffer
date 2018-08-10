#include "fsm_config.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>

// definição do tipo dos dados que serão enviados para o broker
typedef struct packet_t {
  uint8_t mac[6];
  char ssid[21];
} packet;

// Update these with values suitable for your network.

const char* ssid = "Edna";
const char* password = "3dn4123@";
const char* rabbitmq_server = "192.168.0.108";
int port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);
char payload[100];

// Variáveis da FSM
state cur_state = ENTRY_STATE;
event cur_evt;
event (* cur_state_function)(void);

extern "C" {
  #include <user_interface.h>
}

#define DATA_LENGTH           112

#define TYPE_MANAGEMENT       0x00
#define TYPE_CONTROL          0x01
#define TYPE_DATA             0x02
#define SUBTYPE_PROBE_REQUEST 0x04
#define CHANNEL_HOP_INTERVAL_MS   1000

#define DISABLE 0
#define ENABLE  1

// Will get BUFFER_LENGTH - (BUFFER_SAFE_MARGIN - 1) probe requests
// due to countPR starts at 0
#define BUFFER_LENGTH 70
#define BUFFER_SAFE_MARGIN 21

packet buffer[BUFFER_LENGTH];
int countPR = 0;

struct RxControl {
 signed rssi:8; // signal intensity of packet
 unsigned rate:4;
 unsigned is_group:1;
 unsigned:1;
 unsigned sig_mode:2; // 0:is 11n packet; 1:is not 11n packet;
 unsigned legacy_length:12; // if not 11n packet, shows length of packet.
 unsigned damatch0:1;
 unsigned damatch1:1;
 unsigned bssidmatch0:1;
 unsigned bssidmatch1:1;
 unsigned MCS:7; // if is 11n packet, shows the modulation and code used (range from 0 to 76)
 unsigned CWB:1; // if is 11n packet, shows if is HT40 packet or not
 unsigned HT_length:16;// if is 11n packet, shows length of packet.
 unsigned Smoothing:1;
 unsigned Not_Sounding:1;
 unsigned:1;
 unsigned Aggregation:1;
 unsigned STBC:2;
 unsigned FEC_CODING:1; // if is 11n packet, shows if is LDPC packet or not.
 unsigned SGI:1;
 unsigned rxend_state:8;
 unsigned ampdu_cnt:8;
 unsigned channel:4; //which channel this packet in.
 unsigned:12;
};

struct SnifferPacket{
    struct RxControl rx_ctrl;
    uint8_t data[DATA_LENGTH];
    uint16_t cnt;
    uint16_t len;
};

static void getMAC(uint8_t* data, uint16_t offset, int i) {
  buffer[i].mac[0] = data[offset];
  buffer[i].mac[1] = data[offset+1];
  buffer[i].mac[2] = data[offset+2];
  buffer[i].mac[3] = data[offset+3];
  buffer[i].mac[4] = data[offset+4];
  buffer[i].mac[5] = data[offset+5];
}

static void printDataSpan(uint16_t start, uint16_t size, uint8_t* data, int count) {
  if (size > 20) {
    return;
  } else {
    for(uint16_t i = start; i < DATA_LENGTH && i < start+size; i++) {
      buffer[count].ssid[i-start] = (char) data[i];
    }
    buffer[count].ssid[size] = '\0';
  }
  Serial.println();
}

static void showMetadata(SnifferPacket *snifferPacket) {

  unsigned int frameControl = ((unsigned int)snifferPacket->data[1] << 8) + snifferPacket->data[0];

  uint8_t version      = (frameControl & 0b0000000000000011) >> 0;
  uint8_t frameType    = (frameControl & 0b0000000000001100) >> 2;
  uint8_t frameSubType = (frameControl & 0b0000000011110000) >> 4;
  uint8_t toDS         = (frameControl & 0b0000000100000000) >> 8;
  uint8_t fromDS       = (frameControl & 0b0000001000000000) >> 9;


  // Only look for probe request packets
  if (frameType != TYPE_MANAGEMENT ||
      frameSubType != SUBTYPE_PROBE_REQUEST)
        return;

   delay(10);

  getMAC(snifferPacket->data, 10, countPR);

  int bitLocal = buffer[countPR].mac[0] & 0b00000010;
  
  if (bitLocal == 2) {
    return;
  }

//  if (bitRead(buffer[countPR].mac[0], 1) == 1) {
//    return;
//  }

  uint8_t SSID_length = snifferPacket->data[25];

  // Only look for direct probe request
  if (SSID_length == 0) {
    return;
  }
  
  printDataSpan(26, SSID_length, snifferPacket->data, countPR);

  Serial.print("Probe count: ");
  Serial.print(countPR + 1);

  countPR += 1;
}

/**
 * Callback for promiscuous mode
 */
static void ICACHE_FLASH_ATTR sniffer_callback(uint8_t *buffer, uint16_t length) {
  struct SnifferPacket *snifferPacket = (struct SnifferPacket*) buffer;
  showMetadata(snifferPacket);
}

static os_timer_t channelHop_timer;

/**
 * Callback for channel hoping
 */
void channelHop()
{
  // hoping channels 1-14
  uint8 new_channel = wifi_get_channel() + 1;
  if (new_channel > 13)
    new_channel = 1;
  wifi_set_channel(new_channel);
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266", "psd", "psd")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

event config_board_state() {
  // set the WiFi chip to "promiscuous" mode aka monitor mode
  wifi_promiscuous_enable(ENABLE);
  delay(10);
  os_timer_arm(&channelHop_timer, CHANNEL_HOP_INTERVAL_MS, 1);
  delay(10);
  return empty;
}

event check_probe_num_state() {  
  if (countPR > (BUFFER_LENGTH - BUFFER_SAFE_MARGIN)) {
    wifi_promiscuous_enable(DISABLE);
    delay(10);
    os_timer_disarm(&channelHop_timer);
    delay(10);
    countPR = 0;
    return success;
  }
  return wait;
}

event connect_state() {
  delay(10);
  setup_wifi();
  client.setServer(rabbitmq_server, port);
  client.connect("ESP8266", "psd", "psd");
  delay(10);
  if (client.connected()) {
    return connected;
  } else {
    return error;
  }
}

event send_data_state() {
  if (!client.connected()) {
    return error;
  } else {
    for (int i = 0; i < BUFFER_LENGTH - (BUFFER_SAFE_MARGIN - 1); i++) {
      delay(10);
      sprintf(payload, "{\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\", \"ssid\":\"%s\"}", buffer[i].mac[0], buffer[i].mac[1], buffer[i].mac[2], buffer[i].mac[3], buffer[i].mac[4], buffer[i].mac[5], (char*) buffer[i].ssid);
      Serial.println(payload);
      client.publish("psd", payload);
    }
    return success;
  }
}

event end_state() {
  
}

void setup() {
  Serial.begin(115200);

  // set promiscuous receive callback
  wifi_set_opmode(STATION_MODE);
  wifi_set_channel(1);
  wifi_promiscuous_enable(DISABLE);
  delay(10);
  wifi_set_promiscuous_rx_cb(sniffer_callback);
  delay(10);
    
  // setup the channel hoping callback timer
  os_timer_disarm(&channelHop_timer);
  os_timer_setfn(&channelHop_timer, (os_timer_func_t *) channelHop, NULL);
}

void loop() {
  cur_state_function = state_functions[cur_state];
  cur_evt = (event) cur_state_function();
  if (EXIT_STATE == cur_state)
    return;
  cur_state = lookup_transitions(cur_state, cur_evt);
}
