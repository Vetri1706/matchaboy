#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32) && defined(MATCHA_BUILD_SHARED)
#define MATCHA_API __declspec(dllexport)
#elif defined(_WIN32) && defined(MATCHA_USE_SHARED)
#define MATCHA_API __declspec(dllimport)
#else
#define MATCHA_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

// All calls are synchronous. Serialize calls on one handle. Borrowed pointers
// are stable until gym_destroy; do not access them concurrently with gym_step.
MATCHA_API void *gym_create(const char *rom_path, int num_instances);
MATCHA_API void gym_destroy(void *handle);
MATCHA_API void gym_step(void *handle, const uint8_t *actions, float *rewards, uint8_t *dones);
MATCHA_API void gym_get_observations(void *handle, uint8_t *screen_buffers); // Explicit copy, N*160*144 bytes.
MATCHA_API void gym_get_ram(void *handle, int instance_id, uint8_t **ram_ptr); // Real C000-DFFF WRAM, 8192 bytes.
MATCHA_API int gym_step_checked(void *handle, const uint8_t *actions, size_t count,
                     float *rewards, uint8_t *dones);
MATCHA_API int gym_reset(void *handle, int instance_id); // -1 resets the whole batch.
MATCHA_API const char *gym_last_error(void); // Thread-local; valid until next API call on this thread.
MATCHA_API int gym_count(void *handle);
MATCHA_API int gym_observation_ptr(void *handle, int instance_id, const uint8_t **pixels, size_t *length);
MATCHA_API int gym_ram_size(void *handle);
MATCHA_API uint64_t gym_cycles(void *handle, int instance_id);
MATCHA_API uint64_t gym_frames(void *handle, int instance_id);
MATCHA_API int gym_peek(void *handle, int instance_id, uint16_t address, uint8_t *value);
// width=1,2,4 bytes, little endian; delta=0 absolute,1 difference; signed_value=0/1.
// reward=sum(scale*watched_value_or_delta). Watching never changes the machine.
MATCHA_API int gym_add_watch(void *handle, uint16_t address, uint8_t width, float scale,
                  uint8_t delta, uint8_t signed_value);
MATCHA_API int gym_clear_watches(void *handle);
MATCHA_API int gym_set_terminal(void *handle, uint16_t address, uint8_t mask, uint8_t value);
MATCHA_API int gym_clear_terminal(void *handle);
MATCHA_API int gym_set_max_steps(void *handle, uint64_t frames); // 0 disables time limit.
MATCHA_API int gym_episode_flags(void *handle, int instance_id, uint8_t *terminated, uint8_t *truncated);
#ifdef __cplusplus
}
#endif
