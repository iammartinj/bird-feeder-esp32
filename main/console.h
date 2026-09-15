// A line console on the USB port (115200, any terminal). Network and clock
// commands are handled here, the scene's in app_main.cpp.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void console_init(void);

// The scene's commands. Writes the answer and returns true when it knew the
// command.
bool app_command(const char *line, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
