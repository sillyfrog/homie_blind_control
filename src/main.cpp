#include <Arduino.h>
#include <Homie.h>
#include <Time.h>
#include <TimeLib.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <markisol.h>

// If you don't want to listen for incoming signals, comment out the following line
#define MONITOR_INPUT

// If you want the built in LED to flash when a signal is received, uncomment the following
// #define FLASH_ON_INPUT

// If you want the built in LED to flash when a signal is sent, uncomment the following
#define FLASH_ON_SEND

// To not track and publish the temperature, comment out the following line
#define MONITOR_TEMPERATURE
// SHT3X pin allocation: SDA/SCL
//      D1: SCL (GPIO5)
//      D2: SD (GPIO4)

#ifdef MONITOR_TEMPERATURE
#include <WEMOS_SHT3X.h>
#endif

// Pin allocation
// #define TRANSMIT_PIN D5
#ifdef MONITOR_INPUT
#define INPUT_PIN D6
#endif

#define FW_NAME "blind-control"
#define FW_VERSION "1.2.0"

#define COMMAND_LEN 100
uint64_t commandQueue[COMMAND_LEN];
uint8_t commandRetryQueue[COMMAND_LEN];
uint8_t queueEnd = 0;
uint8_t queueActive = 0;
#define COMMAND_REPEAT_COUNT 3
uint64_t lastBroadcastCommand = 0;
uint64_t timeRepeatBroadcast = 0;
#define TIME_BROADCAST_REPEATS 2000000

char ledstate = 0;

#define BROADCAST_SILENCE_WDT 300 * 1000000
uint64_t lastbroadcast = 0;
uint64_t lastSecond = 0;

HomieNode commandNode("command", "command");

#define FFS SPIFFS
AsyncWebServer server(80);

#ifdef MONITOR_TEMPERATURE
#define SEND_ENVIRONMENT_INTERVAL 30 * 1000 * 1000
#define UPDATE_ENVIRONMENT_INTERVAL 2 * 1000 * 1000
uint64_t lastEnvSent = 0;
uint64_t lastEnvUpdate = 0;
SHT3X sht30(0x44);
HomieNode environmentNode("environment", "environment");
float temperature = -100;
float humidity = -100;
#endif

#ifdef MONITOR_INPUT
#define MAX_SAMPLES 1500
uint16_t samples[MAX_SAMPLES];
uint16_t sampleindex = 0;
uint64_t lastChange = 0;
uint16_t lastindex = 0;

void ICACHE_RAM_ATTR changeInput()
{
  uint64_t triggertime = micros64();
  uint64_t duration = triggertime - lastChange;
  lastChange = triggertime;
#ifdef FLASH_ON_INPUT
  digitalWrite(LED_BUILTIN, !digitalRead(INPUT_PIN));
#endif
  if (duration > 10000)
  {
    // We don't care, reset the sampleindex
    sampleindex = 0;
    return;
  }

  if (sampleindex == 0)
  {
    // If at the start of the sample, check we have a desired start AGC
    if (duration < 4500 || duration > 6000)
    {
      // No good, ignore
      return;
    }
    // Check we are currently low, if not, ignore
    if (digitalRead(INPUT_PIN))
    {
      return;
    }
  }

  samples[sampleindex] = (uint16_t)duration;
  sampleindex++;
  if (sampleindex >= MAX_SAMPLES)
  {
    sampleindex = 0;
  }
}
#endif

#ifdef MONITOR_TEMPERATURE
void updateEnvNode(float val, String desc)
{
  if (val > -99)
  {
    Serial << desc << ": " << val << endl;
    String strval = String(val);
    environmentNode.setProperty(desc).send(strval);
  }
}

void readEnvironment()
{
  // Update the temperature and humidity readings
  if (sht30.get() == 0)
  {
    // All good
    temperature = sht30.cTemp;
    humidity = sht30.humidity;
  }
  else
  {
    Serial << "Error reading tempurature!" << endl;
  }
}
#endif

// Increment a queue pointer by one, wrapping if required
uint8_t incQueue(uint8_t i)
{
  i++;
  if (i >= COMMAND_LEN)
  {
    i = 0;
  }
  return i;
}

void setupHandler()
{
  server.begin();
}

void everySecondHandler()
{
  // Check the broadcast watchdog
  if ((micros64() - lastbroadcast) > BROADCAST_SILENCE_WDT)
  {
    // Have not received a broadcast message for BROADCAST_SILENCE_WDT, reboot
    Serial << "No broadcast updates, rebooting..." << endl;
    ESP.restart();
  }
}

void loopHandler()
{
#ifdef MONITOR_TEMPERATURE
  if (micros64() - lastEnvSent >= SEND_ENVIRONMENT_INTERVAL || lastEnvSent == 0)
  {
    updateEnvNode(temperature, "temperature");
    updateEnvNode(humidity, "humidity");
    lastEnvSent = micros64();
  }
#endif
}

void ledBlink()
{
  ledstate = !ledstate;
  digitalWrite(LED_BUILTIN, ledstate);
}

void ledOff()
{
  digitalWrite(LED_BUILTIN, 1);
}

void ledOn()
{
  digitalWrite(LED_BUILTIN, 0);
}

bool broadcastHandler(const String &level, const String &value)
{
  lastbroadcast = micros64();
  if (level == "time")
  {
    int nowtime = value.toInt();
    setTime(nowtime);
  }
  return true;
}

void queueCommand(String strcommand)
{
  commandQueue[queueEnd] = stringCommandToCommand(strcommand);
  commandRetryQueue[queueEnd] = COMMAND_REPEAT_COUNT;
  queueEnd = incQueue(queueEnd);
}

bool commandHandler(const HomieRange &range, const String &value)
{
  queueCommand(value);
  return true;
}

void handleWebCommand(AsyncWebServerRequest *request)
{
  String reqcommand = request->arg("cmd");
  if (reqcommand.length() > 6)
  {
    queueCommand(reqcommand);
    request->send(200, "text/plain", "OK");
  }
  else
  {
    request->send(500, "text/plain", "EMPTY");
  }
}

void handleWebRemotes(AsyncWebServerRequest *request)
{
  String message = "[";
  for (int i = 0; i < recent_remote_count; i++)
  {
    message += "\"" + String(recent_remote_ids[i], 16) + "\"";
    if (i < (recent_remote_count - 1))
    {
      message += ",";
    }
  }
  message += "]";
  request->send(200, "text/json", message);
}

void setup()
{
  setupMarkisol();
  Serial.begin(115200);

#ifdef MONITOR_INPUT
  pinMode(INPUT_PIN, INPUT);
#ifdef FLASH_ON_INPUT
  pinMode(LED_BUILTIN, OUTPUT);
#endif
#ifdef FLASH_ON_SEND
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  attachInterrupt(digitalPinToInterrupt(INPUT_PIN), changeInput, CHANGE);
#endif

  Homie.disableResetTrigger();
  // Homie.disableLedFeedback();
  Homie_setFirmware(FW_NAME, FW_VERSION);
  Homie.setSetupFunction(setupHandler).setLoopFunction(loopHandler);
  Homie.setBroadcastHandler(broadcastHandler);

  commandNode.advertise("send").settable(commandHandler);

  Homie.setup();

  // Handles serving files from the Filesystem
  server.addHandler(new SPIFFSEditor("", "", FFS));
  server.serveStatic("/", FFS, "/").setDefaultFile("index.htm");
  server.on("/command", HTTP_GET + HTTP_POST, handleWebCommand);
  server.on("/remotes", HTTP_GET, handleWebRemotes);
}

void loop()
{
  Homie.loop();
  // Run this in the main loop as we want to show things
  // are wrong when not connected
  if (micros64() / 1000000 != lastSecond)
  {
    everySecondHandler();
    lastSecond = micros64() / 1000000;
  }

  bool queueEmpty = true;
  for (uint8_t i = queueActive; i < COMMAND_LEN; i++)
  {
    if (commandRetryQueue[i] > 0)
    {
      commandRetryQueue[i]--;
#ifdef FLASH_ON_SEND
      ledOn();
#endif
      sendMarkisolCommand(commandQueue[i]);
#ifdef FLASH_ON_SEND
      ledOff();
#endif
      Serial << "Sending index: " << i << endl;
      queueActive = incQueue(i);
      queueEmpty = false;
      break;
    }
  }
  if (queueEmpty)
  {
    queueActive = 0;
  }

#ifdef MONITOR_TEMPERATURE
  if ((micros64() - lastEnvUpdate) >= UPDATE_ENVIRONMENT_INTERVAL || lastEnvUpdate == 0)
  {
    readEnvironment();
    lastEnvUpdate = micros64();
  }
#endif

#ifdef MONITOR_INPUT
  if (sampleindex > 85)
  {
    uint64_t command = readSample(samples, sampleindex);
    sampleindex = 0;
    if (command && (command != lastBroadcastCommand || micros64() > timeRepeatBroadcast))
    {
      String commandstr = fullCommandToString(command);
      if (commandstr.length() > 1)
      {
        commandNode.setProperty("received").send(commandstr);
      }
      lastBroadcastCommand = command;
      timeRepeatBroadcast = micros64() + TIME_BROADCAST_REPEATS;
    }
  }
#endif
}
