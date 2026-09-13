//! 200x200 2bpp packed framebuffer for the 1.54" (G) four-color panel.
//!
//! Pure `core` + embedded-graphics logic, unit-tested on the host.
//! Layout: 4 pixels per byte, 2 bits per pixel, MSB first, 50 bytes per row.
//! Color codes: 0=black, 1=white, 2=yellow, 3=red.

use core::convert::Infallible;

use embedded_graphics::geometry::Size;
use embedded_graphics::pixelcolor::raw::{RawData, RawU2};
use embedded_graphics::pixelcolor::PixelColor;
use embedded_graphics::prelude::{DrawTarget, OriginDimensions, Pixel};

pub const WIDTH: usize = 200;
pub const HEIGHT: usize = 200;
pub const ROW_BYTES: usize = WIDTH / 4;
pub const FRAME_LEN: usize = ROW_BYTES * HEIGHT;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EpdColor {
    Black,
    White,
    Yellow,
    Red,
}

impl EpdColor {
    pub const fn bits(self) -> u8 {
        match self {
            Self::Black => 0x0,
            Self::White => 0x1,
            Self::Yellow => 0x2,
            Self::Red => 0x3,
        }
    }
}

impl PixelColor for EpdColor {
    type Raw = RawU2;
}

impl From<RawU2> for EpdColor {
    fn from(v: RawU2) -> Self {
        match v.into_inner() {
            0 => Self::Black,
            1 => Self::White,
            2 => Self::Yellow,
            _ => Self::Red,
        }
    }
}

/// 200x200 2bpp packed framebuffer. `new()` starts all-white.
pub struct Framebuffer {
    data: [u8; FRAME_LEN],
}

impl Framebuffer {
    pub const fn new() -> Self {
        // 0b01_01_01_01 = four white pixels
        Self {
            data: [0x55; FRAME_LEN],
        }
    }

    pub fn clear(&mut self, color: EpdColor) {
        let c = color.bits();
        self.data = [(c << 6) | (c << 4) | (c << 2) | c; FRAME_LEN];
    }

    pub fn set_pixel(&mut self, x: usize, y: usize, color: EpdColor) {
        assert!(x < WIDTH && y < HEIGHT, "pixel out of bounds");
        let idx = y * ROW_BYTES + x / 4;
        let shift = 6 - (x % 4) * 2;
        self.data[idx] = (self.data[idx] & !(0b11 << shift)) | (color.bits() << shift);
    }

    pub fn packed(&self) -> &[u8] {
        &self.data
    }

    /// Replace the whole framebuffer with a pre-rendered packed image.
    pub fn load_packed(&mut self, data: &[u8; FRAME_LEN]) {
        self.data = *data;
    }

    /// Draw a 1bpp mask (MSB-first rows) in `color` at (x, y).
    pub fn blit_mask(
        &mut self,
        x: i32,
        y: i32,
        w: usize,
        h: usize,
        row_bytes: usize,
        mask: &[u8],
        color: EpdColor,
    ) {
        for yy in 0..h as i32 {
            for xx in 0..w as i32 {
                let px = x + xx;
                let py = y + yy;
                if (0..WIDTH as i32).contains(&px) && (0..HEIGHT as i32).contains(&py) {
                    let bit = mask[yy as usize * row_bytes + xx as usize / 8]
                        & (0x80 >> (xx % 8));
                    if bit != 0 {
                        self.set_pixel(px as usize, py as usize, color);
                    }
                }
            }
        }
    }
}

impl OriginDimensions for Framebuffer {
    fn size(&self) -> Size {
        Size::new(WIDTH as u32, HEIGHT as u32)
    }
}

impl DrawTarget for Framebuffer {
    type Color = EpdColor;
    type Error = Infallible;

    fn draw_iter<I>(&mut self, pixels: I) -> Result<(), Self::Error>
    where
        I: IntoIterator<Item = Pixel<Self::Color>>,
    {
        for Pixel(pos, color) in pixels {
            if (0..WIDTH as i32).contains(&pos.x) && (0..HEIGHT as i32).contains(&pos.y) {
                self.set_pixel(pos.x as usize, pos.y as usize, color);
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn new_is_all_white() {
        let fb = Framebuffer::new();
        // white = 0b01 per pixel -> every byte is 0b01_01_01_01
        assert!(fb.packed().iter().all(|&b| b == 0x55));
        assert_eq!(fb.packed().len(), 10_000);
    }

    #[test]
    fn clear_packs_four_pixels_per_byte() {
        let mut fb = Framebuffer::new();
        fb.clear(EpdColor::Red);
        // red = 0b11 per pixel -> 0b11_11_11_11
        assert!(fb.packed().iter().all(|&b| b == 0xFF));
        fb.clear(EpdColor::Yellow);
        // yellow = 0b10 -> 0b10_10_10_10
        assert!(fb.packed().iter().all(|&b| b == 0xAA));
    }

    #[test]
    fn set_pixel_is_msb_first() {
        let mut fb = Framebuffer::new();
        fb.clear(EpdColor::Black);
        // pixel 0 = bits 7:6
        fb.set_pixel(0, 0, EpdColor::Red);
        assert_eq!(fb.packed()[0], 0b1100_0000);
        // pixel 3 = bits 1:0
        fb.set_pixel(3, 0, EpdColor::Yellow);
        assert_eq!(fb.packed()[0], 0b1100_0010);
        // second row starts at byte 50
        fb.set_pixel(0, 1, EpdColor::White);
        assert_eq!(fb.packed()[50], 0b0100_0000);
    }

    #[test]
    fn set_pixel_overwrites_without_touching_neighbors() {
        let mut fb = Framebuffer::new();
        fb.clear(EpdColor::White);
        fb.set_pixel(5, 7, EpdColor::Red);
        let idx = 7 * ROW_BYTES + 5 / 4;
        // x=5 -> bits 5:4 of the byte containing pixels 4..=7
        assert_eq!(fb.packed()[idx] & 0b0011_0000, 0b0011_0000); // pixel 5 red
        assert_eq!(fb.packed()[idx] & 0b1100_1111, 0b01_00_01_01); // 4, 6, 7 white
    }

    #[test]
    fn draw_target_draws_into_buffer() {
        use embedded_graphics::geometry::Point;
        use embedded_graphics::prelude::*;
        use embedded_graphics::primitives::{PrimitiveStyle, Rectangle};
        let mut fb = Framebuffer::new();
        let dot = Rectangle::new(Point::new(0, 0), Size::new(2, 2))
            .into_styled(PrimitiveStyle::with_fill(EpdColor::Red))
            .draw(&mut fb);
        assert!(dot.is_ok());
        // 2x2 red rect covers pixels (0,0) and (1,0) in byte 0; (2,3) stay white
        assert_eq!(fb.packed()[0], 0b11_11_01_01);
    }

    #[test]
    fn load_packed_replaces_content() {
        let mut fb = Framebuffer::new();
        let mut bg = [0u8; FRAME_LEN];
        bg[123] = 0xAB;
        fb.load_packed(&bg);
        assert_eq!(fb.packed()[123], 0xAB);
        assert_eq!(fb.packed()[0], 0);
    }

    #[test]
    fn blit_mask_draws_only_set_bits() {
        let mut fb = Framebuffer::new();
        fb.clear(EpdColor::White);
        // 8x1 mask, MSB first: set pixels 0, 2, 3, 6
        let mask = [0b1011_0010];
        fb.blit_mask(0, 0, 8, 1, 1, &mask, EpdColor::Red);
        // 0 red, 1 white, 2 red, 3 red, 4..7 white
        assert_eq!(fb.packed()[0], (3 << 6) | (1 << 4) | (3 << 2) | 3);
    }

    #[test]
    fn blit_mask_clips_at_edges() {
        let mut fb = Framebuffer::new();
        fb.clear(EpdColor::White);
        // drawing past the right edge must not panic or wrap
        fb.blit_mask(196, 0, 8, 1, 1, &[0xFF], EpdColor::Red);
        // pixels 196..199 red, nothing wraps to the next row
        assert_eq!(fb.packed()[49], 0xFF);
        assert_eq!(fb.packed()[50], 0x55);
    }
}
