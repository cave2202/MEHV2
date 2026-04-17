#ifndef SD_CARD_H
#define SD_CARD_H
#include <SD.h>
#include "data_structs.h"

void writeFile(fs::FS &fs, const char *path, const char *message);
void readFile(fs::FS &fs, const char *path);
void listDir(fs::FS &fs, const char *dirname, uint8_t levels);
void createDir(fs::FS &fs, const char *path);
void removeDir(fs::FS &fs, const char *path);
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);
void renameFile(fs::FS &fs, const char *path1, const char *path2);
void deleteFile(fs::FS &fs, const char *path);
void testFileIO(fs::FS &fs, const char *path);
void write_sensor_data_to_csv(fs::FS &fs, sensor_data *sensor_data, system_status *system_data,const char *file);
#endif