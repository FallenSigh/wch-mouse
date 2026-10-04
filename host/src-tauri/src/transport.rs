//! Thin wrapper over a hidapi handle: feature-report send/read and the
//! send-then-poll transaction loop.

use std::time::{Duration, Instant};

use hidapi::{HidApi, HidDevice};

use crate::error::{Error, Result};
use crate::protocol::{self, Reply, FRAME_LEN, POLL_INTERVAL_MS, REPORT_ID};

/// One open HID Feature-report transport.
pub struct Transport {
    device: HidDevice,
}

impl Transport {
    /// Open a specific HID path (as reported by [`crate::device::list_devices`]).
    pub fn open_path(api: &HidApi, path: &std::ffi::CStr) -> Result<Self> {
        let device = api.open_path(path)?;
        Ok(Transport { device })
    }

    /// Send one request frame and wait for the reply that echoes it.
    ///
    /// HID gives the host no payload on a GET_FEATURE, so the device keeps the
    /// last response. Over the dongle the answer arrives on the RF link's own
    /// schedule, so anything not matching `cmd | 0x80` and `seq` is ignored and
    /// the read is retried until `timeout` elapses.
    pub fn transact(&self, cmd: u8, seq: u8, payload: &[u8], timeout: Duration) -> Result<Reply> {
        let frame = protocol::build_frame(cmd, seq, payload)?;
        self.send_frame(&frame)?;

        let deadline = Instant::now() + timeout;
        let mut last: Option<(u8, u8)> = None;

        loop {
            let raw = self.read_frame()?;
            if let Ok(reply) = protocol::parse_reply(&raw) {
                last = Some((reply.cmd, reply.seq));
                if reply.matches(cmd, seq) {
                    return Ok(reply);
                }
            }

            if Instant::now() >= deadline {
                let (last_cmd, last_seq) = match last {
                    Some((c, s)) => (Some(c), Some(s)),
                    None => (None, None),
                };
                return Err(Error::Timeout {
                    cmd,
                    seq,
                    last_cmd,
                    last_seq,
                });
            }

            std::thread::sleep(Duration::from_millis(POLL_INTERVAL_MS));
        }
    }

    /// `SET_FEATURE`: one Report-ID byte followed by the 63-byte frame.
    fn send_frame(&self, frame: &[u8; FRAME_LEN]) -> Result<()> {
        let mut out = Vec::with_capacity(1 + FRAME_LEN);
        out.push(REPORT_ID);
        out.extend_from_slice(frame);
        self.device.send_feature_report(&out)?;
        Ok(())
    }

    /// `GET_FEATURE`: read one report with the Report-ID prefix and strip it.
    fn read_frame(&self) -> Result<Vec<u8>> {
        let mut buf = [0u8; 1 + FRAME_LEN];
        // hidapi fetches the report named by buf[0]; it must be our vendor
        // Feature report, not report 0.
        buf[0] = REPORT_ID;
        let n = self.device.get_feature_report(&mut buf)?;
        let read = &buf[..n.min(buf.len())];
        Ok(protocol::strip_report_id(read)
            .map(|s| s.to_vec())
            .unwrap_or_default())
    }
}
