#![no_std]
#![no_main]
//! Candidate C: ttf-parser + a row-sweep signed-area coverage rasterizer (the font-rs accumulation
//! restricted to one scanline at a time), f32 only, no alloc. Two TTF subsets in .rodata.
use core_maths::CoreFloat;
use ttf_parser::{Face, GlyphId, OutlineBuilder};
static mut FB: [u8; 240 * 240] = [0; 240 * 240];
static REGULAR: &[u8] = include_bytes!("regular_t1.ttf");
static SEMIBOLD: &[u8] = include_bytes!("semibold_ascii.ttf");
#[panic_handler] fn p(_: &core::panic::PanicInfo) -> ! { loop {} }

const MAX_EDGES: usize = 512;
const MAX_W: usize = 96;
#[derive(Clone, Copy, Default)] struct Edge { x0: f32, y0: f32, x1: f32, y1: f32 }
static mut EDGES: [Edge; MAX_EDGES] = [Edge { x0: 0.0, y0: 0.0, x1: 0.0, y1: 0.0 }; MAX_EDGES];
static mut ROW: [f32; MAX_W + 2] = [0.0; MAX_W + 2];

struct Flattener { scale: f32, ox: f32, oy: f32, n: usize, last: (f32, f32), start: (f32, f32), min: (f32, f32), max: (f32, f32) }
impl Flattener {
    fn push(&mut self, x0: f32, y0: f32, x1: f32, y1: f32) {
        if self.n < MAX_EDGES { unsafe { EDGES[self.n] = Edge { x0, y0, x1, y1 }; } self.n += 1; }
        for (x, y) in [(x0, y0), (x1, y1)] { self.min.0 = self.min.0.min(x); self.min.1 = self.min.1.min(y); self.max.0 = self.max.0.max(x); self.max.1 = self.max.1.max(y); }
    }
    fn map(&self, x: f32, y: f32) -> (f32, f32) { (x * self.scale + self.ox, -y * self.scale + self.oy) }
}
impl OutlineBuilder for Flattener {
    fn move_to(&mut self, x: f32, y: f32) { self.last = self.map(x, y); self.start = self.last; }
    fn line_to(&mut self, x: f32, y: f32) { let p = self.map(x, y); self.push(self.last.0, self.last.1, p.0, p.1); self.last = p; }
    fn quad_to(&mut self, x1: f32, y1: f32, x: f32, y: f32) {
        let c = self.map(x1, y1); let p = self.map(x, y); let (a0, a1) = self.last;
        let d = (a0 - 2.0 * c.0 + p.0).abs().max((a1 - 2.0 * c.1 + p.1).abs());
        let n = ((d * 2.0).sqrt().ceil() as i32).clamp(1, 16);
        let mut prev = self.last;
        for i in 1..=n { let t = i as f32 / n as f32; let mt = 1.0 - t;
            let q = (mt * mt * a0 + 2.0 * mt * t * c.0 + t * t * p.0, mt * mt * a1 + 2.0 * mt * t * c.1 + t * t * p.1);
            self.push(prev.0, prev.1, q.0, q.1); prev = q; }
        self.last = p;
    }
    fn curve_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x: f32, y: f32) {
        let c1 = self.map(x1, y1); let c2 = self.map(x2, y2); let p = self.map(x, y); let a = self.last; let mut prev = a;
        for i in 1..=16 { let t = i as f32 / 16.0; let mt = 1.0 - t;
            let q = (mt*mt*mt*a.0 + 3.0*mt*mt*t*c1.0 + 3.0*mt*t*t*c2.0 + t*t*t*p.0, mt*mt*mt*a.1 + 3.0*mt*mt*t*c1.1 + 3.0*mt*t*t*c2.1 + t*t*t*p.1);
            self.push(prev.0, prev.1, q.0, q.1); prev = q; }
        self.last = p;
    }
    fn close(&mut self) { if self.last != self.start { self.push(self.last.0, self.last.1, self.start.0, self.start.1); } self.last = self.start; }
}

/// font-rs's signed-area accumulation for the part of one edge inside scanline `y`.
fn accumulate_row(row: &mut [f32], e: &Edge, y: f32, w: usize) {
    if (e.y0 - e.y1).abs() <= 1e-6 { return; }
    let (dir, x0, y0, x1, y1) = if e.y0 < e.y1 { (1.0, e.x0, e.y0, e.x1, e.y1) } else { (-1.0, e.x1, e.y1, e.x0, e.y0) };
    let top = y0.max(y); let bot = y1.min(y + 1.0);
    if bot <= top { return; }
    let dxdy = (x1 - x0) / (y1 - y0);
    let xa = x0 + (top - y0) * dxdy; let xb = x0 + (bot - y0) * dxdy;
    let dy = bot - top; let d = dy * dir;
    let (xl, xr) = if xa < xb { (xa, xb) } else { (xb, xa) };
    let xl = xl.max(0.0); let xr = xr.max(xl).min(w as f32);
    let x0floor = xl.floor(); let x0i = x0floor as usize; let x1ceil = xr.ceil(); let x1i = x1ceil as usize;
    if x1i <= x0i + 1 {
        let xmf = 0.5 * (xl + xr) - x0floor;
        row[x0i] += d - d * xmf; row[x0i + 1] += d * xmf;
    } else {
        let s = 1.0 / (xr - xl); let x0f = xl - x0floor;
        let a0 = 0.5 * s * (1.0 - x0f) * (1.0 - x0f);
        let x1f = xr - x1ceil + 1.0; let am = 0.5 * s * x1f * x1f;
        row[x0i] += d * a0;
        if x1i == x0i + 2 { row[x0i + 1] += d * (1.0 - a0 - am); }
        else { let a1 = s * (1.5 - x0f); row[x0i + 1] += d * (a1 - a0);
            for xi in x0i + 2..x1i - 1 { row[xi] += d * s; }
            let a2 = a1 + (x1i - x0i - 3) as f32 * s; row[x1i - 1] += d * (1.0 - a2 - am); }
        row[x1i] += d * am;
    }
}

fn shade(color: u8, lv: u8) -> u8 { let ch = |s: u8| ((((color >> s) & 3) as u16 * lv as u16 + 1) / 3) as u8; 0xC0 | ch(0) | (ch(2) << 2) | (ch(4) << 4) }

/// Rasterizes one glyph at `px` with its origin at (`pen_x`, `baseline`), quantised to four levels.
fn draw_glyph(fb: &mut [u8], face: &Face, gid: GlyphId, px: f32, pen_x: f32, baseline: i32, color: u8) {
    let scale = px / face.units_per_em() as f32;
    let frac = pen_x - pen_x.floor();
    let mut fl = Flattener { scale, ox: frac + 1.0, oy: 0.0, n: 0, last: (0.0, 0.0), start: (0.0, 0.0), min: (f32::MAX, f32::MAX), max: (f32::MIN, f32::MIN) };
    if face.outline_glyph(gid, &mut fl).is_none() || fl.n == 0 { return; }
    // shift so the top of the ink is at row 0
    let y_off = fl.min.1.floor(); let h = ((fl.max.1 - y_off).ceil() as usize).min(MAX_W); let w = ((fl.max.0).ceil() as usize + 1).min(MAX_W);
    let edges = unsafe { &EDGES[..fl.n] };
    let row = unsafe { &mut *core::ptr::addr_of_mut!(ROW) };
    for r in 0..h {
        row.iter_mut().for_each(|v| *v = 0.0);
        let y = r as f32 + y_off;
        for e in edges { if e.y0.min(e.y1) < y + 1.0 && e.y0.max(e.y1) > y { accumulate_row(row, e, y, w); } }
        let mut acc = 0.0f32;
        let fy = baseline + y_off as i32 + r as i32;
        for x in 0..w {
            acc += row[x];
            let c = acc.abs().min(1.0);
            let lv = if c >= 5.0 / 6.0 { 3 } else if c >= 0.5 { 2 } else if c >= 1.0 / 6.0 { 1 } else { 0 };
            let fx = pen_x.floor() as i32 - 1 + x as i32;
            if lv != 0 && fx >= 0 && fy >= 0 && fx < 240 && fy < 240 { fb[(fy * 240 + fx) as usize] = shade(color, lv); }
        }
    }
}

fn draw_text(fb: &mut [u8], data: &[u8], px: f32, s: &str, x: i32, baseline: i32, color: u8) -> f32 {
    let Ok(face) = Face::parse(data, 0) else { return 0.0 };
    let scale = px / face.units_per_em() as f32;
    let mut pen = x as f32;
    for c in s.chars() {
        let gid = face.glyph_index(c).or_else(|| face.glyph_index('?')).unwrap_or(GlyphId(0));
        draw_glyph(fb, &face, gid, px, pen, baseline, color);
        pen += face.glyph_hor_advance(gid).unwrap_or(0) as f32 * scale;
    }
    pen - x as f32
}

#[no_mangle] pub extern "C" fn _start() -> ! {
    let fb = unsafe { &mut *core::ptr::addr_of_mut!(FB) };
    let s = unsafe { core::ptr::read_volatile(&TEXT) };
    draw_text(fb, REGULAR, 18.0, s, 20, 40, 0xFF);
    draw_text(fb, SEMIBOLD, 20.0, s, 20, 80, 0xFF);
    draw_text(fb, SEMIBOLD, 54.0, s, 20, 160, 0xFF);
    unsafe { core::ptr::write_volatile(fb.as_mut_ptr(), 1) };
    loop {}
}
static TEXT: &str = "GYMWORLD12345678 José 12:34";
