//! Battery state machine for the low-battery warning feature.
//!
//! Pure `core` logic, unit-tested on the host. Input: battery voltage in mV
//! (VBAT, measured via the onboard 200K/200K divider on GPIO4 / ADC1_CH3).
//! Output: which screen the panel should show. Hysteresis prevents flapping
//! around a threshold (each exit threshold sits above its entry threshold).

/// Enter Low below this (≈10% remaining on a Li-ion discharge curve).
pub const LOW_ENTER_MV: u16 = 3550;
/// Enter Critical below this (refresh still safe above brownout territory).
pub const CRIT_ENTER_MV: u16 = 3350;
/// Leave Critical, back to Low, above this.
pub const CRIT_EXIT_MV: u16 = 3450;
/// Leave Low, back to Normal, above this.
pub const LOW_EXIT_MV: u16 = 3750;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BattState {
    Normal,
    Low,
    Critical,
}

impl BattState {
    /// Screen kind this state renders (mirrors the three pre-rendered assets).
    pub fn screen(self) -> ScreenKind {
        match self {
            Self::Normal => ScreenKind::Normal,
            Self::Low => ScreenKind::Low,
            Self::Critical => ScreenKind::Charge,
        }
    }

    /// Decode a persisted screen-kind byte. `0xFF` (erased flash) and any
    /// unknown value decode as Normal — records written before this feature
    /// never warned, and the panel indeed showed the normal screen.
    pub fn from_kind_byte(b: u8) -> Self {
        match b {
            1 => Self::Low,
            2 => Self::Critical,
            _ => Self::Normal,
        }
    }

    pub fn kind_byte(self) -> u8 {
        match self {
            Self::Normal => 0,
            Self::Low => 1,
            Self::Critical => 2,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ScreenKind {
    Normal,
    Low,
    Charge,
}

/// Advance the state machine one wake cycle.
pub fn next_state(prev: BattState, vbat_mv: u16) -> BattState {
    match prev {
        BattState::Normal => {
            if vbat_mv < CRIT_ENTER_MV {
                BattState::Critical
            } else if vbat_mv < LOW_ENTER_MV {
                BattState::Low
            } else {
                BattState::Normal
            }
        }
        BattState::Low => {
            if vbat_mv < CRIT_ENTER_MV {
                BattState::Critical
            } else if vbat_mv >= LOW_EXIT_MV {
                BattState::Normal
            } else {
                BattState::Low
            }
        }
        BattState::Critical => {
            if vbat_mv >= LOW_EXIT_MV {
                BattState::Normal
            } else if vbat_mv >= CRIT_EXIT_MV {
                BattState::Low
            } else {
                BattState::Critical
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn normal_until_thresholds() {
        assert_eq!(next_state(BattState::Normal, 4200), BattState::Normal);
        assert_eq!(next_state(BattState::Normal, 3600), BattState::Normal);
        assert_eq!(next_state(BattState::Normal, 3549), BattState::Low);
        assert_eq!(next_state(BattState::Normal, 3349), BattState::Critical);
    }

    #[test]
    fn low_hysteresis() {
        assert_eq!(next_state(BattState::Low, 3550), BattState::Low);
        assert_eq!(next_state(BattState::Low, 3749), BattState::Low);
        assert_eq!(next_state(BattState::Low, 3750), BattState::Normal);
        // can still fall deeper
        assert_eq!(next_state(BattState::Low, 3300), BattState::Critical);
    }

    #[test]
    fn critical_hysteresis() {
        assert_eq!(next_state(BattState::Critical, 3350), BattState::Critical);
        assert_eq!(next_state(BattState::Critical, 3449), BattState::Critical);
        assert_eq!(next_state(BattState::Critical, 3450), BattState::Low);
        assert_eq!(next_state(BattState::Critical, 3750), BattState::Normal);
    }

    #[test]
    fn kind_byte_roundtrip() {
        for s in [BattState::Normal, BattState::Low, BattState::Critical] {
            assert_eq!(BattState::from_kind_byte(s.kind_byte()), s);
        }
    }

    #[test]
    fn legacy_and_garbage_kind_bytes_decode_normal() {
        assert_eq!(BattState::from_kind_byte(0xFF), BattState::Normal);
        assert_eq!(BattState::from_kind_byte(0), BattState::Normal);
        assert_eq!(BattState::from_kind_byte(99), BattState::Normal);
    }

    #[test]
    fn screens_map_to_states() {
        assert_eq!(BattState::Normal.screen(), ScreenKind::Normal);
        assert_eq!(BattState::Low.screen(), ScreenKind::Low);
        assert_eq!(BattState::Critical.screen(), ScreenKind::Charge);
    }
}
