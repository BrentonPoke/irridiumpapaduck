#include <iostream>
#include <rockblock_9704.h>
#include <Ducks/PapaDuck.h>
#include <CdpPacket.h>
#include <random>
#define I_EN_PIN  15    // RockBLOCK Pin 3 → GPIO15
#define I_BTD_PIN 13    // RockBLOCK Pin 7 → GPIO13

int messagesSent = 0;
bool messagePending = false;  // Track if we're waiting to send
uint32_t lastSendAttempt = 0;
const uint32_t SEND_INTERVAL = 60000;  // Try every 60 seconds max

PapaDuck duck("PAPADUCK");
const char* ntpServer = "pool.ntp.org";
void handleDuckData(CdpPacket packetBuffer);
std::mt19937 gen;
std::uniform_int_distribution<> distrib(0, 40), hops(1, 5);
std::uniform_real_distribution<float> fm_distrib(10.0, 130.0);
//const char *msg = "{\"DeviceID\":\"SatDuck\",\"MessageID\":\"0001\",\"C:1|FM:123456\",\"hops\":1,\"duckType\":1}";

// const char *msg = R"===(
//                     {
//                     "PapaId": "ROCKBOCK",
//                    "EventType": "sensor",
//                     "InnerPayload": {\"DeviceID\":\"SatDuck\",\"MessageID\":\"0001\",\"C:1|FM:123456\",\"hops\":1,\"duckType\":1}
//                     })===";

static std::string payload;

void onMessageProvisioning(const jsprMessageProvisioning_t *mp) {
  if (mp && mp->provisioningSet) {
    loginfo_ln("Provisioned for %d topics", mp->topicCount);
    for (int i = 0; i < mp->topicCount; i++) {
      loginfo_ln("  [%d] %s (ID %d)", i, mp->provisioning[i].topicName, mp->provisioning[i].topicId);
    }
  }
}

void onMoComplete(const uint16_t id, const rbMsgStatus_t status) {
  loginfo_ln("MO Complete: ID=%d Status=%d", id, (int)status);
  if (status == RB_MSG_STATUS_OK) {
    messagesSent++;
    loginfo_ln("SUCCESS: Message %d sent!", messagesSent);
    messagePending = false;  // Ready for next message
  } else {
    loginfo_ln("Send failed — will retry in 60s");
    messagePending = false;
  }
}

void onMtComplete(const uint16_t id, const rbMsgStatus_t status) {
  loginfo_ln("MT: ID=%d Status=%d", id, (int)status);
}

void onConstellationState(const jsprConstellationState_t *state) {
  if (state) {
    loginfo_ln("Signal: %d/5 bars", state->signalBars);
  }
}

static rbCallbacks_t myCallbacks = {
  .messageProvisioning = onMessageProvisioning,
  .moMessageComplete = onMoComplete,
  .mtMessageComplete = onMtComplete,
  .constellationState = onConstellationState
};

void setup() {

  Serial.begin(115200);
  Serial1.begin(230400, SERIAL_8N1, 0, 4);  // RX=0, TX=4

  gen.seed(std::random_device{}());

  loginfo_ln("RockBLOCK-9704 + T-Beam – STARTING");
  duck.setupWithDefaults();
  duck.onReceiveDuckData(handleDuckData);
  configTime(0, 0, ntpServer,"time.nist.gov");

  pinMode(I_EN_PIN, OUTPUT);
  pinMode(I_BTD_PIN, INPUT);
  digitalWrite(I_EN_PIN, LOW);
  delay(2500);
  digitalWrite(I_EN_PIN, HIGH);

  loginfo_ln("Waiting for boot...");
  uint32_t t = millis() + 45000;
  while (digitalRead(I_BTD_PIN) == LOW && millis() < t) delay(100);
  if (digitalRead(I_BTD_PIN) == LOW) {
    loginfo_ln("BOOT FAILED");
    while (1);
  }
  loginfo_ln("BOOTED!");
  if (rbBegin(Serial1)) {
    loginfo_ln("CONNECTED!");
    rbRegisterCallbacks(&myCallbacks);
  } else {
    loginfo_ln("rbBegin FAILED");
    while (1);
  }
}
void handleDuckData(CdpPacket packetBuffer) {
  payload.clear();
  //randomly generate values for packet

  JsonDocument doc, innerpayload;
  deserializeJson(innerpayload, packetBuffer.data);
  doc["PapaId"] = "PAPADUCK";
  doc["Topic"] = packetBuffer.topic;
  doc["DeviceID"] = duckutils::toString(packetBuffer.sduid);
  doc["MessageID"] = duckutils::toString(packetBuffer.muid);
  doc["Data"] = innerpayload;
  doc["hops"] = packetBuffer.hopCount;
  doc["duckType"] = packetBuffer.duckType;

  serializeJson(doc, payload);
  logdbg_ln("Payload: %s", payload.c_str());

    loginfo_ln("Attempting to queue message... ");
    if (rbSendMessageAsync(244, payload.c_str(), payload.size())) {
      loginfo_ln("QUEUED!");
    } else {
      logerr_ln("Queue failed"); // Allow retry
    }

}

void loop() {
  rbPoll();  // **CRITICAL** — must be called often
  delay(10);

duck.run();
  // Auto-shutdown after success (optional)
  //if (messagesSent > 0) {
    //digitalWrite(I_EN_PIN, LOW);
    //while (digitalRead(I_BTD_PIN) == HIGH) delay(100);
    //rbEnd();
    //Serial1.end();
    //loginfo_ln(F("Session ended — power off or reset to restart"));
    //while (1) delay(1000);
  //}
}
