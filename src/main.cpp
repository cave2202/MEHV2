#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Arduino.h>
#include <FS.h>
#include <RTClib.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <bsec2.h>

#include "data_structs.h"
#include "sd_card.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDR 0x3c

#define RAIN_SENSOR_ANALOG_PIN 35
#define RAIN_SENSOR_DIGITAL_PIN 14

#define SDA_PIN 21
#define SCL_PIN 22

#define SD_CS_PIN 13

#define BME_SAMPLE_RATE BSEC_SAMPLE_RATE_LP
#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 3
#define SLEEP_DURATION_US (TIME_TO_SLEEP * uS_TO_S_FACTOR)

#define BME680_I2C_ADDR BME68X_I2C_ADDR_HIGH

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

RTC_DS1307 rtc;

Bsec2 envSensor;

sensor_data global_sensor_data;
system_status global_system_status;

bsec_virtual_sensor_t sensor_list[12] = {
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_STABILIZATION_STATUS,
    BSEC_OUTPUT_RUN_IN_STATUS,
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
};

RTC_DATA_ATTR uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
RTC_DATA_ATTR bool hasBsecState = false;
RTC_DATA_ATTR int bootCount = 0;

void go_to_sleep() {
    // Save BSEC2 state to RTC memory before sleeping
    envSensor.getState(bsecState);
    hasBsecState = true;

    Serial.println("Saving state, going to sleep...");
    Serial.flush();
    digitalWrite(LED_BUILTIN, LOW);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
    esp_deep_sleep_start();
}

void print_sensor_data(const sensor_data& data) {
    Serial.println("=== Sensor Data ===");
    Serial.printf("  Timestamp     : %lld\n", data.timestamp);
    Serial.printf("  Raw Temp      : %.2f °C\n", data.raw_temperature);
    Serial.printf("  Raw Humidity  : %.2f %%\n", data.raw_humidity);
    Serial.printf("  Raw Pressure  : %.2f hPa\n", data.raw_pressure);
    Serial.printf("  Comp Temp     : %.2f °C\n", data.c_temperature);
    Serial.printf("  Comp Humidity : %.2f %%\n", data.c_humidity);
    Serial.printf("  CO2 equiv     : %.1f ppm\n", data.co2_equivalent);
    Serial.printf("  bVOC equiv    : %.2f ppm\n", data.bVoc);
    Serial.printf("  IAQ           : %.1f  (accuracy %d/3)\n", data.iaq, data.iaq_accuracy);
    Serial.printf("  Light level   : %d\n", data.light_level);
    Serial.printf("  BME680 run-in : %s\n", data.bme680_run_in ? "done" : "in progress");
    Serial.printf("  Is Raining : %s\n", data.is_raining ? "yes" : "no");
    Serial.printf("  Rain Voltage   : %d\n", data.raing_voltage);

    Serial.println("===================");
}

void bsec_data_callback(const bme68x_data data, const bsecOutputs outputs, Bsec2 bsec) {
    if (!outputs.nOutputs) return;

    global_sensor_data.timestamp = (int64_t)time(nullptr);

    for (uint8_t i = 0; i < outputs.nOutputs; i++) {
        const bsecData& o = outputs.output[i];
        switch (o.sensor_id) {
            case BSEC_OUTPUT_RAW_TEMPERATURE:
                global_sensor_data.raw_temperature = o.signal;
                break;
            case BSEC_OUTPUT_RAW_HUMIDITY:
                global_sensor_data.raw_humidity = o.signal;
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                global_sensor_data.raw_pressure = o.signal;
                break;

            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                global_sensor_data.c_temperature = o.signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                global_sensor_data.c_humidity = o.signal;
                break;

            case BSEC_OUTPUT_CO2_EQUIVALENT:
                global_sensor_data.co2_equivalent = o.signal;
                break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                global_sensor_data.bVoc = o.signal;
                break;

            case BSEC_OUTPUT_IAQ:
                global_sensor_data.iaq = o.signal;
                global_sensor_data.iaq_accuracy = o.accuracy;
                break;

            case BSEC_OUTPUT_RUN_IN_STATUS:
                global_sensor_data.bme680_run_in = o.accuracy;
                break;
        }
    }

    if (global_system_status.sd_card_avalible) {
        write_sensor_data_to_csv(SD, &global_sensor_data, &global_system_status, "/sens.csv");
    }

    if (!digitalRead(RAIN_SENSOR_DIGITAL_PIN)) {
        Serial.println("RAINING!");
        int rain_volt = analogRead(RAIN_SENSOR_ANALOG_PIN);
        global_sensor_data.is_raining = true;
        global_sensor_data.raing_voltage = rain_volt;
    } else {
        global_sensor_data.is_raining = false;
    }

    print_sensor_data(global_sensor_data);

    if (!global_system_status.display_active) {
        go_to_sleep();
    }
}

void checkBsecStatus(Bsec2 bsec) {
    if (bsec.status < BSEC_OK) {
        Serial.println("BSEC error code : " + String(bsec.status));
        // errLeds(); /* Halt in case of failure */
    } else if (bsec.status > BSEC_OK) {
        Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK) {
        Serial.println("BME68X error code : " + String(bsec.sensor.status));
        // errLeds(); /* Halt in case of failure */
    } else if (bsec.sensor.status > BME68X_OK) {
        Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}

void init_sd_card() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("Card Mount Failed - continuing without SD");
        global_system_status.sd_card_avalible = false;
    } else {
        uint8_t cardType = SD.cardType();
        if (cardType == CARD_NONE) {
            Serial.println("No SD card attached");
            global_system_status.sd_card_avalible = false;
        } else {
            Serial.println("Found SD card!");
            global_system_status.sd_card_avalible = true;
            testFileIO(SD, "/test.txt");
        }
    }
}

void init_bme680() {
    // Check if the Sensor is avalible
    if (!envSensor.begin(BME680_I2C_ADDR, Wire)) {
        Serial.println("Error with BME680!");
        checkBsecStatus(envSensor);
        while (true) delay(1000);
    }

    if (!envSensor.updateSubscription(sensor_list, ARRAY_LEN(sensor_list), BME_SAMPLE_RATE)) {
        Serial.println("Could not Subscribe to sensors!");
    }

    // Check if there is already a state in the RTC RAM
    if (hasBsecState) {
        Serial.println("Restoring BSEC2 state...");
        if (!envSensor.setState(bsecState)) {
            Serial.println("State restore failed, starting fresh.");
            hasBsecState = false;
            memset(bsecState, 0, sizeof(bsecState));
        }
    } else {
        Serial.println("First boot, no BSEC2 state yet.");
    }

    if (BME_SAMPLE_RATE == BSEC_SAMPLE_RATE_ULP) {
        envSensor.setTemperatureOffset(TEMP_OFFSET_ULP);
    } else if (BME_SAMPLE_RATE == BSEC_SAMPLE_RATE_LP) {
        envSensor.setTemperatureOffset(TEMP_OFFSET_LP);
    }

    envSensor.attachCallback(bsec_data_callback);
}

void init_rtc() {
    if (!rtc.begin()) {
        Serial.println("DS1307 not found!");
        global_system_status.rtc_avalible = false;

    } else {
        if (!rtc.isrunning()) {
            Serial.println("DS1307 not running, setting time...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }

        // Sync DS1307 time to ESP32 internal clock
        DateTime now = rtc.now();
        struct timeval tv = {now.unixtime(), 0};
        settimeofday(&tv, NULL);

        Serial.printf("Time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());

        global_system_status.rtc_avalible = true;
    }
}

void display_setup() {
    if (!display.begin(SCREEN_ADDR, true)) {
        Serial.println("Display Init failed!");
        global_system_status.display_avalible = false;
        return;
    }
    display.display();
    delay(1000);
    display.clearDisplay();
    global_system_status.display_avalible = true;
}

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);

    pinMode(LED_BUILTIN, OUTPUT);

    pinMode(RAIN_SENSOR_ANALOG_PIN, INPUT);
    pinMode(RAIN_SENSOR_DIGITAL_PIN, INPUT);

    digitalWrite(LED_BUILTIN, HIGH);

    init_rtc();

    init_sd_card();

    init_bme680();

    Serial.println("BME680 + BSEC2 ready.");
}

void loop() {
    if (!envSensor.run()) {
        if (envSensor.status < BSEC_OK || envSensor.sensor.status < BME68X_OK) {
            checkBsecStatus(envSensor);
            while (true) delay(1000);
        }
    }
}