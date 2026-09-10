#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// All calls are synchronous. Serialize calls on one handle. Borrowed pointers
// are stable until gym_destroy; do not access them concurrently with gym_step.
void *gym_create(const char *rom_path, int num_instances);
void gym_destroy(void *handle);
void gym_step(void *handle, const uint8_t *actions, float *rewards, uint8_t *dones);
void gym_get_observations(void *handle, uint8_t *screen_buffers); // Explicit copy, N*160*144 bytes.
void gym_get_ram(void *handle, int instance_id, uint8_t **ram_ptr); // Real C000-DFFF WRAM, 8192 bytes.
int gym_step_checked(void *handle, const uint8_t *actions, size_t count,
                     float *rewards, uint8_t *dones);
int gym_reset(void *handle, int instance_id); // -1 resets the whole batch.
const char *gym_last_error(void); // Thread-local; valid until next API call on this thread.
int gym_count(void *handle);
int gym_observation_ptr(void *handle, int instance_id, const uint8_t **pixels, size_t *length);
int gym_ram_size(void *handle);
uint64_t gym_cycles(void *handle, int instance_id);
uint64_t gym_frames(void *handle, int instance_id);
int gym_peek(void *handle, int instance_id, uint16_t address, uint8_t *value);
// width=1,2,4 bytes, little endian; delta=0 absolute,1 difference; signed_value=0/1.
// reward=sum(scale*watched_value_or_delta). Watching never changes the machine.
int gym_add_watch(void *handle, uint16_t address, uint8_t width, float scale,
                  uint8_t delta, uint8_t signed_value);
int gym_clear_watches(void *handle);
int gym_set_terminal(void *handle, uint16_t address, uint8_t mask, uint8_t value);
int gym_clear_terminal(void *handle);
int gym_set_max_steps(void *handle, uint64_t frames); // 0 disables time limit.
int gym_episode_flags(void *handle, int instance_id, uint8_t *terminated, uint8_t *truncated);
#ifdef __cplusplus
}
#endif
