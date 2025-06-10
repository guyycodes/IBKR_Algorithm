#!/usr/bin/env python3
"""
hkf_filter_5min.py
────────────────────────────────────────────────────────────────────────────
1‑second‑timestep Hybrid Kalman Filter optimised for **medium‑term (≈5 min)**
stock‑tick smoothing.

Public API
----------
run_hybrid_filter(df_sym, freq_feats, *, dt=1.0) → pd.DataFrame
hkf_cfg : global config dataclass
"""

from __future__ import annotations
from dataclasses import dataclass
from typing import Dict

import numpy as np
import pandas as pd
from filterpy.kalman import KalmanFilter

from hkf_features import bucket_expectation

__all__ = ["hkf_cfg", "run_hybrid_filter"]


# ─────────────────────── Configuration dataclass ───────────────────────────
@dataclass
class HKFConfig:
    time_domain_weight:      float = 0.65  # Slightly less time domain for 5min
    frequency_domain_weight: float = 0.35  # More frequency weight for medium-term
    adaptive_noise:          bool  = True  # scale R/Q on‑the‑fly
    preserve_breakouts:      bool  = True  # (placeholder for future logic)
    bucket_weight:           float = 0.5   # 0=no use, 1=full use (tune per timeframe)


hkf_cfg = HKFConfig()


# ───────────────────── Kalman initialisation helpers ───────────────────────
def _init_kf(dt: float) -> KalmanFilter:
    """Create a 4‑state KF (price, velocity, volume, spread) tuned for 5-minute horizon with control input."""
    kf = KalmanFilter(dim_x=4, dim_z=3, dim_u=1)  # extra control

    # Transition & observation matrices
    kf.F = np.array([[1, dt, 0, 0],
                     [0,  1, 0, 0],
                     [0,  0, 1, 0],
                     [0,  0, 0, 1]], dtype=float)
    kf.H = np.array([[1, 0, 0, 0],
                     [0, 0, 1, 0],
                     [0, 0, 0, 1]], dtype=float)
    kf.B = np.array([[1],       # control affects price directly
                     [0],       # no direct effect on velocity (it is derived)
                     [0],
                     [0]], dtype=float)

    # Baseline covariances (adjusted for 5-minute horizon)
    kf.P[:] = np.eye(4) * 1.5  # Slightly more initial uncertainty
    kf.R_base = np.diag([0.15, 12_000.0, 0.015])  # Higher measurement noise
    kf.Q_base = np.eye(4) * 2e-4  # Higher process noise for 5min dynamics
    kf.R = kf.R_base.copy()
    kf.Q = kf.Q_base.copy()
    return kf


def _update_noise(kf: KalmanFilter, mkt_vol: float) -> None:
    """Adaptive re‑scaling of R and Q (non‑cumulative) for 5min horizon."""
    if not hkf_cfg.adaptive_noise:
        return
    scale = float(np.clip(mkt_vol / 0.003, 0.4, 6.0))  # Wider scale range
    kf.R = kf.R_base * scale
    kf.Q = kf.Q_base * scale * 0.15


# ─────────────────────────── Main filter API ───────────────────────────────
def run_hybrid_filter(
    df_sym: pd.DataFrame, 
    freq_feats: Dict[str, float], 
    *, 
    dt: float = 1.0
) -> pd.DataFrame:
    """
    Apply the Hybrid KF to a single‑symbol DataFrame (5min optimized with bucket confidence).

    Parameters
    ----------
    df_sym : pd.DataFrame
        DataFrame with columns ['Last', 'Volume', 'Spread'] and optional bucket columns
        ['up_001_002', 'up_002_005', 'up_005_010', 'up_010_plus', 
         'dn_001_002', 'dn_002_005', 'dn_005_010', 'dn_010_plus']
    freq_feats : Dict[str, float]
        Frequency domain features from extract_frequency_features()
    dt : float, default=1.0
        Time step for Kalman filter

    Returns
    -------
    DataFrame indexed by Timestamp with columns:
        ['price_smoothed', 'price_velocity',
         'volume_denoised', 'spread_filtered']
    """
    assert not df_sym.empty, "df_sym must contain ≥1 row"

    kf = _init_kf(dt)
    out: list[dict[str, float]] = []

    # Columns are guaranteed to exist after preprocess_data
    bucket_cols = [c for c in df_sym.columns if c.startswith(("up_", "dn_"))]

    last_price = float(df_sym["Last"].iloc[0])

    for ts, row in df_sym.iterrows():
        price   = float(row["Last"])
        volume  = float(row["Volume"])
        spread  = float(row["Spread"])

        price_vel = price - last_price
        last_price = price

        _update_noise(kf, abs(price_vel) / price if price else 0.0)

        # --- 0. Convert bucket confidence → control and noise -----------
        u_t = 0.0  # default: no control input
        q_extra = 0.0  # default: no extra process noise
        
        conf = {c: float(row[c]) for c in bucket_cols if not pd.isna(row[c])}
        
        # Edge-case guard: normalize if probabilities don't sum to 1.0
        if conf and sum(conf.values()) > 1.0 + 1e-6:
            total = sum(conf.values())
            conf = {k: v / total for k, v in conf.items()}
        
        if conf and hkf_cfg.bucket_weight > 0:
            μ_ret, σ2_ret = bucket_expectation(conf)   # <- 8‑bucket dict for THIS BAR
            μ_price  = price * μ_ret                   # level move, not pct
            σ2_price = (price**2) * σ2_ret

            # Dial‑how‑much‑to‑trust: 0..1  (like a Kalman gain for the control channel)
            u_weight = hkf_cfg.bucket_weight           # new hyper‑param
            u_t      = u_weight * μ_price             # deterministic drift
            q_extra  = u_weight**2 * σ2_price         # stochastic part

        # Predict step with control --------------------------------------
        kf.Q[0,0] = kf.Q_base[0,0] + q_extra             # widen process variance
        kf.predict(u=np.asarray([u_t], dtype=float))     # pass control vector (force 1-D)

        # 2 Update
        kf.update(np.array([price, volume, spread]))

        # 3 Frequency‑domain nudging (enhanced for 5min with coherence)
        if hkf_cfg.frequency_domain_weight:
            # Base frequency components for 5min predictions
            trend_bias = freq_feats["trend_strength"] * price_vel
            short_term_bias = freq_feats.get("snr_by_band", {}).get("short_term", 0) * price_vel * 0.5
            
            # Coherence-enhanced signal validation
            pv_coherence = freq_feats.get("coherence_price_volume_peak", 0.0)
            ps_coherence = freq_feats.get("coherence_price_spread_peak", 0.0)
            
            # For 5min: Combine microstructure and short_term coherence
            pv_micro = freq_feats.get("coherence_price_volume_by_band", {}).get("microstructure", 0.0)
            pv_short = freq_feats.get("coherence_price_volume_by_band", {}).get("short_term", 0.0)
            
            # Signal quality: emphasize short-term coherence for 5min horizon
            quality_factor = (pv_coherence + pv_short * 1.2 + pv_micro * 0.8) / 3
            
            # Enhanced bias with coherence modulation
            base_bias = trend_bias + short_term_bias
            bias = base_bias * (0.6 + 0.8 * quality_factor)  # 0.6-1.4x multiplier
            
            if abs(bias) > 1e-10:  # Only if meaningful bias
                kf.x[0] += hkf_cfg.frequency_domain_weight * bias
                # Inflate covariance because we perturbed the state estimate directly
                kf.P *= 1.05

        out.append(
            {
                "Timestamp": ts,
                "price_smoothed": float(kf.x.flatten()[0]),
                "price_velocity": float(kf.x.flatten()[1]),
                "volume_denoised": float(kf.x.flatten()[2]),
                "spread_filtered": float(kf.x.flatten()[3]),
            }
        )

    return pd.DataFrame(out).set_index("Timestamp")
