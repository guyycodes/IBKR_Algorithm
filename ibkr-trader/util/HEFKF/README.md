
<!--
 ═══════════════════════════════════ COMPREHENSIVE FEATURE USAGE CHART ═══════════════════════════════════
Feature	                                            Used in Simple Scoring?	      Used in Complex Regime?	        Used in Quality Factor?  Used with 1min filter?    Used with 5min filter?
coherence_price_volume_peak	                        ✅ Yes (weight: 0.4-0.6)	     ✅ Yes (breakout detection)	      ✅ Yes (core quality)    ✅ Yes (filter knobs)     ✅ Yes (filter knobs)
spectral_flux	                                    ✅ Yes (weight: 0.1)	         ✅ Yes (high-vol detection)	      ✅ Yes (freq quality)    ✅ Yes (volatility alert) ✅ Yes (volatility alert)
trend_strength_derivative	                        ✅ Yes (weight: 0.05-0.15)	 ✅ Yes (bull/bear detection)	  ✅ Yes (trend quality)   ✅ Yes (directional bias) ✅ Yes (directional bias)
centroid_velocity	                                ✅ Yes (via enhanced quality) ✅ Yes (reversal detection)	      ✅ Yes (freq quality)    ✅ Yes (freq instability) ✅ Yes (freq instability)
spectral_centroid (price)                           ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (freq quality)    ✅ Yes (filter knobs)     ✅ Yes (filter knobs)
spectral_centroid (volume)                          ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (freq quality)    ✅ Yes (volatility alert) ✅ Yes (volatility alert)
entropy (Only microstructure_goertzel band)         ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (entropy quality) ✅ Yes (volatility alert) ✅ Yes (volatility alert)
trend_strength                                      ✅ Yes (weight: varies)       ✅ Yes (regime classification)    ✅ Yes (trend quality)   ✅ Yes (regime detect)    ✅ Yes (regime detect)
coherence_price_spread_peak                         ✅ Yes (via enhanced quality) ✅ Yes (via enhanced quality)     ✅ Yes (core quality)    ✅ Yes (filter knobs)     ✅ Yes (filter knobs)

📊 Enhanced Quality Signal Breakdown (ALL FEATURES NOW USED):
Component	        Weight  Features Used	                                                                        Purpose
Core Quality	        40%	    (coher_pv×0.7 + coher_ps×0.3) × (1-entropy_short)	                                    Dual-coherence signal validation
Trend Quality	    25%	    trend_strength×0.7 + d_trend×0.3	                                                    Directional stability
Freq Quality	        20%	    (1-flux)×0.4 + centroid_price×0.25 + centroid_volume×0.15 + (1-centroid_velocity)×0.2	Dual-centroid spectral stability
Entropy Quality	    15%	    All 4 entropy bands (micro, short, medium, trend)	                                    Multi-timeframe noise

⚡ Volatility Alert Breakdown (ENHANCED WITH NEW FEATURES):
Component	        Weight	Features Used	                                Purpose
Flux Alert	        50%	    spectral_flux	                                Primary volatility indicator
Freq Instability	    30%	    centroid_velocity×0.6 + entropy_trend×0.4	    Frequency domain chaos
Entropy Chaos	    20%	    entropy_micro + entropy_short + entropy_medium	Multi-timeframe noise

🔧 Filter Knob Adjustments (NEW INTEGRATION):
Parameter	        Formula	                                            Features Used                            Purpose
bucket_weight	    base + 0.30×quality - 0.20×volatility	            Enhanced quality + volatility alert      Dynamic u-channel gain
freq_domain_weight	base + 0.15×quality	                                Enhanced quality signal                  Spectral nudging strength
lambda_fixed	        base - 0.02×volatility (if vol>0.6)	                Volatility alert signal                  Adaptive forgetting rate

🎯 SUMMARY: Feature Coverage
✅ FULLY UTILIZED: coherence_pv_peak, coherence_ps_peak, spectral_flux, trend_strength_derivative, 
                  centroid_velocity, spectral_centroid_price, spectral_centroid_volume, entropy_bands, trend_strength
📈 INTEGRATION STATUS: ALL 9 major spectral features now actively drive filter parameters and scoring 
-->