#include <RTClib.h>

struct StorageRecord {
  DateTime date;
  float temp;
};

StorageRecord read_mem(int index);
