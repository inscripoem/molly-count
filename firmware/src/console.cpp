// Functional port of src/console.rs.
#include "console.h"

#include <Arduino.h>
#include <string.h>

void console_print(const char *s)
{
    Serial.write((const uint8_t *)s, strlen(s));
}

bool console_read_line(unsigned long idle_timeout_ms, char *buf, size_t buflen)
{
    unsigned long start = millis();
    size_t n = 0;
    while (millis() - start < idle_timeout_ms) {
        while (Serial.available() > 0) {
            int c = Serial.read();
            if (c < 0) break;
            if (c == '\r') {
                // ignore
            } else if (c == '\n') {
                console_print("\r\n");
                buf[n] = '\0';
                if (n == 0) {
                    return false; // empty line: keep polling (Rust behavior)
                }
                return true;
            } else if ((c >= 0x21 && c <= 0x7E) || c == ' ') {
                if (n + 1 < buflen) {
                    buf[n++] = (char)c;
                    Serial.write((uint8_t)c); // echo
                }
            }
            // restart the idle window on any received byte (Rust measured
            // from call start; commands arrive as one burst either way)
            start = millis();
        }
        delay(1); // yield to the scheduler; the Rust firmware busy-polling
                  // had no RTOS to starve, this one does
    }
    buf[n] = '\0';
    return n > 0;
}
