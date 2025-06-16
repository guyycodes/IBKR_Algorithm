#!/usr/bin/env python3
"""
kalman_features.py
────────────────────────────────────────────────────────────────────────────
Frequency‑domain helpers for the Hybrid‑Kalman pipeline **with coherence**.

Public API
----------
extract_frequency_features(df_sym, sampling_rate_hz) → dict
bucket_expectation(conf) → tuple[float, float]
Exports (via __all__):
    • frequency_bands
    • extract_frequency_features
    • bucket_expectation
"""

from __future__ import annotations
from typing import Dict, Tuple

import numpy as np
import pandas as pd
from scipy import signal

__all__ = ["frequency_bands", "extract_frequency_features", "bucket_expectation"]

# ─────────────────────────── Band dictionary (Hz) ──────────────────────────
frequency_bands: Dict[str, Tuple[float, float]] = {
    #          lower‑Hz                upper‑Hz           comment
    "microstructure": (1 / (5 * 60),     1 / 60),        # 1–5 min cycles
    "short_term":     (1 / (15 * 60),    1 / (5 * 60)),  # 5–15 min
    "medium_term":    (1 / (30 * 60),    1 / (15 * 60)), # 15–30 min
    "trend":          (1 / (120 * 60),   1 / (30 * 60)), # 30–120 min
}

# ────────────────────── Bucket-to-Gaussian mapping ─────────────────────────
def bucket_expectation(conf: Dict[str, float]) -> Tuple[float, float]:
    """
    Convert bucket probabilities → expected return (µ) and variance (σ²).
    
    Parameters
    ----------
    conf : Dict[str, float]
        Confidence by bin, e.g. {"up_001_002": 0.33, "dn_001_002": 0.13, ...}
        
    Returns
    -------
    Tuple[float, float]
        (μ, σ²) where μ is expected fractional return and σ² is variance
        
    Example
    -------
    >>> confidence = {
    ...     "up_001_002": 0.33, "up_002_005": 0.27, "up_005_010": 0.05, "up_010_plus": 0.02,
    ...     "dn_001_002": 0.13, "dn_002_005": 0.13, "dn_005_010": 0.05, "dn_010_plus": 0.02
    ... }
    >>> μ, σ² = bucket_expectation(confidence)
    """
    # Bucket specifications: (mean_return, variance)
    specs = {
        "up_001_002": (+0.0015, 0.0003**2),  # +0.15%, σ ≈ 3bp
        "up_002_005": (+0.0035, 0.0009**2),  # +0.35%, σ ≈ 9bp  
        "up_005_010": (+0.0075, 0.0012**2),  # +0.75%, σ ≈ 12bp
        "up_010_plus":(+0.0150, 0.0020**2),  # +1.50%, σ ≈ 20bp
        "dn_001_002": (-0.0015, 0.0003**2),  # mirror image
        "dn_002_005": (-0.0035, 0.0009**2),
        "dn_005_010": (-0.0075, 0.0012**2),
        "dn_010_plus":(-0.0150, 0.0020**2),
    }
    
    μ = σ2 = 0.0
    for bucket, prob in conf.items():
        if bucket in specs:
            mean_ret, variance = specs[bucket]
            μ  += prob * mean_ret
            σ2 += prob * (variance + mean_ret**2)  # law of total variance
    
    σ2 -= μ**2  # subtract off E[X]² to get pure variance
    σ2 = max(σ2, 1e-10)  # numerical safeguard
    
    return μ, σ2

# ─────────────────────────── Low‑level helpers ─────────────────────────────
def _welch_psd(x: np.ndarray, fs: float) -> tuple[np.ndarray, np.ndarray]:
    """Welch PSD with a short‑segment safeguard."""
    f, Pxx = signal.welch(x, fs=fs, nperseg=min(256, len(x)))
    return f, Pxx


def _mscohere(
    x: np.ndarray, y: np.ndarray, fs: float
) -> tuple[np.ndarray, np.ndarray]:
    """Magnitude‑squared coherence |Cxy|² via Welch segments with error handling."""
    if len(x) < 10 or len(y) < 10:
        # Return dummy arrays for insufficient data
        return np.array([0.0]), np.array([0.0])
    
    try:
        f, Cxy = signal.coherence(x, y, fs=fs, nperseg=min(256, len(x), len(y)))
        return f, Cxy
    except (ValueError, RuntimeError):
        # Fallback for numerical issues
        return np.array([0.0]), np.array([0.0])


def _band_power(f: np.ndarray, Pxx: np.ndarray, band: tuple[float, float]) -> float:
    """Power ∫ PSD df over a [f0, f1) band."""
    idx = (f >= band[0]) & (f < band[1])
    return float(np.trapz(Pxx[idx], f[idx])) if np.any(idx) else 0.0


def _band_mean(
    f: np.ndarray, arr: np.ndarray, band: tuple[float, float]
) -> float:
    """Mean of *arr* over the frequency band."""
    idx = (f >= band[0]) & (f < band[1])
    return float(arr[idx].mean()) if np.any(idx) else 0.0


# ────────────────────────── Public API function ────────────────────────────
def extract_frequency_features(
    df_sym: pd.DataFrame, sampling_rate_hz: float
) -> dict[str, float | dict[str, float]]:
    """
    Extract PSD‑ *and coherence*‑based indicators for a single symbol.

    Parameters
    ----------
    df_sym : DataFrame with columns 'Last', 'Volume', 'Spread'.
    sampling_rate_hz : float
        Sample rate of the time series after resampling / gap‑fill.

    Returns
    -------
    Dict containing:
        • price_dominant_period  (minutes)
        • price_cycle_strength
        • volume_burst_frequency (per minute)
        • spread_volatility_cycle (minutes)
        • trend_strength
        • snr_by_band {band: value}
        
        **NEW COHERENCE FEATURES:**
        • coherence_price_volume_peak
        • coherence_price_volume_by_band {band: value}
        • coherence_price_spread_peak
        • coherence_price_spread_by_band {band: value}
    """
    # ── Raw vectors ──
    price  = df_sym["Last"].to_numpy()
    volume = df_sym["Volume"].to_numpy()
    spread = df_sym["Spread"].to_numpy()

    # ───────── PSD FEATURES ─────────
    f_price, P_price = _welch_psd(price, sampling_rate_hz)
    dom_idx = int(np.argmax(P_price))
    dom_freq = f_price[dom_idx] or np.finfo(float).eps

    features: dict[str, float | dict] = {
        "price_dominant_period": 1 / dom_freq / 60,  # → minutes
        "price_cycle_strength": (
            P_price[dom_idx] / P_price.sum() if P_price.sum() else 0.0
        ),
    }

    # Volume PSD
    f_vol, P_vol = _welch_psd(volume, sampling_rate_hz)
    features["volume_burst_frequency"] = f_vol[int(np.argmax(P_vol))] * 60  # /min

    # Spread PSD
    f_sp, P_sp = _welch_psd(spread, sampling_rate_hz)
    sp_idx = int(np.argmax(P_sp))
    sp_dom_freq = f_sp[sp_idx] or np.finfo(float).eps
    features["spread_volatility_cycle"] = 1 / sp_dom_freq / 60

    # Trend strength (low‑freq power ratio)
    trend_power = _band_power(f_price, P_price, frequency_bands["trend"])
    features["trend_strength"] = (
        trend_power / P_price.sum() if P_price.sum() else 0.0
    )

    # SNR per band
    total_power = P_price.sum()
    snr = {}
    for name, band in frequency_bands.items():
        sig_power = _band_power(f_price, P_price, band)
        noise_power = total_power - sig_power
        snr[name] = sig_power / noise_power if noise_power else 0.0
    features["snr_by_band"] = snr

    # ───────── COHERENCE FEATURES ─────────
    # Price‑Volume coherence
    f_pv, C_pv = _mscohere(price, volume, sampling_rate_hz)
    if len(C_pv) > 0:
        pv_peak_idx = int(np.argmax(C_pv))
        features["coherence_price_volume_peak"] = float(C_pv[pv_peak_idx])
        
        pv_band_means = {
            band_name: _band_mean(f_pv, C_pv, band)
            for band_name, band in frequency_bands.items()
        }
        features["coherence_price_volume_by_band"] = pv_band_means
    else:
        # Fallback for insufficient data
        features["coherence_price_volume_peak"] = 0.0
        features["coherence_price_volume_by_band"] = {
            band_name: 0.0 for band_name in frequency_bands.keys()
        }

    # Price‑Spread coherence
    f_ps, C_ps = _mscohere(price, spread, sampling_rate_hz)
    if len(C_ps) > 0:
        ps_peak_idx = int(np.argmax(C_ps))
        features["coherence_price_spread_peak"] = float(C_ps[ps_peak_idx])
        
        ps_band_means = {
            band_name: _band_mean(f_ps, C_ps, band)
            for band_name, band in frequency_bands.items()
        }
        features["coherence_price_spread_by_band"] = ps_band_means
    else:
        # Fallback for insufficient data
        features["coherence_price_spread_peak"] = 0.0
        features["coherence_price_spread_by_band"] = {
            band_name: 0.0 for band_name in frequency_bands.keys()
        }

    return features
