#!/usr/bin/env python3
"""Drumgizmo -> Karatala converter (initial implementation)

Defaults to dry-run: prints platform-specific commands without executing.

Usage: python converter.py todo/ --output-dir instruments
"""
import argparse
import os
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import defaultdict
import re


CHANNEL_TO_MIC = {
    'AmbLeft': 'rfs', 'AmbRight': 'rfs',
    'OHLeft': 'ons', 'OHRight': 'ons', 'OHCenter': 'rcm',
}

# Additional keyword-based mapping (case-insensitive substring -> mic code)
CHANNEL_KEYWORD_MAP = {
    'amb': 'rfs',
    'room': 'rfs',
    'ambleft': 'rfs', 'ambright': 'rfs',
    'ohleft': 'ons', 'ohright': 'ons', 'ohcenter': 'rcm', 'oh': 'ons',
    'snare_top': 'dtm', 'snaretop': 'dtm', 'snare_bottom': 'dbm', 'snarebottom': 'dbm',
    'kdruminside': 'dim', 'kdrumoutside': 'dom', 'kick': 'dim',
    'hihat': 'dtm', 'hh': 'dtm',
    'ride': 'dtm', 
    'tom': 'dtm', 'ftom': 'dtm',
    # 'crash': 'dtm', 'china': 'dtm', 'splash': 'dtm' only hat and ride of cymbals might have direct mic
}

# Hi-hat variant mapping: variant_key -> (expression_name, expression_code, default_velocity)
HIHAT_VARIANTS = {
    'closed': ('bow[close]', 'pdl_opn_cls', 78),
    'closednopedal': ('edge[half]', 'pdl_opn_cls', 64),
    'semiopen': ('edge[half]', 'pdl_opn_cls', 52),
    'open': ('edge[open]', 'pdl_opn_cls', 43),
    'pedal': ('pedal', 'pdl_opn_cls', 85),
    'pedalhit': ('pedal[splash]', 'pdl_opn_cls', 85)
}


def find_kits(root: Path):
    kits = []
    for child in sorted(root.iterdir()):
        if child.is_dir():
            kits.append(child)
    return kits


def find_all_instruments(kit_dir: Path):
    """Scan kit directory for all instrument XML files (those with <samples>).
    
    Returns list of dicts with 'name' and 'path' keys.
    This discovers ALL instruments, not just those declared in kit XML.
    Handles multiple directory structures (flat or nested in subdirectories).
    """
    instruments = []
    seen_paths = set()  # Track already-found instrument XMLs to avoid duplicates
    
    def scan_dir(search_dir: Path, depth: int = 0):
        """Recursively scan for instrument XMLs, limiting depth to avoid excessive recursion."""
        if depth > 2:  # Limit depth to avoid deep traversal
            return
        try:
            for item in sorted(search_dir.iterdir()):
                if not item.is_dir():
                    continue
                # First, try to find an XML file with <samples> element in this folder
                # Try {foldername}.xml first (CrocellKit style)
                inst_xml = item / f"{item.name}.xml"
                if inst_xml.exists() and inst_xml not in seen_paths:
                    try:
                        tree = ET.parse(inst_xml)
                        root = tree.getroot()
                        if root.find('samples') is not None:
                            # Use XML's name attribute if available, otherwise use folder name
                            inst_name = root.attrib.get('name', item.name)
                            instruments.append({'name': inst_name, 'path': inst_xml})
                            seen_paths.add(inst_xml)
                            continue
                    except Exception:
                        pass
                
                # If {foldername}.xml doesn't exist or doesn't have <samples>,
                # look for any XML file with <samples> in this folder
                try:
                    for xml_file in item.glob('*.xml'):
                        if xml_file not in seen_paths:
                            try:
                                tree = ET.parse(xml_file)
                                root = tree.getroot()
                                if root.find('samples') is not None:
                                    inst_name = root.attrib.get('name', item.name)
                                    instruments.append({'name': inst_name, 'path': xml_file})
                                    seen_paths.add(xml_file)
                                    break  # Use first XML with samples in this folder
                            except Exception:
                                pass
                except Exception:
                    pass
                
                # Recursively scan subdirectories (for nested structures like kitpieces/)
                scan_dir(item, depth + 1)
        except Exception:
            pass
    
    scan_dir(kit_dir)
    return instruments


def extract_channels_from_kit(kit_dir: Path, instruments: list):
    """Extract channel list from any instrument's audiofiles.
    
    All instruments in a kit reference the same set of channels, so we extract
    the channel order from the first instrument's audiofiles.
    Returns list of channel names in order (by filechannel).
    """
    if not instruments:
        return []
    
    # Parse the first instrument to get channel order
    first_inst_path = instruments[0]['path']
    tree = ET.parse(first_inst_path)
    root = tree.getroot()
    
    channels_dict = {}  # filechannel -> channel_name
    samples_parent = root.find('samples')
    if samples_parent is not None:
        for sample in samples_parent.findall('sample'):
            for af in sample.findall('audiofile'):
                ch_name = af.attrib.get('channel')
                fc = int(af.attrib.get('filechannel') or 0)
                if ch_name and fc not in channels_dict:
                    channels_dict[fc] = ch_name
    
    # Sort by filechannel number to get correct order
    if channels_dict:
        channels = [channels_dict[i] for i in sorted(channels_dict.keys())]
        return channels
    
    return []


def get_kit_info(kit_dir: Path):
    """Build kit info by scanning filesystem and extracting from instruments.
    
    Returns dict with 'kit_name', 'channels', 'instruments'.
    This is independent of kit XML instrument list, discovering all real instruments.
    """
    # Get kit name from folder or kit XML if available
    kit_name = kit_dir.name
    kit_xml = kit_dir / f"{kit_dir.name}.xml"
    if kit_xml.exists():
        try:
            tree = ET.parse(kit_xml)
            root = tree.getroot()
            kit_name = root.attrib.get('name', kit_name)
        except Exception:
            pass
    
    # Scan for all instruments in the kit directory
    instruments = find_all_instruments(kit_dir)
    
    # Extract channels from the instruments
    channels = extract_channels_from_kit(kit_dir, instruments)
    
    return {
        'kit_name': kit_name,
        'channels': channels,
        'instruments': instruments
    }


def parse_instrument_xml(inst_xml: Path):
    tree = ET.parse(inst_xml)
    root = tree.getroot()
    samples = []
    samples_parent = root.find('samples')
    if samples_parent is None:
        return samples
    for s in samples_parent.findall('sample'):
        sname = s.attrib.get('name')
        power = float(s.attrib.get('power') or 0.0)
        audiofiles = []
        for af in s.findall('audiofile'):
            audiofiles.append({
                'channel': af.attrib.get('channel'),
                'file': af.attrib.get('file'),
                'filechannel': int(af.attrib.get('filechannel') or 0)
            })
        samples.append({'name': sname, 'power': power, 'audiofiles': audiofiles})
    return samples


def map_channels_for_sample(channels, audiofiles, inst_name):
    # Build set of used channel names for this sample
    used = {af['channel'] for af in audiofiles}
    mic_codes = []
    for ch in channels:
        if ch not in used:
            mic_codes.append('x')
            continue
        # exact mapping by exact name
        code = CHANNEL_TO_MIC.get(ch)
        if code:
            mic_codes.append(code)
            continue
        # substring/keyword mapping
        cl = ch.lower()
        matched = False
        for k, v in CHANNEL_KEYWORD_MAP.items():
            if k in cl:
                mic_codes.append(v)
                matched = True
                break
        if matched:
            continue
        # instrument-specific heuristics
        if inst_name.lower() in ch.lower() or any(k in cl for k in ['snare', 'kdrum', 'hihat', 'ride', 'tom', 'crash', 'china', 'splash']):
            # simple direct mic mapping
            if 'snare' in cl:
                mic_codes.append('dtm')
            elif 'kdrum' in cl or 'kick' in cl:
                mic_codes.append('dim')
            elif 'hihat' in cl:
                mic_codes.append('oni')
            else:
                mic_codes.append('dtm')
            continue
        # fallback
        mic_codes.append('x')
    return mic_codes


def infer_velocity(power, max_power, name, index, total, inst_type=None, hihat_variant=None):
    # If hihat variant supplies a default velocity, use it
    if inst_type and inst_type.lower() == 'hihat' and hihat_variant:
        hv = HIHAT_VARIANTS.get(hihat_variant)
        if hv and hv[2] is not None:
            return max(1, min(127, int(hv[2])))

    if max_power and max_power > 0.0:
        ratio = max(0.0, min(1.0, power / max_power))
        # tuned perceptual mapping: gentler curve than sqrt for better spread
        vel = int(round(127.0 * (ratio ** 0.45)))
        return max(1, min(127, vel))
    # fallback to index heuristic
    if total and total > 0:
        vel = int(round(127.0 * ((index + 1) / float(total))))
        return max(1, min(127, vel))
    return 64


def assign_variant(counter_for_vel):
    # 0 -> a, 1 -> b
    return chr(ord('a') + (counter_for_vel % 26))


def split_camel(name: str):
    # Split CamelCase and numbers, e.g. "ChinaL1" -> ["China","L","1"]
    parts = re.findall(r'[A-Z]?[a-z]+|[A-Z]+(?![a-z])|\d+', name)
    return [p for p in parts if p]


def infer_size_label(parts: list):
    """Infer compact size labels like L, R, 1, F1, X1 from split name parts."""
    if not parts:
        return ''

    for idx, token in enumerate(parts):
        tl = token.lower()
        next_token = parts[idx + 1] if idx + 1 < len(parts) else ''
        if tl in ('l', 'r', 'x'):
            if next_token.isdigit():
                return f"{token}{next_token}"
            return token
        if tl == 'f' and next_token.isdigit():
            return f"{token}{next_token}"
        if tl.isdigit():
            return token

    return ''


def map_channel_to_mic(channel_name: str) -> str:
    """Map source channel names to Karatala mic codes."""
    code = CHANNEL_TO_MIC.get(channel_name)
    if code:
        return code

    channel_lower = channel_name.lower()
    for key, value in CHANNEL_KEYWORD_MAP.items():
        if key in channel_lower:
            return value

    if 'snare' in channel_lower:
        return 'dtm'
    if 'kdrum' in channel_lower or 'kick' in channel_lower:
        return 'dim'
    if 'hihat' in channel_lower or 'hh' in channel_lower:
        return 'dtm'
    return 'dtm'


def parse_instrument_name(name: str):
    lname = name.lower()
    parts = split_camel(name)
    inst_type = None
    size = ''
    articulation = None

    KEYWORDS = {
        'kick': 'kick', 'kdrum': 'kick', 'bass': 'kick',
        'snare': 'snare',
        'tom': 'tom', 'ftom': 'tom',
        'hihat': 'hihat', 'hh': 'hihat',
        'ride': 'ride',
        'crash': 'crash',
        'splash': 'splash',
        'china': 'china'
    }

    ARTIC = {
        'open': 'edge[open]',
        'semiopen': 'edge[half]',
        'closed': 'bow[close]',
        'pedal': 'pedal',
        'pedalhit': 'pedal[splash]',
        'rimshot': 'rimshot',
        'rim': 'rim',
        'rest': 'rest',
        'bell': 'bell',
        'stop': 'choke',
        'stopped': 'choke',
        'choke': 'choke',
    }

    # Phrase-level articulation checks before token pass.
    if 'rimshot' in lname:
        articulation = 'rimshot'
    elif 'pedalhit' in lname:
        articulation = 'pedal[splash]'
    elif 'closednopedal' in lname:
        articulation = 'edge[half]'
    elif 'semiopen' in lname:
        articulation = 'edge[half]'
    elif 'stopped' in lname or 'stop' in lname:
        articulation = 'choke'

    for p in parts:
        pl = p.lower()
        if not inst_type and pl in KEYWORDS:
            inst_type = KEYWORDS[pl]
            continue
        if not articulation and pl in ARTIC:
            articulation = ARTIC[pl]

    if not size:
        size = infer_size_label(parts)

    # Extra convention: RXtra/LXtra are side-specific extra crashs/splashes/chinas.
    if not size and 'xtra' in lname:
        size = 'X1'

    if not inst_type:
        # fallback: check substrings
        for k, v in KEYWORDS.items():
            if k in lname:
                inst_type = v
                break

    # detect hihat variant keywords
    hihat_variant = None
    if (inst_type or '').lower() == 'hihat':
        # longest keys first to avoid matching "closed" before "closednopedal"
        for key in sorted(HIHAT_VARIANTS.keys(), key=len, reverse=True):
            if key in lname:
                hihat_variant = key
                hv = HIHAT_VARIANTS.get(key)
                if hv:
                    articulation = hv[0]
                break

    return {'type': inst_type or lname, 'size': size, 'articulation': articulation, 'hihat_variant': hihat_variant}


def _first_number(parts: list) -> int:
    for token in parts:
        if token.isdigit():
            return int(token)
    return 999


def _strip_articulation_suffix(name: str) -> str:
    cleaned = name.lower()
    suffixes = [
        'closednopedal',
        'pedalhit',
        'semiopen',
        'rimshot',
        'stopped',
        'stop',
        'closed',
        'pedal',
        'splash',
        'choke',
        'extra',
        'xtra',
        'open',
        'bell',
        'rest',
        'rim',
    ]
    changed = True
    while changed:
        changed = False
        for suffix in suffixes:
            if cleaned.endswith(suffix):
                cleaned = cleaned[:-len(suffix)]
                changed = True
                break
    return re.sub(r'[^a-z0-9]+', '', cleaned)


def infer_default_articulation(inst_type: str) -> str:
    t = (inst_type or '').lower()
    if t in ('china', 'crash', 'splash'):
        return 'edge'
    if t in ('snare', 'kick', 'tom'):
        return 'center'
    if t == 'hihat':
        return 'edge'
    if t == 'ride':
        return 'bow'
    return 'center'


def resolve_articulation(parsed: dict, inst_type: str) -> str:
    artic = parsed.get('articulation')
    if not artic:
        artic = infer_default_articulation(inst_type)

    if (inst_type or '').lower() == 'hihat' and parsed.get('hihat_variant'):
        variant_key = parsed.get('hihat_variant')
        variant_info = HIHAT_VARIANTS.get(variant_key)
        if variant_info:
            artic = variant_info[0]
            if variant_info[1] and variant_info[2] is not None:
                artic = f"{artic}-{variant_info[1]}{int(variant_info[2]):03d}"
    return artic


def enumerate_instrument_slots(instruments: list, parsed_by_path: dict) -> dict:
    """Assign stable per-kit size labels before emitting any commands."""
    slot_by_path = {}

    # Tom + floor tom share one X-index sequence.
    tom_entries = []
    for inst in instruments:
        path_key = str(inst['path'])
        parsed = parsed_by_path[path_key]
        if (parsed.get('type') or '').lower() == 'tom':
            lname = inst['name'].lower()
            is_floor = ('ftom' in lname)
            parts = split_camel(inst['name'])
            tom_entries.append((is_floor, _first_number(parts), inst['name'].lower(), path_key))
    tom_entries.sort()
    for idx, (_, _, _, path_key) in enumerate(tom_entries, start=1):
        slot_by_path[path_key] = f"X{idx}"

    # Hi-hat family: always use X-index labels.
    hihat_entries = []
    for inst in instruments:
        path_key = str(inst['path'])
        parsed = parsed_by_path[path_key]
        if (parsed.get('type') or '').lower() == 'hihat':
            hihat_entries.append((inst['name'].lower(), path_key))
    hihat_entries.sort()
    for idx, (_, path_key) in enumerate(hihat_entries, start=1):
        slot_by_path[path_key] = f"X{idx}"

    # Side-based cymbals: letter must always have numeric suffix (L1, R1...).
    for side_type in ('crash', 'china', 'splash', 'ride'):
        grouped = defaultdict(list)
        for inst in instruments:
            path_key = str(inst['path'])
            parsed = parsed_by_path[path_key]
            inst_type = (parsed.get('type') or '').lower()
            if inst_type != side_type:
                continue
            parts = split_camel(inst['name'])
            side = ''
            explicit_size = ''
            for token in parts:
                tl = token.lower()
                if tl in ('l', 'r'):
                    side = token.upper()
                elif tl.isdigit():
                    value = int(tl)
                    if value >= 6:
                        explicit_size = str(value)
            grouped[side].append((inst['name'], path_key, explicit_size))

        for side, entries in grouped.items():
            entries.sort(key=lambda item: item[0].lower())
            stem_to_index = {}
            next_index = 1
            for inst_name, path_key, explicit_size in entries:
                if explicit_size:
                    slot_by_path[path_key] = explicit_size
                    continue
                stem_key = _strip_articulation_suffix(inst_name)
                if stem_key not in stem_to_index:
                    stem_to_index[stem_key] = next_index
                    next_index += 1
                if side:
                    slot_by_path[path_key] = f"{side}{stem_to_index[stem_key]}"
                else:
                    slot_by_path[path_key] = f"X{stem_to_index[stem_key]}"

    # Generic fallback for everything else.
    fallback_counters = defaultdict(int)
    for inst in instruments:
        path_key = str(inst['path'])
        if path_key in slot_by_path:
            continue
        parsed = parsed_by_path[path_key]
        inst_type = (parsed.get('type') or '').lower()
        base_size = (parsed.get('size') or '').strip()

        if re.fullmatch(r'[LRX]\d+', base_size, flags=re.IGNORECASE):
            slot_by_path[path_key] = base_size.upper()
            continue

        if re.fullmatch(r'[LR]', base_size, flags=re.IGNORECASE):
            slot_by_path[path_key] = f"{base_size.upper()}1"
            continue

        if base_size.isdigit():
            number = int(base_size)
            if number >= 6:
                slot_by_path[path_key] = base_size
            else:
                slot_by_path[path_key] = f"X{number}"
            continue

        fallback_counters[inst_type] += 1
        slot_by_path[path_key] = f"X{fallback_counters[inst_type]}"

    return slot_by_path


def is_channel_relevant_for_type(inst_type: str, channel_name: str, inst_name: str = None) -> bool:
    """Return True if channel should be considered for this instrument type.

    Uses CamelCase word splitting to avoid substring false positives like 'tom' in 'bottom'.
    
    For tom/tom-like instruments, matches only that specific instrument's channels plus common mics.
    E.g., FTom1 matches only FTom1 channels + ambient/OH, not Tom1/Tom2/FTom2 channels.
    """
    parts = split_camel(channel_name)  # e.g., "SnareBottom" -> ['Snare', 'Bottom']
    parts_lower = [p.lower() for p in parts]
    t = (inst_type or '').lower()
    cl = channel_name.lower()
    
    # Instrument-specific keywords (word-level, not substring)
    def has_word(keywords):
        return any(kw in parts_lower for kw in keywords)
    
    # Ambient/room mic keywords (substring OK, generally safe)
    ambient = any(k in cl for k in ('amb', 'room'))
    overhead = any(k in cl for k in ('oh', 'ohcenter', 'oh '))
    
    if t in ('china', 'crash', 'splash'):
        return has_word(['china', 'crash', 'splash']) or ambient or overhead
    if t == 'kick':
        return has_word(['kick', 'kdrum', 'bass']) or ambient or overhead
    if t == 'snare':
        return has_word(['snare']) or ambient or overhead
    if t in ('tom', 'ftom'):
        # Exclude snare channels (Tom != Snare)
        if has_word(['snare']):
            return False
        # Common mics always included
        if ambient or overhead:
            return True
        # For specific tom instances (FTom1, Tom2, etc.), match ONLY channels that
        # start with the exact instrument name (e.g., FTom1 matches FTom1, FTom1Left, FTom1Right)
        # This prevents FTom1 from matching FTom2 or Tom1 from matching Tom2
        if inst_name:
            # Check if channel name starts with this instrument name (case-insensitive)
            if cl.startswith(inst_name.lower()):
                return True
            return False
        else:
            # If we don't know the specific instrument, allow all toms (fallback)
            return has_word(['tom', 'ftom'])
    if t == 'hihat':
        return has_word(['hihat', 'hh']) or ambient or overhead
    # default: allow amb/oh and exact type matches
    return ambient or overhead or t in parts_lower


def _base_channel_name(ch: str) -> str:
    # remove common L/R suffixes
    return re.sub(r'(?:Left|Right|L|R)$', '', ch, flags=re.IGNORECASE)


def compress_mic_chain(channels: list, mic_codes: list) -> list:
    """Collapse obvious L/R stereo pairs when they map to the same mic code.

    Keeps order; when a Left/Right adjacent pair is detected and both codes
    are equal and not 'x', emit a single code (stereo). Otherwise emit codes
    as-is.
    """
    out = []
    i = 0
    while i < len(channels):
        ch = channels[i]
        code = mic_codes[i]
        if i + 1 < len(channels):
            ch2 = channels[i+1]
            code2 = mic_codes[i+1]
            if _base_channel_name(ch).lower() == _base_channel_name(ch2).lower():
                # possible stereo pair
                if code == code2 and code != 'x':
                    out.append(code)
                    i += 2
                    continue
        out.append(code)
        i += 1
    return out


def build_filename(inst_type, size, artic, velocity, variant, mic_chain, ext):
    parts = [inst_type]
    if size:
        parts.append(size)
    if artic:
        parts.append(artic)

    vel_part = f"{velocity:03d}{variant}"
    mic_part = "_".join(mic_chain)
    fname = f"{'-'.join(parts)}-{vel_part}-{mic_part}.{ext}"
    return fname


def platform_commands(os_name, mkdir_path, src, dst):
    if os_name == 'Windows':
        # Emit Windows cmd.exe commands (not PowerShell)
        mkdir_cmd = f'mkdir "{mkdir_path}"'
        file_cmd = f'copy "{src}" "{dst}"'
    else:
        mkdir_cmd = f"mkdir -p \"{mkdir_path}\""
        file_cmd = f"cp \"{src}\" \"{dst}\""
    return mkdir_cmd, file_cmd


def process_kit(kit_dir: Path, output_dir: Path, os_name: str):
    # Build kit info by scanning filesystem for all actual instruments
    kit = get_kit_info(kit_dir)
    
    if not kit['instruments']:
        print(f"Skipping {kit_dir}: no instruments found")
        return
    
    kit_name = kit['kit_name']
    channels = kit['channels']
    bat_lines = ["@echo off", "setlocal"]

    def emit(line: str):
        print(line)
        if line.startswith('#'):
            bat_lines.append("REM " + line)
        else:
            bat_lines.append(line)

    emit(f"# Kit: {kit_name} ({kit_dir}) — channels: {len(channels)} instruments: {len(kit['instruments'])}")

    printed_dirs = set()
    summary = {
        'kit': kit_name,
        'instruments': len(kit['instruments']),
        'samples': 0,
        'commands': 0,
        'missing_source': 0,
        'errors': 0,
    }
    cwd = Path.cwd()

    def short(p: Path) -> str:
        try:
            return str(p.relative_to(cwd))
        except Exception:
            return str(p)

    # Pre-parse instrument names and cache parsed metadata.
    parsed_map = {}
    parsed_by_path = {}
    for inst in kit['instruments']:
        parsed = parse_instrument_name(inst['name'])
        parsed_map[inst['name']] = parsed
        parsed_by_path[str(inst['path'])] = parsed

    slot_by_path = enumerate_instrument_slots(kit['instruments'], parsed_by_path)

    # Print deterministic slot plan before any file command output.
    emit("# Instrument slot plan:")
    for inst in kit['instruments']:
        inst_name = inst['name']
        path_key = str(inst['path'])
        parsed = parsed_by_path[path_key]
        inst_type = (parsed.get('type') or inst_name).lower()
        size_label = slot_by_path.get(path_key, 'X1')
        artic_preview = resolve_articulation(parsed, inst_type)
        emit(f"#   {inst_name} -> {inst_type}-{size_label}/{artic_preview}")

    # Process each instrument.
    for inst in kit['instruments']:
        inst_name = inst['name']
        parsed = parsed_map[inst_name]
        itype = (parsed['type'] or inst_name).lower()
        size_label = slot_by_path.get(str(inst['path']), 'X1')
        # inst['path'] is already the full path to the instrument XML from find_all_instruments
        inst_file = inst.get('path')
        if not inst_file or not Path(inst_file).exists():
            emit(f"  Instrument file missing: {inst_file}")
            summary['errors'] += 1
            continue
        samples = parse_instrument_xml(inst_file)
        if not samples:
            emit(f"  No samples for {inst_name}")
            continue

        max_power = max((s['power'] for s in samples), default=0.0)
        # per-velocity counters
        vel_counters = defaultdict(int)
        for idx, s in enumerate(samples):
            summary['samples'] += 1
            # infer velocity, passing instrument type and any hihat variant
            hihat_variant = parsed.get('hihat_variant') if parsed else None
            velocity = infer_velocity(s['power'], max_power, s['name'], idx, len(samples), inst_type=itype, hihat_variant=hihat_variant)
            vcount = vel_counters[velocity]
            variant = assign_variant(vcount)
            vel_counters[velocity] += 1
            # choose first audiofile as source file for simplicity
            af = s['audiofiles'][0]
            src = (inst_file.parent / af['file']).resolve()
            if not src.exists():
                emit(f"  Missing source file: {src}")
                summary['missing_source'] += 1
                continue
            ext = Path(af['file']).suffix.lstrip('.')
            
            # Build mic-chain based on ACTUAL AUDIO FILE order (filechannel), not kit's channel list.
            # Sort audiofiles by filechannel to get the real track ordering.
            sorted_audiofiles = sorted(s['audiofiles'], key=lambda x: int(x['filechannel']))
            audio_channels_ordered = [af_sorted['channel'] for af_sorted in sorted_audiofiles]
            
            # Build mic codes for audio file's actual channel order
            raw_mic_chain = []
            for ch in audio_channels_ordered:
                # if channel is not relevant to this instrument type, mark 'x'
                if not is_channel_relevant_for_type(itype, ch, inst_name):
                    raw_mic_chain.append('x')
                    continue
                raw_mic_chain.append(map_channel_to_mic(ch))

            # Compress stereo pairs (adjacent L/R channels with same mic code)
            # e.g., rfs_rfs becomes rfs, ons_ons becomes ons
            mic_chain = compress_mic_chain(audio_channels_ordered, raw_mic_chain)
            
            # For comments, show the actual audio file's channel order
            comment_channels = audio_channels_ordered
            comment_mic_map = []
            for ch in audio_channels_ordered:
                if not is_channel_relevant_for_type(itype, ch, inst_name):
                    comment_mic_map.append('x')
                    continue
                comment_mic_map.append(map_channel_to_mic(ch))
            # use parsed info and assigned size_label from pre-enumeration pass
            size = size_label
            artic = resolve_articulation(parsed, itype)

            fname = build_filename(itype, size, artic, velocity, variant, mic_chain, ext)
            inst_folder = itype + (f"-{size}" if size else '')
            target_dir = output_dir / kit_name / inst_folder / artic
            target_dir = target_dir.resolve()
            dst = target_dir / fname
            mkdir_cmd, file_cmd = platform_commands(os_name, str(target_dir), str(src), str(dst))
            # Print mkdir only once per target dir; comment it for dry-run clarity
            td = str(target_dir)
            if td not in printed_dirs:
                printed_dirs.add(td)
                # print one mkdir comment
                emit(f"# {mkdir_cmd}")
                # print instrument xml path, audio file's actual channel order, and mic map
                try:
                    inst_xml_path = str(inst_file.resolve())
                except Exception:
                    inst_xml_path = str(inst_file)
                emit('# "' + inst_xml_path + '"')
                col_width = 12
                emit('# ' + ''.join(f"{c:<{col_width}}" for c in comment_channels))
                emit('# ' + ''.join(f"{m:<{col_width}}" for m in comment_mic_map))

            # show relative paths when possible to reduce line length
            emit(file_cmd.replace(str(src), short(src)).replace(str(dst), short(dst)))
            summary['commands'] += 1

    emit(
        f"Summary [{kit_name}]: instruments={summary['instruments']} "
        f"samples={summary['samples']} commands={summary['commands']} "
        f"missing_source={summary['missing_source']} errors={summary['errors']}"
    )

    safe_kit = re.sub(r'[^A-Za-z0-9_.-]+', '_', kit_name).strip('_') or 'kit'
    bat_dir = (output_dir / kit_name).resolve()
    bat_dir.mkdir(parents=True, exist_ok=True)
    bat_path = bat_dir / f"convert-{safe_kit}.bat"
    bat_text = "\n".join(bat_lines) + "\n"
    with bat_path.open('w', encoding='utf-8', newline='\n') as bat_file:
        bat_file.write(bat_text)
    emit(f"# BAT: {bat_path}")

    return summary


def main():
    p = argparse.ArgumentParser(description='Convert Drumgizmo kits to Karatala format (dry-run default)')
    p.add_argument('todo_path', nargs='?', default='todo', help='Path to todo kits root')
    p.add_argument('--output-dir', default='instruments', help='Output dir for organized kits')
    args = p.parse_args()

    todo = Path(args.todo_path)
    if not todo.exists():
        print(f"Path not found: {todo}")
        sys.exit(1)

    os_name = 'Windows' if os.name == 'nt' else 'Unix'

    # Detect if provided path is a single kit or a root directory of multiple kits.
    # A kit directory has a {dirname}.xml file at the root level.
    kit_xml = todo / f"{todo.name}.xml"
    is_single_kit = kit_xml.exists() or (
        list(todo.glob('*.xml')) and 
        find_all_instruments(todo)
    )
    
    if is_single_kit:
        process_kit(todo, Path(args.output_dir), os_name)
        return

    kits = find_kits(todo)
    totals = {
        'kits': 0,
        'instruments': 0,
        'samples': 0,
        'commands': 0,
        'missing_source': 0,
        'errors': 0,
    }
    for k in kits:
        kit_summary = process_kit(k, Path(args.output_dir), os_name)
        if not kit_summary:
            continue
        totals['kits'] += 1
        totals['instruments'] += kit_summary['instruments']
        totals['samples'] += kit_summary['samples']
        totals['commands'] += kit_summary['commands']
        totals['missing_source'] += kit_summary['missing_source']
        totals['errors'] += kit_summary['errors']

    if totals['kits'] > 0:
        print(
            f"Total: kits={totals['kits']} instruments={totals['instruments']} "
            f"samples={totals['samples']} commands={totals['commands']} "
            f"missing_source={totals['missing_source']} errors={totals['errors']}"
        )


if __name__ == '__main__':
    main()
