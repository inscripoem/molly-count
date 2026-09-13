//! USB-CDC command console (`espflash monitor` / pyserial compatible).

use esp_hal::time::{Duration, Instant};
use esp_hal::usb::usb_serial_jtag::UsbSerialJtag;

pub struct Console {
    usb: UsbSerialJtag<'static, esp_hal::Blocking>,
}

impl Console {
    pub fn new(usb: UsbSerialJtag<'static, esp_hal::Blocking>) -> Self {
        Self { usb }
    }

    pub fn print(&mut self, s: &str) {
        self.usb.write(s.as_bytes()).ok();
    }

    /// Poll for one line until the idle timeout expires.
    /// Returns `None` when nothing arrived (or only whitespace).
    pub fn read_line(&mut self, idle_timeout_ms: u64) -> Option<heapless::String<64>> {
        let start = Instant::now();
        let mut buf = heapless::String::<64>::new();
        while start.elapsed() < Duration::from_millis(idle_timeout_ms) {
            match self.usb.read_byte() {
                Ok(b'\r') => {}
                Ok(b'\n') => {
                    self.print("\r\n");
                    return if buf.is_empty() { None } else { Some(buf) };
                }
                Ok(c) if c.is_ascii_graphic() || c == b' ' => {
                    let _ = buf.push(c as char);
                    let _ = self.usb.write(&[c]);
                }
                Ok(_) => {}
                Err(_) => {} // RX empty, keep polling
            }
        }
        if buf.is_empty() { None } else { Some(buf) }
    }
}
