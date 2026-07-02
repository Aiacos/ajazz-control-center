//! Per-device-family parameters for the Stream Dock families mirajazz can
//! drive: AKP05/N4, AKP03/N3, AKP153/HSV293S. Values follow the authoritative
//! mirajazz consumers (opendeck-akp05 / -akp03 / -akp153).
//!
//! PROVISIONAL for AKP03 / AKP153: no hardware is available to verify here
//! (only the AKP05E demo unit). AKP05/N4 (pv3, 112x112 Rot180) is hardware-
//! confirmed. The (vid,pid) -> params mapping and format selection are covered
//! by unit tests; live device behaviour for the no-hardware families is not.

use mirajazz::types::{ImageFormat, ImageMirroring, ImageMode, ImageRotation};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Family {
    /// AKP05 / Mirabox N4 — 10 keys + 4 encoders + 4 touch-strip zones.
    Akp05,
    /// AKP03 / Mirabox N3 — small grid + encoders.
    Akp03,
    /// AKP153 / Mirabox HSV293S — key grid, no encoders.
    Akp153,
}

/// Connection + geometry parameters for one device family member.
#[derive(Clone, Copy, Debug)]
pub struct DeviceParams {
    pub family: Family,
    pub protocol_version: usize,
    pub key_count: usize,
    pub encoder_count: usize,
    pub human_name: &'static str,
}

/// Resolve (vid, pid) to its family parameters, or None if unknown.
///
/// Mirrors the app's `register.cpp` Stream Dock matrix EXACTLY — same SKUs,
/// same geometry (key/encoder counts) — so the sidecar is behaviourally at
/// parity with the C++ backends it replaces (no geometry drift on the AKP03 /
/// AKP153 SKUs that have no hardware here). Note the `0x0300:0x1001`
/// collision: in `register.cpp` AKP153 is registered first and wins it, so the
/// shadowed AKP03 `0x1001` entry is intentionally omitted (dead in the app).
pub fn params_for(vid: u16, pid: u16) -> Option<DeviceParams> {
    let p = |family, protocol_version, key_count, encoder_count, human_name| {
        Some(DeviceParams { family, protocol_version, key_count, encoder_count, human_name })
    };
    match (vid, pid) {
        // --- AKP05 / N4 (pv3, 10 keys + 4 enc; 15 mirajazz surfaces) ---
        // hardware-confirmed on 0x0300:0x3004.
        (0x0300, 0x3004) => p(Family::Akp05, 3, 15, 4, "Ajazz AKP05E"),
        (0x0300, 0x5001) => p(Family::Akp05, 3, 15, 4, "Ajazz AKP05 (provisional)"),
        (0x6603, 0x1007) => p(Family::Akp05, 3, 15, 4, "Mirabox N4"),
        // --- AKP03 / N3 (pv2, 9 buttons + 3 enc) — PROVISIONAL, no hardware ---
        (0x0300, 0x3001) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03 (legacy)"),
        (0x0300, 0x3002) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03E"),
        (0x0300, 0x1003) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03R"),
        (0x0300, 0x3003) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03R (rev.2)"),
        (0x6602, 0x1002) => p(Family::Akp03, 2, 9, 3, "Mirabox N3"),
        (0x6602, 0x1003) => p(Family::Akp03, 2, 9, 3, "Mirabox N3E"),
        (0x6603, 0x1002) => p(Family::Akp03, 2, 9, 3, "Mirabox N3 (rev.3)"),
        (0x6603, 0x1003) => p(Family::Akp03, 2, 9, 3, "Mirabox N3EN"),
        // --- AKP153 / HSV293S (pv1, 15 keys, no enc) — PROVISIONAL, no hardware ---
        (0x0300, 0x1001) => p(Family::Akp153, 1, 15, 0, "Ajazz AKP153"),
        (0x0300, 0x1002) => p(Family::Akp153, 1, 15, 0, "Ajazz AKP153E"),
        (0x5548, 0x6674) => p(Family::Akp153, 1, 15, 0, "Ajazz AKP153 (Mirabox V1)"),
        (0x0300, 0x1010) => p(Family::Akp153, 1, 15, 0, "Ajazz AKP153E (V2)"),
        (0x0300, 0x1020) => p(Family::Akp153, 1, 15, 0, "Ajazz AKP153R"),
        _ => None,
    }
}

/// Image format for a regular key. Constant per family for the SKUs the app
/// registers (no pv3 AKP153 / per-key variants in our matrix yet).
pub fn key_image_format(family: Family) -> ImageFormat {
    match family {
        Family::Akp05 => ImageFormat {
            mode: ImageMode::JPEG,
            size: (112, 112),
            rotation: ImageRotation::Rot180,
            mirror: ImageMirroring::None,
        },
        Family::Akp03 => ImageFormat {
            mode: ImageMode::JPEG,
            size: (60, 60),
            rotation: ImageRotation::Rot90,
            mirror: ImageMirroring::None,
        },
        // Every AKP153 SKU the app registers is pv1 (85x85 Rot90, mirror both).
        Family::Akp153 => ImageFormat {
            mode: ImageMode::JPEG,
            size: (85, 85),
            rotation: ImageRotation::Rot90,
            mirror: ImageMirroring::Both,
        },
    }
}

/// Image format for an AKP05 encoder touch zone (other families have no zones).
pub fn zone_image_format() -> ImageFormat {
    ImageFormat {
        mode: ImageMode::JPEG,
        size: (128, 128),
        rotation: ImageRotation::Rot180,
        mirror: ImageMirroring::None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn akp05e_is_pv3_with_4_encoders() {
        let p = params_for(0x0300, 0x3004).expect("AKP05E known");
        assert_eq!(p.family, Family::Akp05);
        assert_eq!(p.protocol_version, 3);
        assert_eq!(p.encoder_count, 4);
    }

    #[test]
    fn n4_shares_akp05_family() {
        assert_eq!(params_for(0x6603, 0x1007).unwrap().family, Family::Akp05);
    }

    #[test]
    fn akp03_is_pv2_grid_with_encoders() {
        let p = params_for(0x0300, 0x3002).expect("AKP03E known");
        assert_eq!(p.family, Family::Akp03);
        assert_eq!(p.protocol_version, 2);
        assert_eq!(p.encoder_count, 3);
    }

    #[test]
    fn vidpid_0300_1001_resolves_to_akp153_not_akp03() {
        // register.cpp registers AKP153 first, so it wins this collision.
        assert_eq!(params_for(0x0300, 0x1001).unwrap().family, Family::Akp153);
    }

    #[test]
    fn akp153_is_pv1_no_encoders_15_keys() {
        let p = params_for(0x5548, 0x6674).expect("AKP153 known");
        assert_eq!(p.family, Family::Akp153);
        assert_eq!(p.protocol_version, 1);
        assert_eq!(p.encoder_count, 0);
        assert_eq!(p.key_count, 15); // parity with our descriptor, not opendeck's 18
    }

    #[test]
    fn unknown_vid_pid_is_none() {
        assert!(params_for(0xDEAD, 0xBEEF).is_none());
    }

    #[test]
    fn akp05_key_format_is_112_rot180() {
        let f = key_image_format(Family::Akp05);
        assert_eq!(f.size, (112, 112));
        assert!(matches!(f.rotation, ImageRotation::Rot180));
    }

    #[test]
    fn akp153_is_85_rot90_mirror_both() {
        let f = key_image_format(Family::Akp153);
        assert_eq!(f.size, (85, 85));
        assert!(matches!(f.rotation, ImageRotation::Rot90));
        assert!(matches!(f.mirror, ImageMirroring::Both));
    }

    #[test]
    fn akp03_key_format_is_60_rot90() {
        let f = key_image_format(Family::Akp03);
        assert_eq!(f.size, (60, 60));
        assert!(matches!(f.rotation, ImageRotation::Rot90));
    }
}
