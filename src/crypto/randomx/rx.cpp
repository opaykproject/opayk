#include <cstring>
#include <util/system.h>
#include <util/strencodings.h>
#include <crypto/randomx/rx.h>

static const size_t HASH_SIZE = 32;

static std::mutex main_cache_lock;
static randomx_cache *main_cache = NULL;
static char main_seedhash[HASH_SIZE];
static int main_seedhash_set = 0;

static std::mutex secondary_cache_lock;
static randomx_cache *secondary_cache = NULL;
static char secondary_seedhash[HASH_SIZE];
static int secondary_seedhash_set = 0;

static randomx_vm *main_vm_light = NULL;
static randomx_vm *secondary_vm_light = NULL;

static bool is_main(const char* seedhash) { return main_seedhash_set && (memcmp(seedhash, main_seedhash, HASH_SIZE) == 0); }
static bool is_secondary(const char* seedhash) { return secondary_seedhash_set && (memcmp(seedhash, secondary_seedhash, HASH_SIZE) == 0); }

#define SEEDHASH_EPOCH_BLOCKS	2048
#define SEEDHASH_EPOCH_LAG		64

uint64_t rx_seedheight(const uint64_t height) {
  return (height <= SEEDHASH_EPOCH_BLOCKS+SEEDHASH_EPOCH_LAG) ? 0 : (height - SEEDHASH_EPOCH_LAG - 1) & ~(SEEDHASH_EPOCH_BLOCKS-1);
}

static bool rx_alloc_cache(randomx_flags flags, randomx_cache** cache)
{
  if (*cache) {
    return true;
  }
  *cache = randomx_alloc_cache(flags);
  if (!*cache) return error("Couldn't allocate RandomX cache");
  return true;
}

void rx_set_main_seedhash(const char *seedhash) {
  std::unique_lock<std::mutex> lock(main_cache_lock);
  if (is_main(seedhash)) {
    return;
  }
  memcpy(main_seedhash, seedhash, HASH_SIZE);
  main_seedhash_set = 1;

  const randomx_flags flags = randomx_get_flags();
  if (!rx_alloc_cache(flags, &main_cache)) return;

  randomx_init_cache(main_cache, seedhash, HASH_SIZE);
}

static bool rx_init_light_vm(randomx_flags flags, randomx_vm** vm, randomx_cache* cache)
{
  if (*vm) {
    randomx_vm_set_cache(*vm, cache);
    return true;
  }

  *vm = randomx_create_vm(flags, cache, NULL);
  if (!*vm) return error("Couldn't allocate RandomX light VM");
  return true;
}

void rx_slow_hash(const char* seedhash, const void *data, size_t length, char *result_hash) {
  const randomx_flags flags = randomx_get_flags();

  // Fast path (seedhash == main_seedhash)
  std::unique_lock<std::mutex> lock(main_cache_lock);
  if (is_main(seedhash)) {
    if (!rx_init_light_vm(flags, &main_vm_light, main_cache)) return;
    randomx_calculate_hash(main_vm_light, data, length, result_hash);
    return;
  }

  // Slow path (seedhash != main_seedhash, but seedhash == secondary_seedhash)
  // Multiple threads can run in parallel in light mode, 10-15 ms per hash per thread
  if (!secondary_cache) {
    std::unique_lock<std::mutex> secondary_lock(secondary_cache_lock);
    if (!secondary_cache) {
      if (!rx_alloc_cache(flags, &secondary_cache)) return;
      randomx_init_cache(secondary_cache, seedhash, HASH_SIZE);
      memcpy(secondary_seedhash, seedhash, HASH_SIZE);
      secondary_seedhash_set = 1;
    }
  }

  std::unique_lock<std::mutex> secondary_lock(secondary_cache_lock);
  if (is_secondary(seedhash)) {
    if (!rx_init_light_vm(flags, &secondary_vm_light, secondary_cache)) return;
    randomx_calculate_hash(secondary_vm_light, data, length, result_hash);
    return;
  }

  // Slowest path (seedhash != main_seedhash, seedhash != secondary_seedhash)
  // Only one thread runs at a time and updates secondary_seedhash if needed, up to 200-500 ms per hash
  if (!is_secondary(seedhash)) {
    randomx_init_cache(secondary_cache, seedhash, HASH_SIZE);
    memcpy(secondary_seedhash, seedhash, HASH_SIZE);
    secondary_seedhash_set = 1;
  }
  if (!rx_init_light_vm(flags, &secondary_vm_light, secondary_cache)) return;
  randomx_calculate_hash(secondary_vm_light, data, length, result_hash);
}
