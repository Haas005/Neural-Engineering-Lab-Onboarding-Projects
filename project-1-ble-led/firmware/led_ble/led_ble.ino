/*
 * XIAO ESP32 BLE LED + Music Controller
 * -----------------------------------------------------------------------
 * Controls a red, a white and a green LED plus a small speaker over
 * Bluetooth Low Energy (BLE). It can blink the LEDs in patterns and play the
 * song while the LEDs flash with the notes.
 *
 * Wiring (same for XIAO ESP32-C3, -S3 and -C6):
 *   D3 -> 330 ohm resistor -> RED   LED anode (long leg) -> cathode -> GND
 *   D4 -> 330 ohm resistor -> WHITE LED anode (long leg) -> cathode -> GND
 *   D5 -> 330 ohm resistor -> GREEN LED anode (long leg) -> cathode -> GND
 *   D1 -> speaker / passive buzzer (+) ... (-) -> GND
 *         A bare speaker needs a resistor in series (220 ohm or more) or a
 *         transistor driver. Wiring a speaker straight to a pin can overload it.
 *         The speaker MUST be a passive buzzer or a speaker. An "active"
 *         buzzer has a built-in tone and can only make one fixed beep.
 *
 * BLE interface (one service, two characteristics):
 *   Service   4fafc201-1fb5-459e-8fcc-c5c9c331914b
 *   LED state beb5483e-36e1-4688-b7f5-ea07361b26a8   read / write
 *             3 bytes: [red, white, green], each 0 = off, 1 = on.
 *             Writing this switches to manual mode (pattern 0) and stops music.
 *   Pattern   1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e   read / write
 *             2 bytes: [pattern, speed]
 *               pattern: 0 = manual, 1 = blink all, 2 = chase, 3 = alternate,
 *                        4 = play the song
 *               speed:   step time in units of 10 ms (50 = 500 ms) for
 *                        patterns 1-3. Ignored by the music pattern.
 *                        Optional: a 1-byte write sets only the pattern.
 *             When the song ends the pattern returns to 0 on its own.
 *
 * Arduino IDE setup: install "esp32 by Espressif Systems" in Boards Manager,
 * pick your XIAO board under Tools > Board, then upload.
 * Works with arduino-esp32 core 2.x and 3.x.
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ---------------------------------------------------------------- config
static const char *DEVICE_NAME = "XIAO-LED";

#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define LED_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define PATTERN_CHAR_UUID "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

// LED order matches the app: red, white, green.
static const uint8_t LED_PINS[3] = {D3, D4, D5};

// Change this if your speaker is wired to a different pin.
#define SPEAKER_PIN D1

// Length of one eighth note in milliseconds. Most of the song is eighth
// notes, so 180 ms gives a lively tempo. Raise it to slow the song down,
// lower it to speed it up.
static const uint32_t EIGHTH_MS = 300;

enum Pattern : uint8_t {
  PATTERN_MANUAL = 0,
  PATTERN_BLINK_ALL = 1,
  PATTERN_CHASE = 2,
  PATTERN_ALTERNATE = 3,
  PATTERN_MUSIC = 4,
  PATTERN_COUNT = 5
};

// ----------------------------------------------------------------- music
// Note frequencies in Hz.
#define N_REST   0
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523

struct Note {
  uint16_t freq;    // Hz, 0 = rest
  uint8_t eighths;  // length in eighth notes (1 = eighth, 2 = quarter, 4 = half)
};

// Your song. Lengths were converted from the divisor style:
// 8 -> 1 eighth, 4 -> 2 eighths, 2 -> 4 eighths.
static const Note SONG[] = {
    {NOTE_E4, 1}, {NOTE_A4, 1}, {NOTE_C5, 1},
    {NOTE_B4, 1}, {NOTE_A4, 1}, {NOTE_C5, 1}, {NOTE_A4, 1}, {NOTE_B4, 1}, {NOTE_A4, 1}, {NOTE_F4, 1}, {NOTE_G4, 1},
    {NOTE_E4, 4}, {NOTE_E4, 1}, {NOTE_A4, 1}, {NOTE_C5, 1},
    {NOTE_B4, 1}, {NOTE_A4, 1}, {NOTE_C5, 1}, {NOTE_A4, 1}, {NOTE_C5, 1}, {NOTE_A4, 1}, {NOTE_E4, 1}, {NOTE_DS4, 1},
    {NOTE_D4, 4}, {NOTE_D4, 1}, {NOTE_F4, 1}, {NOTE_GS4, 1},
    {NOTE_B4, 4}, {NOTE_D4, 1}, {NOTE_F4, 1}, {NOTE_GS4, 1},
    {NOTE_A4, 4}, {NOTE_C4, 1}, {NOTE_C4, 1}, {NOTE_G4, 1},
    {NOTE_F4, 1}, {NOTE_E4, 1}, {NOTE_G4, 1}, {NOTE_F4, 1}, {NOTE_F4, 1}, {NOTE_E4, 1}, {NOTE_E4, 1}, {NOTE_GS4, 1},
    {NOTE_A4, 4},
};
static const size_t SONG_LEN = sizeof(SONG) / sizeof(SONG[0]);

// ----------------------------------------------------------------- state
// Written from the BLE task, read from loop(), so mark them volatile.
volatile uint8_t ledState[3] = {0, 0, 0};
volatile uint8_t pattern = PATTERN_MANUAL;
volatile uint8_t speedUnits = 50;  // x10 ms
volatile bool dirty = true;        // something changed, re-apply outputs
volatile bool restartAdvertising = false;
volatile bool musicRestart = false;  // set by BLE write to start the song

BLECharacteristic *ledChar = nullptr;
BLECharacteristic *patternChar = nullptr;

uint32_t lastStepMs = 0;
uint32_t stepCount = 0;

// Music playback state (only touched from loop()).
bool musicActive = false;
bool noteSounding = false;
size_t songIndex = 0;
uint32_t noteStartMs = 0;
uint32_t noteTotalMs = 0;
uint32_t noteToneMs = 0;
uint8_t flashColor = 0;

// --------------------------------------------------------------- speaker
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void speakerInit() {
  ledcAttach(SPEAKER_PIN, 2000, 8);
  ledcWriteTone(SPEAKER_PIN, 0);
}
static void speakerTone(uint32_t freq) {
  ledcWriteTone(SPEAKER_PIN, freq);  // 0 = silence
}
#else
static const uint8_t SPEAKER_CHANNEL = 4;
static void speakerInit() {
  ledcSetup(SPEAKER_CHANNEL, 2000, 8);
  ledcAttachPin(SPEAKER_PIN, SPEAKER_CHANNEL);
  ledcWriteTone(SPEAKER_CHANNEL, 0);
}
static void speakerTone(uint32_t freq) {
  ledcWriteTone(SPEAKER_CHANNEL, freq);  // 0 = silence
}
#endif

// ------------------------------------------------------------ BLE callbacks
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    Serial.println("Client connected");
  }
  void onDisconnect(BLEServer *server) override {
    Serial.println("Client disconnected");
    restartAdvertising = true;  // handled in loop()
  }
};

class LedCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    const uint8_t *data = c->getData();
    size_t len = c->getLength();
    if (len < 3) return;  // ignore malformed writes
    for (int i = 0; i < 3; i++) ledState[i] = data[i] ? 1 : 0;
    pattern = PATTERN_MANUAL;  // direct LED control cancels patterns and music
    uint8_t p[2] = {PATTERN_MANUAL, speedUnits};
    patternChar->setValue(p, 2);
    dirty = true;
  }
};

class PatternCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    const uint8_t *data = c->getData();
    size_t len = c->getLength();
    if (len < 1) return;
    if (data[0] >= PATTERN_COUNT) return;  // unknown pattern id
    if (len >= 2 && data[1] > 0) speedUnits = data[1];
    stepCount = 0;
    lastStepMs = 0;
    if (data[0] == PATTERN_MUSIC) musicRestart = true;
    pattern = data[0];
    dirty = true;
  }
};

// --------------------------------------------------------------- helpers
static void writeLeds(bool a, bool b, bool c) {
  digitalWrite(LED_PINS[0], a ? HIGH : LOW);
  digitalWrite(LED_PINS[1], b ? HIGH : LOW);
  digitalWrite(LED_PINS[2], c ? HIGH : LOW);
}

static void syncCharacteristics() {
  uint8_t s[3] = {ledState[0], ledState[1], ledState[2]};
  ledChar->setValue(s, 3);
  uint8_t p[2] = {pattern, speedUnits};
  patternChar->setValue(p, 2);
}

// Starts note number i: sound it and flash the LEDs.
// Short notes light one LED, cycling red -> white -> green. Held notes
// (a half note or longer) light all three. Rests keep the LEDs dark.
static void startNote(size_t i, uint32_t now) {
  const Note &n = SONG[i];
  noteStartMs = now;
  noteTotalMs = (uint32_t)n.eighths * EIGHTH_MS;
  // Leave a short gap at the end of each note so repeated notes are distinct.
  uint32_t gap = noteTotalMs / 6;
  if (gap < 25) gap = 25;
  noteToneMs = noteTotalMs - gap;
  speakerTone(n.freq);
  noteSounding = true;
  if (n.freq == 0) {
    writeLeds(false, false, false);
  } else if (n.eighths >= 4) {
    writeLeds(true, true, true);
  } else {
    writeLeds(flashColor == 0, flashColor == 1, flashColor == 2);
    flashColor = (flashColor + 1) % 3;
  }
}

static void stopMusic() {
  speakerTone(0);
  noteSounding = false;
  musicActive = false;
}

// ------------------------------------------------------------------ setup
void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 3; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }
  speakerInit();

  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService *service = server->createService(SERVICE_UUID);

  ledChar = service->createCharacteristic(
      LED_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  ledChar->setCallbacks(new LedCallbacks());

  patternChar = service->createCharacteristic(
      PATTERN_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  patternChar->setCallbacks(new PatternCallbacks());

  syncCharacteristics();
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("Advertising as XIAO-LED");
}

// ------------------------------------------------------------------- loop
void loop() {
  delay(1);  // yield to the BLE stack and save power; far finer than note timing

  if (restartAdvertising) {
    restartAdvertising = false;
    delay(300);  // give the BLE stack a moment before re-advertising
    BLEDevice::startAdvertising();
    Serial.println("Advertising again");
  }

  const uint32_t now = millis();
  const uint8_t current = pattern;

  // Leaving music mode (any other write) silences the speaker.
  if (current != PATTERN_MUSIC && musicActive) {
    stopMusic();
    dirty = true;
  }

  if (current == PATTERN_MUSIC) {
    if (musicRestart) {
      musicRestart = false;
      dirty = false;
      musicActive = true;
      songIndex = 0;
      flashColor = 0;
      Serial.println("Playing the song");
      startNote(0, now);
      return;
    }
    if (!musicActive) return;

    const uint32_t elapsed = now - noteStartMs;
    if (noteSounding && elapsed >= noteToneMs) {
      speakerTone(0);  // gap between notes
      writeLeds(false, false, false);
      noteSounding = false;
    }
    if (elapsed >= noteTotalMs) {
      songIndex++;
      if (songIndex >= SONG_LEN) {
        // Song finished: go back to manual mode with everything off.
        stopMusic();
        for (int i = 0; i < 3; i++) ledState[i] = 0;
        pattern = PATTERN_MANUAL;
        dirty = true;
        syncCharacteristics();
        Serial.println("Song finished");
      } else {
        // Start the next note exactly when the previous one ended, so timing
        // errors don't accumulate over the song.
        startNote(songIndex, noteStartMs + noteTotalMs);
      }
    }
    return;
  }

  if (current == PATTERN_MANUAL) {
    if (dirty) {
      dirty = false;
      writeLeds(ledState[0], ledState[1], ledState[2]);
    }
    return;
  }

  // Pattern modes: advance one step every (speedUnits * 10) ms.
  const uint32_t interval = (uint32_t)speedUnits * 10UL;
  if (dirty || now - lastStepMs >= interval) {
    dirty = false;
    lastStepMs = now;
    switch (current) {
      case PATTERN_BLINK_ALL: {
        bool on = (stepCount % 2) == 0;
        writeLeds(on, on, on);
        break;
      }
      case PATTERN_CHASE: {
        uint8_t n = stepCount % 3;
        writeLeds(n == 0, n == 1, n == 2);
        break;
      }
      case PATTERN_ALTERNATE: {
        bool even = (stepCount % 2) == 0;
        writeLeds(even, !even, even);
        break;
      }
    }
    stepCount++;
  }
}
