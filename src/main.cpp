#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Arduino.h>
#include <ESP32Encoder.h>
#include <SD.h>
#include <SPI.h>
#include <bsec.h>
#include <ezButton.h>

#include "data_structs.h"

// Some config vals
#define LED_BUILTIN 2
#define SERIAL_B_RATE 115200

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDR 0x3c

#define ROTARY_ENCODER_BTN 27
#define BACK_BTN 33
#define CONFIRM_BTN 32

// Value for when the encoder skips to a new page
#define ENCODER_PAGE_VAL 7

// Componet objects
Bsec bme680_sensor;
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

ESP32Encoder encoder;

ezButton rotary_encoder_btn(ROTARY_ENCODER_BTN);
ezButton back_btn(BACK_BTN);
ezButton confirm_btn(CONFIRM_BTN);

// Data Structs
sensor_data global_sensor_data;

// Helpers
int menuIndex = 0;  // Aktuell ausgewähltes Menü
int lastEncoderValue = 0;
int maxMenus = 3;  // Anzahl der Menüs

int encoderValue = 0;

bsec_virtual_sensor_t sensorList[13] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_GAS_PERCENTAGE};

void bme680_get_data() {
    /*Collects data from the bm680 and stores them into the global_sensor_data struct*/
    // TODO Add time stamp
    global_sensor_data.bme680_run_in = bme680_sensor.runInStatus;
    global_sensor_data.bVoc = bme680_sensor.breathVocEquivalent;
    global_sensor_data.c_humidity = bme680_sensor.humidity;
    global_sensor_data.c_temperature = bme680_sensor.temperature;
    global_sensor_data.co2_equivalent = bme680_sensor.co2Equivalent;
    global_sensor_data.iaq = bme680_sensor.iaq;
    global_sensor_data.iaq_accuracy = bme680_sensor.iaqAccuracy;
}

void get_sensor_data() {
    /*Collects all the sensor data and stores it into the global_sensor_data struct*/
    // clear the sensor data
    global_sensor_data = sensor_data();

    bme680_get_data();
}

void display_setup() {
    display.begin(SCREEN_ADDR, true);
    display.display();
    delay(2000);
    display.clearDisplay();
}

void setup(void) {
    // Coms init
    Serial.begin(115200);
    delay(1000);

    pinMode(LED_BUILTIN, OUTPUT);

    bme680_sensor.begin(BME68X_I2C_ADDR_HIGH, Wire);

    bme680_sensor.updateSubscription(sensorList, 13, BSEC_SAMPLE_RATE_LP);

    encoder.attachHalfQuad(25, 26);

    display_setup();
}

// Function that is looped forever
void loop(void) {
    rotary_encoder_btn.loop();
    confirm_btn.loop();
    back_btn.loop();

    encoderValue = encoder.getCount();

    unsigned long time_trigger = millis();
    if (bme680_sensor.run()) {  // If new data is available
        get_sensor_data();
    }

    // Drehbereich mit Funktion begrenzen
    if (encoderValue < 0) encoderValue = 0;    // Begrenzung nach unten
    if (encoderValue > 18) encoderValue = 18;  // Begrenzung nach oben

    // Bereiche: 0–6 -> Screen 0, 7–12 -> Screen 1, 13–18 -> Screen 2
    menuIndex = encoderValue / ENCODER_PAGE_VAL;

    Serial.printf("MenuIdex: %i", menuIndex);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 0);

    if (menuIndex == 0) {
        display.println("Screen 1: Messwerte");
        display.printf("Temp: %.2f\n", global_sensor_data.c_temperature);
        display.printf("Pressure: %.2f\n", global_sensor_data.raw_pressure);
        display.printf("Humidity: %.2f\n", global_sensor_data.c_humidity);
        display.printf("IAQ: %.2f\n", global_sensor_data.iaq);
        display.printf("ACC: %u\n", global_sensor_data.iaq_accuracy);
    }

    else if (menuIndex == 1) {
        display.println("Screen 2: Systemstatus");
        display.printf("Power: \n");
        display.printf("Encoder: %d\n", encoderValue);
        display.printf("SD: %s\n", "Nicht eingesteckt");  // muss noch abgefragt werden
        display.printf("Batt: %.1fV\n", 3.7);             // Dummywert
    }

    else if (menuIndex == 2) {
        display.println("Screen 3: Einstellungen");
        display.println("1) Sensor Rate");
        display.println("2) Power Mode");
        display.println("3) Shutdown");
    }

    display.display();

    if (rotary_encoder_btn.isPressed()) {
        Serial.println("Encoder CLICK");

        display.clearDisplay();
        display.setCursor(0, 16);
        display.println("Encoder CLICK!");
        display.display();
        delay(500);
    }
    if (back_btn.isPressed()) {
        Serial.println("Back CLICK");

        display.clearDisplay();
        display.setCursor(0, 16);
        display.println("back CLICK!");
        display.display();
        delay(500);
    }
    if (confirm_btn.isPressed()) {
        Serial.println("Conn CLICK");

        display.clearDisplay();
        display.setCursor(0, 16);
        display.println("Conn CLICK!");
        display.display();
        delay(500);
    }
}
