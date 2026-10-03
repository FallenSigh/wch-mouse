//! Pure, dependency-free framing and parsing for the JFC103 BIO real-time
//! packet (88 bytes, header `0xFF`).
//!
//! This module deliberately has **no external dependencies** (only `std`) so it
//! can be compiled and unit-tested without the Tauri / serialport / webkit
//! toolchain. The serde facade lives in [`crate::bio`].
//!
//! # Packet layout (0-based offsets)
//!
//! ```text
//! [0]      header = 0xFF
//! [1..65]  acdata[64]  int8 waveform (-128..127)
//! [65]     heartrate
//! [66]     spo2
//! [67]     bk (microcirculation)
//! [68]     fatigue index
//! [69,70]  reserved
//! [71]     systolic BP
//! [72]     diastolic BP
//! [73]     cardiac output
//! [74]     peripheral resistance
//! [75]     RR interval
//! [76]     sdnn
//! [77]     rmssd
//! [78]     nn50
//! [79]     pnn50
//! [80..86] rra[6]
//! [86,87]  reserved
//! ```
//!
//! Framing mirrors `src/app/bio.c`: at packet start require `0xFF`, then take
//! exactly 88 bytes. A packet is valid when heartrate, spo2 or bk is nonzero
//! (an all-zero packet means no contact).

/// Packet header byte; marks the start of a frame and never appears inside one.
pub const HEADER: u8 = 0xFF;
/// A real-time packet is exactly this many bytes.
pub const PACKET_LEN: usize = 88;

/// One decoded JFC103 real-time packet.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct BioPacket {
    /// 64-sample heart-rate waveform, signed.
    pub acdata: Vec<i8>,
    pub heartrate: u8,
    pub spo2: u8,
    pub bk: u8,
    pub fatigue: u8,
    pub systolic: u8,
    pub diastolic: u8,
    pub cardiac_output: u8,
    pub peripheral_resistance: u8,
    pub rr: u8,
    pub sdnn: u8,
    pub rmssd: u8,
    pub nn50: u8,
    pub pnn50: u8,
    /// RR-interval series.
    pub rra: Vec<u8>,
    /// False for the all-zero "no contact" packet.
    pub valid: bool,
}

/// Decode one full 88-byte packet. Pure: no serial port, no allocation beyond
/// the two sample vectors.
pub fn parse_packet(bytes: &[u8; PACKET_LEN]) -> BioPacket {
    let acdata = bytes[1..65].iter().map(|b| *b as i8).collect();
    let rra = bytes[80..86].to_vec();
    let heartrate = bytes[65];
    let spo2 = bytes[66];
    let bk = bytes[67];

    BioPacket {
        acdata,
        heartrate,
        spo2,
        bk,
        fatigue: bytes[68],
        systolic: bytes[71],
        diastolic: bytes[72],
        cardiac_output: bytes[73],
        peripheral_resistance: bytes[74],
        rr: bytes[75],
        sdnn: bytes[76],
        rmssd: bytes[77],
        nn50: bytes[78],
        pnn50: bytes[79],
        rra,
        valid: heartrate != 0 || spo2 != 0 || bk != 0,
    }
}

/// Stateful byte-at-a-time framer. Feed bytes with [`Framer::push`]; it returns
/// `Some(packet)` once 88 bytes have been collected, then re-arms for the next
/// header. Non-header garbage before a frame is discarded (resync).
#[derive(Debug, Clone)]
pub struct Framer {
    buf: [u8; PACKET_LEN],
    len: usize,
}

impl Default for Framer {
    fn default() -> Self {
        Self::new()
    }
}

impl Framer {
    pub const fn new() -> Self {
        Framer {
            buf: [0u8; PACKET_LEN],
            len: 0,
        }
    }

    /// Feed one byte, returning a decoded packet when a full frame completes.
    pub fn push(&mut self, byte: u8) -> Option<BioPacket> {
        if self.len == 0 && byte != HEADER {
            return None;
        }

        self.buf[self.len] = byte;
        self.len += 1;

        if self.len == PACKET_LEN {
            self.len = 0;
            return Some(parse_packet(&self.buf));
        }

        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Build a packet with a known waveform and every metric distinct.
    fn crafted() -> [u8; PACKET_LEN] {
        let mut b = [0u8; PACKET_LEN];
        b[0] = HEADER;
        b[1] = 0x80; // acdata[0] = -128
        b[2] = 0x7F; // acdata[1] = 127
        b[64] = 0xFF; // acdata[63] = -1
        b[65] = 72; // heartrate
        b[66] = 98; // spo2
        b[67] = 34; // bk
        b[68] = 12; // fatigue
        b[71] = 120; // systolic
        b[72] = 80; // diastolic
        b[73] = 55; // cardiac output
        b[74] = 44; // peripheral resistance
        b[75] = 60; // rr
        b[76] = 40; // sdnn
        b[77] = 35; // rmssd
        b[78] = 20; // nn50
        b[79] = 15; // pnn50
        b[80..86].copy_from_slice(&[1, 2, 3, 4, 5, 6]);
        b
    }

    #[test]
    fn crafted_packet_parses_to_expected_fields() {
        let p = parse_packet(&crafted());

        assert_eq!(p.acdata.len(), 64);
        assert_eq!(p.acdata[0], -128);
        assert_eq!(p.acdata[1], 127);
        assert_eq!(p.acdata[63], -1);
        assert_eq!(p.heartrate, 72);
        assert_eq!(p.spo2, 98);
        assert_eq!(p.bk, 34);
        assert_eq!(p.fatigue, 12);
        assert_eq!(p.systolic, 120);
        assert_eq!(p.diastolic, 80);
        assert_eq!(p.cardiac_output, 55);
        assert_eq!(p.peripheral_resistance, 44);
        assert_eq!(p.rr, 60);
        assert_eq!(p.sdnn, 40);
        assert_eq!(p.rmssd, 35);
        assert_eq!(p.nn50, 20);
        assert_eq!(p.pnn50, 15);
        assert_eq!(p.rra, vec![1, 2, 3, 4, 5, 6]);
        assert!(p.valid);
    }

    #[test]
    fn framer_discards_leading_garbage_then_resyncs() {
        let packet = crafted();
        let mut framer = Framer::new();

        for &g in &[0x00u8, 0x12, 0x34, 0x7E] {
            assert!(framer.push(g).is_none());
        }

        let mut got = None;
        for &b in packet.iter() {
            got = framer.push(b);
        }
        assert_eq!(got, Some(parse_packet(&packet)));
    }

    #[test]
    fn framer_emits_consecutive_packets() {
        let packet = crafted();
        let mut framer = Framer::new();

        let mut first = None;
        for &b in packet.iter() {
            first = framer.push(b);
        }
        assert!(first.is_some());

        let mut second = None;
        for &b in packet.iter() {
            second = framer.push(b);
        }
        assert_eq!(second, Some(parse_packet(&packet)));
    }

    #[test]
    fn valid_flag_false_for_all_zero_no_contact() {
        let mut zero = [0u8; PACKET_LEN];
        zero[0] = HEADER;
        assert!(!parse_packet(&zero).valid);

        // Any one of heartrate / spo2 / bk marks contact.
        let mut hr = zero;
        hr[65] = 1;
        assert!(parse_packet(&hr).valid);

        let mut spo2 = zero;
        spo2[66] = 1;
        assert!(parse_packet(&spo2).valid);

        let mut bk = zero;
        bk[67] = 1;
        assert!(parse_packet(&bk).valid);
    }
}
