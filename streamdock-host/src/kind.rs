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

/// Resolve (vid, pid) to its family parameters, or None if unknown to mirajazz.
/// Covers the SKUs registered by the app's `register.cpp` Stream Dock matrix.
pub fn params_for(vid: u16, pid: u16) -> Option<DeviceParams> {
    let p = |family, protocol_version, key_count, encoder_count, human_name| {
        Some(DeviceParams { family, protocol_version, key_count, encoder_count, human_name })
    };
    match (vid, pid) {
        // --- AKP05 / N4 (pv3) — hardware-confirmed on 0x0300:0x3004 ---
        (0x0300, 0x3004) => p(Family::Akp05, 3, 15, 4, "Ajazz AKP05E"),
        (0x0300, 0x5001) => p(Family::Akp05, 3, 15, 4, "Ajazz AKP05"),
        (0x6603, 0x1007) => p(Family::Akp05, 3, 15, 4, "Mirabox N4"),
        // --- AKP03 / N3 (pv2 base, pv3 rev.2) — PROVISIONAL ---
        (0x0300, 0x1001) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03"),
        (0x0300, 0x1002) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03E"),
        (0x0300, 0x1003) => p(Family::Akp03, 2, 9, 3, "Ajazz AKP03R"),
        (0x0300, 0x3002) => p(Family::Akp03, 3, 9, 3, "Ajazz AKP03E (rev.2)"),
        (0x0300, 0x3003) => p(Family::Akp03, 3, 9, 3, "Ajazz AKP03R (rev.2)"),
        (0x6602, 0x1002) => p(Family::Akp03, 2, 9, 3, "Mirabox N3"),
        (0x6603, 0x1002) => p(Family::Akp03, 2, 9, 3, "Mirabox N3"),
        (0x6603, 0x1003) => p(Family::Akp03, 3, 9, 3, "Mirabox N3EN"),
        // --- AKP153 / HSV293S (pv1 base, pv3 v3/rev.2) — PROVISIONAL ---
        (0x5548, 0x6674) => p(Family::Akp153, 1, 18, 0, "Ajazz AKP153"),
        (0x5548, 0x6670) => p(Family::Akp153, 1, 18, 0, "Mirabox HSV293S"),
        (0x0300, 0x1010) => p(Family::Akp153, 1, 18, 0, "Ajazz AKP153E"),
        (0x0300, 0x1020) => p(Family::Akp153, 1, 18, 0, "Ajazz AKP153R"),
        (0x0300, 0x3010) => p(Family::Akp153, 3, 18, 0, "Ajazz AKP153E (rev.2)"),
        (0x0300, 0x3011) => p(Family::Akp153, 3, 18, 0, "Ajazz AKP153R (rev.2)"),
        (0x6603, 0x1014) => p(Family::Akp153, 3, 18, 0, "Mirabox HSV293S V3"),
        _ => None,
    }
}

/// Image format for a regular key, given family + protocol version + key index.
pub fn key_image_format(family: Family, protocol_version: usize, key: u8) -> ImageFormat {
    match family {
        Family::Akp05 => ImageFormat {
            mode: ImageMode::JPEG,
            size: (112, 112),
            rotation: ImageRotation::Rot180,
            mirror: ImageMirroring::None,
        },
        Family::Akp03 => ImageFormat {
            mode: ImageMode::JPEG,
            size: (64, 64),
            rotation: ImageRotation::Rot90,
            mirror: ImageMirroring::None,
        },
        Family::Akp153 => {
            if protocol_version == 1 {
                ImageFormat {
                    mode: ImageMode::JPEG,
                    size: (85, 85),
                    rotation: ImageRotation::Rot90,
                    mirror: ImageMirroring::Both,
                }
            } else {
                // pv3: edge column keys are narrower (opendeck-akp153).
                let size = match key {
                    5 | 11 | 17 => (82, 82),
                    _ => (95, 95),
                };
                ImageFormat {
                    mode: ImageMode::JPEG,
                    size,
                    rotation: ImageRotation::Rot90,
                    mirror: ImageMirroring::Both,
                }
            }
        }
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
        let p = params_for(0x0300, 0x1001).expect("AKP03 known");
        assert_eq!(p.family, Family::Akp03);
        assert_eq!(p.protocol_version, 2);
        assert_eq!(p.encoder_count, 3);
    }

    #[test]
    fn akp153_is_pv1_no_encoders() {
        let p = params_for(0x5548, 0x6674).expect("AKP153 known");
        assert_eq!(p.family, Family::Akp153);
        assert_eq!(p.protocol_version, 1);
        assert_eq!(p.encoder_count, 0);
    }

    #[test]
    fn unknown_vid_pid_is_none() {
        assert!(params_for(0xDEAD, 0xBEEF).is_none());
    }

    #[test]
    fn akp05_key_format_is_112_rot180() {
        let f = key_image_format(Family::Akp05, 3, 0);
        assert_eq!(f.size, (112, 112));
        assert!(matches!(f.rotation, ImageRotation::Rot180));
    }

    #[test]
    fn akp153_pv1_is_85_rot90_mirror_both() {
        let f = key_image_format(Family::Akp153, 1, 0);
        assert_eq!(f.size, (85, 85));
        assert!(matches!(f.rotation, ImageRotation::Rot90));
        assert!(matches!(f.mirror, ImageMirroring::Both));
    }

    #[test]
    fn akp153_pv3_edge_keys_are_narrower() {
        assert_eq!(key_image_format(Family::Akp153, 3, 5).size, (82, 82));
        assert_eq!(key_image_format(Family::Akp153, 3, 0).size, (95, 95));
    }
}
