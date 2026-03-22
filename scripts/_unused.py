

def compute_lufs_momentary_envelope(
    audio_waveform: npt.NDArray[np.float64],
    sample_rate_hz: int,
    window_ms: float = 400.0,
    hop_ms: float = 10.0
) -> Tuple[npt.NDArray[np.float64], float]:
    """
    Computes LUFS-M (momentary loudness) envelope.

    Parameters
    ----------
    audio_waveform
        Mono audio waveform.

    window_ms
        LUFS-M integration window (ITU standard = 400 ms).

    hop_ms
        Step between measurements.

    Returns
    -------
    lufs_values
        LUFS-M loudness values.

    hop_ms
        Time resolution of envelope.
    """
    meter = pyln.Meter(sample_rate_hz)
    window_samples = int(sample_rate_hz * window_ms / 1000)
    hop_samples = int(sample_rate_hz * hop_ms / 1000)
    lufs_values = []
    for start in range(0, len(audio_waveform) - window_samples, hop_samples):
        segment = audio_waveform[start:start + window_samples]
        try:
            loudness = meter.integrated_loudness(segment)
        except ValueError:
            loudness = -np.inf
        lufs_values.append(loudness)
    return np.array(lufs_values), hop_ms


def extract_decay_points_lufs_m(
    lufs_envelope: npt.NDArray[np.float64],
    hop_ms: float
) -> Tuple[List[float], float, float]:
    """
    Extracts decay time points using LUFS-M loudness envelope.

    Parameters
    ----------
    lufs_envelope
        Momentary loudness values in LUFS.

    hop_ms
        Time resolution of envelope.

    Returns
    -------
    decay_times_ms
        Times when signal crosses -6, -12 ... -60 dB relative to peak. 

    peak_time_ms
        Time of loudness peak.

    peak_lufs
        Peak LUFS value.
    """
    peak_idx = int(np.argmax(lufs_envelope))
    peak_lufs = float(lufs_envelope[peak_idx])
    decay_levels = np.array([-6,-12,-18,-24,-30,-36,-42,-48,-54,-60]) #FIXME: should be relative to peak LUFS, not dB. also lufs have different scale, so maybe use some fixed LUFS intervals
    targets = peak_lufs + decay_levels
    tail = lufs_envelope[peak_idx:]
    decay_times = []
    for target in targets:
        hits = np.where(tail <= target)[0]
        if len(hits) > 0:
            idx = hits[0]
            decay_times.append((idx + peak_idx) * hop_ms)
    peak_time_ms = peak_idx * hop_ms
    return decay_times, peak_time_ms, peak_lufs

def find_dominant_frequency(signal: npt.NDArray[np.float64], sample_rate: int) -> float:
    """Finds the frequency with the highest amplitude in the signal."""
    if len(signal) < 512:
        return 0.0
    window: npt.NDArray[np.float64] = np.hanning(len(signal))
    spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(signal * window))
    spectrum = uniform_filter1d(spectrum, size=3)
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(len(signal), 1.0 / sample_rate)
    return float(freqs[int(np.argmax(spectrum))])


def detect_resonance_frequency(
    audio_waveform: npt.NDArray[np.float64],
    sample_rate_hz: int,
    peak_sample_index: int
) -> Optional[float]:
    """
    Estimates resonant frequency of a drum sample by averaging multiple FFT windows
    after the transient.

    Strategy
    --------
    1. Skip transient region
    2. Take several overlapping windows
    3. Average magnitude spectra
    4. Smooth spectrum
    5. Detect dominant peak

    Returns
    -------
    resonance_frequency_hz
    """
    skip_samples: int = int(sample_rate_hz * 0.015)  # skip 15 ms transient
    start_index: int = peak_sample_index + skip_samples
    window_samples: int = int(sample_rate_hz * RESONANCE_WINDOW_MS / 1000)
    if start_index + window_samples >= len(audio_waveform):
        return None
    step: int = window_samples // 2
    num_windows: int = 4
    spectra: list[npt.NDArray[np.float64]] = []
    for i in range(num_windows):
        pos: int = start_index + i * step
        if pos + window_samples > len(audio_waveform):
            break
        segment: npt.NDArray[np.float64] = audio_waveform[pos:pos + window_samples]
        window: npt.NDArray[np.float64] = np.hanning(len(segment))
        spectrum: npt.NDArray[np.float64] = np.abs(np.fft.rfft(segment * window))
        spectra.append(spectrum)
    if not spectra:
        return None
    avg_spectrum: npt.NDArray[np.float64] = np.mean(spectra, axis=0)
    # spectral smoothing
    avg_spectrum = uniform_filter1d(avg_spectrum, size=5)
    freqs: npt.NDArray[np.float64] = np.fft.rfftfreq(window_samples, 1.0 / sample_rate_hz)
    mask: npt.NDArray[np.bool_] = ((freqs >= RESONANCE_MIN_HZ) & (freqs <= RESONANCE_MAX_HZ))
    if not np.any(mask):
        return None
    spectrum_band: npt.NDArray[np.float64] = avg_spectrum[mask]
    freq_band: npt.NDArray[np.float64] = freqs[mask]
    peak_index: int = int(np.argmax(spectrum_band))
    interp_index: float = refine_peak_index_via_quadratic_interpolation(spectrum_band, peak_index)
    freq_step: float = float(freq_band[1] - freq_band[0])
    resonance_frequency_hz: float = float(freq_band[0] + interp_index * freq_step)
    return resonance_frequency_hz


def detect_resonance_frequency_autocorrelation(
    audio_waveform: npt.NDArray[np.float64],
    sample_rate_hz: int,
    peak_sample_index: int
) -> Optional[float]:
    """
    Detects resonant frequency using autocorrelation.

    More stable for drums because it finds the fundamental
    even if harmonics are louder.
    """
    skip_samples = int(sample_rate_hz * 0.015)
    start = peak_sample_index + skip_samples
    window_samples = int(sample_rate_hz * RESONANCE_WINDOW_MS / 1000)
    if start + window_samples >= len(audio_waveform):
        return None
    segment = audio_waveform[start:start + window_samples]
    segment = segment * np.hanning(len(segment))
    autocorr = np.correlate(segment, segment, mode="full")
    autocorr = autocorr[len(autocorr)//2:]
    min_lag = int(sample_rate_hz / RESONANCE_MAX_HZ)
    max_lag = int(sample_rate_hz / RESONANCE_MIN_HZ)
    search_region = autocorr[min_lag:max_lag]
    if len(search_region) == 0:
        return None
    lag = np.argmax(search_region) + min_lag
    resonance_frequency_hz = sample_rate_hz / lag
    return float(resonance_frequency_hz)



def detect_resonance_frequency_old(signal: npt.NDArray[np.float64], sample_rate: int, start_index: int) -> Optional[float]:
    """Detects the resonant frequency of the signal starting from a given index."""
    start_index = start_index + int(sample_rate * 0.01) # Skip transient (10 ms)
    window_samples = int(sample_rate * RESONANCE_WINDOW_MS / 1000)
    segment = signal[start_index:start_index + window_samples]
    if len(segment) < 512:
        return None
    window = np.hanning(len(segment))
    spectrum = np.abs(np.fft.rfft(segment * window))
    freqs = np.fft.rfftfreq(len(segment), 1 / sample_rate)
    mask = (freqs >= RESONANCE_MIN_HZ) & (freqs <= RESONANCE_MAX_HZ)
    if not np.any(mask):
        return None
    spec = spectrum[mask]
    freq = freqs[mask]
    peak = int(np.argmax(spec))
    interp = refine_peak_index_via_quadratic_interpolation(spec, peak)
    freq_step = freq[1] - freq[0]
    return float(freq[0] + interp * freq_step)


def refine_peak_index_via_quadratic_interpolation(magnitude: np.ndarray, peak_index: int) -> float:
    """Refines peak frequency index using quadratic interpolation for better accuracy."""
    if peak_index <= 0 or peak_index >= len(magnitude) - 1:
        return float(peak_index)
    left_mag = magnitude[peak_index - 1]
    center_mag = magnitude[peak_index]
    right_mag = magnitude[peak_index + 1]
    denominator = left_mag - 2 * center_mag + right_mag
    if denominator == 0:
        return float(peak_index)
    shift = 0.5 * (left_mag - right_mag) / denominator
    return peak_index + shift




    # lufs_envelope, lufs_hop_ms = compute_lufs_momentary_envelope(audio_waveform, sample_rate)
    # decay_points_lufs, lufs_peak_rms_ms, lufs_peak_rms_db = extract_decay_points_lufs_m(lufs_envelope, lufs_hop_ms)
    #LUFS peak: {lufs_peak_rms_db:.1f}@{lufs_peak_rms_ms:.1f}ms,



    
    time_axis_sec, schroeder_db = schroeder_decay_curve(audio_waveform, sample_rate)
    schroeder_frame_ms = 1000.0 / sample_rate
    # T60 relative decay (true energy decay)
    amp_decay_ms = compute_decay_time_ms_from_envelope_frames(schroeder_db, schroeder_frame_ms, -SILENCE_THRESHOLD_DB)
    amp_decay_points_db, amp_peak_ms, amp_peak_db = extract_decay_points(schroeder_db, schroeder_frame_ms)







    amp_envelope_linear: npt.NDArray[np.float64]
    amp_envelope_linear = compute_amplitude_envelope_via_hilbert(audio_waveform)
    amp_envelope_linear = smooth_amplitude_envelope(amp_envelope_linear, sample_rate)
    # amp_envelope = smooth_amplitude_envelope(amp_envelope, sample_rate)
    # amp_envelope /= np.max(amp_envelope) + 1e-9
    amp_envelope_db: npt.NDArray[np.float64] = convert_linear_amplitude_to_db(amp_envelope_linear)

    amp_peak_index = int(np.argmax(amp_envelope_db))

    amp_frame_ms = 1000.0 / sample_rate

    amp_peak_db: float = float(np.max(amp_envelope_db))
    amp_peak_ms: float = amp_peak_index * amp_frame_ms
    amp_decay_ms: float = compute_decay_time_ms_from_envelope_frames(amp_envelope_db, amp_frame_ms, -SILENCE_THRESHOLD_DB)
    
    amp_decay_points_db, _, _ = extract_decay_points(amp_envelope_db, amp_frame_ms)

    


    
def compute_amplitude_envelope_via_hilbert(
    audio_waveform_float: npt.NDArray[np.float64]
) -> npt.NDArray[np.float64]:
    """
    Computes instantaneous amplitude envelope of a mono audio waveform.

    Input
    -----
    audio_waveform_float
        Mono PCM waveform.
        Range typically [-1.0, 1.0].

    Output
    ------
    amplitude_envelope_linear
        Instantaneous amplitude magnitude (linear scale).
        Same length as input waveform.
        Range: [0.0 .. ~1.0]
    """
    analytic_signal = hilbert(audio_waveform_float)
    amplitude_envelope_linear = np.abs(analytic_signal)
    return amplitude_envelope_linear