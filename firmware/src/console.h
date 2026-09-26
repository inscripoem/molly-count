// molly-count — USB-CDC command console. Functional port of
// src/console.rs: line reads with idle timeout, printable echo, CR ignored.
// `Serial` is the hardware USB-Serial-JTAG (ARDUINO_USB_MODE=1).
#pragma once

#include <stddef.h>

void console_print(const char *s);

// Poll for one line until the idle timeout expires. Returns true when a
// non-empty line arrived (NUL-terminated in buf); false on timeout, empty
// lines, or whitespace-only lines.
bool console_read_line(unsigned long idle_timeout_ms, char *buf, size_t buflen);
