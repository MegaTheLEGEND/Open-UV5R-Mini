// jl_usbkey: an ESP32 sends JieLi's USB_KEY on the radio's USB lines, so the AC695N's mask ROM starts its USB
// download mode ("UBOOT") instead of the app. Untested on a radio.
//
// The key is 0x16EF, sent MSB first; D+ is the clock (the chip latches data on its rising edge), D- is the data.
// The chip answers by pulling both lines low for 1-2 ms, then pulls D+ up and waits for USB start-of-frame
// packets from the PC. Protocol described by kagaimiq: https://kagaimiq.github.io/jielie/isp/usb/usb-key.html
//
// Wiring (ESP32 dev module, see README.md): GPIO25 -> 1 kOhm -> pad P (D+), GPIO26 -> 1 kOhm -> pad M (D-),
// GND -> pad G. The radio's own USB cable (G, M, P) stays plugged into the PC. Never connect pad V.
//
// Use: radio off. Press BOOT on the ESP32, then switch the radio on within 5 s. The ESP32 sends the key until it
// sees D+ held high, then lets go of both lines so the PC can talk to the chip.

#include <Arduino.h>

static const int PIN_CLK = 25;   // to pad P, USB D+
static const int PIN_DAT = 26;   // to pad M, USB D-
static const int PIN_BTN = 0;    // the BOOT button
static const int PIN_LED = 2;    // the blue LED on most dev modules

static const uint16_t KEY = 0x16EF;
static const uint32_t HALF_US = 10;      // about 50 kHz
static const uint32_t LISTEN_US = 2500;  // after each key, watch D+ this long
static const uint32_t WINDOW_MS = 5000;  // give up after this

static void release_lines()
{
    pinMode(PIN_CLK, INPUT);
    pinMode(PIN_DAT, INPUT);
}

static void send_key()
{
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_DAT, LOW);
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_DAT, OUTPUT);
    for (int bit = 15; bit >= 0; bit--) {
        digitalWrite(PIN_DAT, (KEY >> bit) & 1);
        delayMicroseconds(HALF_US);
        digitalWrite(PIN_CLK, HIGH);
        delayMicroseconds(HALF_US);
        digitalWrite(PIN_CLK, LOW);
    }
    release_lines();
}

// True if D+ is held high for 200 us within the next `us` microseconds. With the lines released, the PC's
// pull-downs keep D+ low unless something on the radio pulls it up.
static bool dplus_held_high(uint32_t us)
{
    uint32_t start = micros();
    while (micros() - start < us) {
        if (digitalRead(PIN_CLK)) {
            uint32_t t = micros();
            bool steady = true;
            while (micros() - t < 200) {
                if (!digitalRead(PIN_CLK)) {
                    steady = false;
                    break;
                }
            }
            if (steady)
                return true;
        }
    }
    return false;
}

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_BTN, INPUT_PULLUP);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    release_lines();
    delay(500);
    Serial.println();
    Serial.println("jl_usbkey: radio off, then press BOOT and switch the radio on within 5 s.");
}

void loop()
{
    if (digitalRead(PIN_BTN))
        return;
    delay(50);

    if (dplus_held_high(2000)) {
        Serial.println("D+ is already high: switch the radio off first (and check the USB cable is in the PC).");
    } else {
        Serial.println("Sending the key: switch the radio on now.");
        digitalWrite(PIN_LED, HIGH);
        uint32_t start = millis();
        unsigned tries = 0;
        bool answered = false;
        while (millis() - start < WINDOW_MS) {
            send_key();
            tries++;
            if (dplus_held_high(LISTEN_US)) {
                answered = true;
                break;
            }
        }
        release_lines();
        digitalWrite(PIN_LED, LOW);
        if (answered)
            Serial.printf("D+ went high after %u keys (%lu ms). Lines released. Look for a UBOOT device on the PC.\n",
                          tries, (unsigned long)(millis() - start));
        else
            Serial.printf("No answer after %u keys. Lines released.\n", tries);
    }

    while (!digitalRead(PIN_BTN))
        delay(10);
    Serial.println("Ready: radio off, then press BOOT again.");
}
