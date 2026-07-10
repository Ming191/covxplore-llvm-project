//===-- MCDCTraceRuntime.c - compiler-integrated MC/DC tracing -----------===//
//
// Part of downstream covXplore LLVM toolchain. See toolchain/README.md.
//
//===----------------------------------------------------------------------===//

#include <stdint.h>
#include <stdlib.h>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

#define MCDC_TRACE_MAGIC 0x4344434dU
#define MCDC_TRACE_VERSION 1U
#define MCDC_MAX_CONDITIONS 32U

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint64_t reserved;
} MCDCTraceHeader;

typedef struct {
  uint64_t test_id;
  uint64_t logical_decision_id;
  uint64_t physical_instance_id;
  uint64_t execution_id;
  uint64_t evaluated_mask;
  uint64_t condition_values;
  uint8_t decision_result;
  uint8_t status;
  uint16_t reserved;
} MCDCEvent;

typedef struct {
  uint64_t function_hash;
  uint32_t bitmap_index;
  uint64_t evaluated_mask;
  uint64_t condition_values;
  uint8_t active;
} MCDCContext;

#define MCDC_MAX_DEPTH 16U
static _Thread_local MCDCContext mcdc_contexts[MCDC_MAX_DEPTH];
static _Thread_local uint32_t mcdc_depth;
static uint64_t mcdc_next_execution_id = 1;

#if defined(__unix__) || defined(__APPLE__)
static int mcdc_fd = -2;
static int mcdc_open_trace(void) {
  if (mcdc_fd != -2)
    return mcdc_fd;

  const char *path = getenv("MCDC_TRACE_FILE");
  if (!path || !*path) {
    mcdc_fd = -1;
    return -1;
  }

  int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) {
    mcdc_fd = -1;
    return -1;
  }

  MCDCTraceHeader header = {MCDC_TRACE_MAGIC, MCDC_TRACE_VERSION,
                            sizeof(MCDCTraceHeader), 0};
  // ponytail: concurrent first-open can repeat header. Upgrade to locked init
  // when shared file sinks are supported.
  if (write(fd, &header, sizeof(header)) != sizeof(header)) {
    close(fd);
    mcdc_fd = -1;
    return -1;
  }
  mcdc_fd = fd;
  return fd;
}

static void mcdc_write_event(const MCDCEvent *event) {
  int fd = mcdc_open_trace();
  if (fd >= 0)
    (void)write(fd, event, sizeof(*event));
}
#else
static void mcdc_write_event(const MCDCEvent *event) { (void)event; }
#endif

void __mcdc_trace_begin(const char *function_name, uint64_t function_hash,
                        uint32_t bitmap_index, uint32_t bitmap_bytes) {
  (void)function_name;
  (void)bitmap_bytes;
  if (mcdc_depth >= MCDC_MAX_DEPTH)
    return;
  MCDCContext *context = &mcdc_contexts[mcdc_depth++];
  context->function_hash = function_hash;
  context->bitmap_index = bitmap_index;
  context->evaluated_mask = 0;
  context->condition_values = 0;
  context->active = 1;
}

void __mcdc_trace_condition(const char *function_name, uint64_t function_hash,
                            uint32_t condition_index, uint8_t value) {
  (void)function_name;
  if (mcdc_depth == 0 || condition_index >= MCDC_MAX_CONDITIONS)
    return;
  MCDCContext *context = &mcdc_contexts[mcdc_depth - 1];
  if (!context->active || context->function_hash != function_hash)
    return;

  const uint64_t bit = UINT64_C(1) << condition_index;
  context->evaluated_mask |= bit;
  if (value)
    context->condition_values |= bit;
  else
    context->condition_values &= ~bit;
}

void __mcdc_trace_complete(const char *function_name, uint64_t function_hash,
                           uint32_t bitmap_index, uint8_t result) {
  (void)function_name;
  if (mcdc_depth == 0)
    return;
  uint32_t index = mcdc_depth;
  while (index != 0) {
    --index;
    MCDCContext *candidate = &mcdc_contexts[index];
    if (candidate->active && candidate->function_hash == function_hash &&
        candidate->bitmap_index == bitmap_index)
      break;
  }
  if (index == mcdc_depth)
    return;
  MCDCContext *context = &mcdc_contexts[index];

  MCDCEvent event = {
      0,
      function_hash,
      ((uint64_t)bitmap_index << 32) ^ function_hash,
      __atomic_fetch_add(&mcdc_next_execution_id, 1, __ATOMIC_RELAXED),
      context->evaluated_mask,
      context->condition_values,
      result != 0,
      0,
      0,
  };
  context->active = 0;
  if (index + 1 == mcdc_depth)
    --mcdc_depth;
  mcdc_write_event(&event);
}
