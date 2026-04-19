#ifndef DATA_STRUCTS_H
#define DATA_STRUCTS_H
struct sensor_data {
    float raw_temperature; // Celsius
    float raw_humidity; // % 
    float raw_pressure; // Pa

    float c_temperature; // Celsius
    float c_humidity; // %
    // float c_pressure;
    
    float co2_equivalent; // ppm
    float bVoc; // ppm

    float iaq; // 0-500
    int iaq_accuracy;
    int light_level;

    bool is_raining;
    int raing_voltage;

    int64_t timestamp; // Unix timestamp, when the first measurement was created

    bool bme680_run_in;

};

struct system_status {
    bool sd_card_avalible; // true if an sdcard is pressent
    bool is_charing;
    bool display_active; // if the display is currently beeing used
    bool display_avalible; // if the display is connected
    bool rtc_avalible;
    float battery_percentage;
    float battery_milli_volts;
    bool in_ulp_mode;
    bool burn_in_mode;
}; 

#endif