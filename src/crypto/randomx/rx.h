#include <cstdint>
#include <mutex>
#include <randomx.h>

uint64_t rx_seedheight(const uint64_t height);
void rx_set_main_seedhash(const char *seedhash);
void rx_slow_hash(const char* seedhash, const void *data, size_t length, char *result_hash);
