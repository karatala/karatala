#!/usr/bin/env python3
"""Drumgizmo -> Karatala converter (initial implementation)

Defaults to dry-run: prints platform-specific commands without executing.

Usage: python converter.py todo/ --output-dir instruments


# HOWTO:
python scripts\convert-drumgizmo2karatala.py assets\drumgizmo --output-dir instruments_py --overwrite-map
# Review/edit:
    instruments_py\<kit>\map_<kit>.py
# After editing map_<kit>.py, refresh the CSV from the edited map:

python scripts\convert-drumgizmo2karatala.py assets\drumgizmo --output-dir instruments_py --write-review

python instruments_py\aasimonster\convert_aasimonster.py > instruments_py\aasimonster.bat
python instruments_py\aasimonster2\convert_aasimonster2.py > instruments_py\aasimonster2.bat
python instruments_py\artstar\convert_artstar.py > instruments_py\artstar.bat
python instruments_py\CrocellKit\convert_CrocellKit.py > instruments_py\CrocellKit.bat
python instruments_py\MuldjordKit3\convert_MuldjordKit3.py > instruments_py\MuldjordKit3.bat
python instruments_py\Salamander\convert_Salamander.py > instruments_py\Salamander.bat
python instruments_py\tchackpoum_drumgizmo_13\convert_tchackpoum_drumgizmo_13.py > instruments_py\tchackpoum_drumgizmo_13.bat

"""
import argparse
import csv
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from collections import defaultdict
from dataclasses import asdict, dataclass, field
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

KNOWN_INSTRUMENT_TYPES = {
    'percussion', 'drum', 'cymbal', 'kick', 'tom', 'snare', 'ride', 'hihat', 'crash', 'splash', 'china'
}

INSTRUMENT_TYPE_ALIASES = {
    'zilbel': 'percussion',
    'cowbell': 'percussion',
    'bellchime': 'percussion',
    'stagg': 'percussion',
    'cowbell_muff_hi': 'percussion',
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
    seen_names = set()  # Track instrument names to skip duplicates (e.g. _sub variants)
    
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
                            if inst_name not in seen_names:
                                instruments.append({'name': inst_name, 'path': inst_xml})
                                seen_names.add(inst_name)
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
                                    inst_name = root.attrib.get('name', xml_file.stem)
                                    if inst_name not in seen_names:
                                        instruments.append({'name': inst_name, 'path': xml_file})
                                        seen_names.add(inst_name)
                                    seen_paths.add(xml_file)
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


def extract_channels_from_kit(kit_dir: Path, instruments: list, kit_xml: Path = None):
    """Extract channel list from kit XML or instrument audiofiles.
    
    Prefers <channels> from kit XML if available.
    Falls back to merging channels from all instruments.
    """
    # Try kit XML <channels> definition first
    if kit_xml and kit_xml.exists():
        try:
            tree = ET.parse(kit_xml)
            root = tree.getroot()
            channels_elem = root.find('channels')
            if channels_elem is not None:
                channels = []
                for ch in channels_elem.findall('channel'):
                    ch_name = ch.attrib.get('name')
                    if ch_name:
                        channels.append(ch_name)
                if channels:
                    return channels
        except Exception:
            pass

    if not instruments:
        return []

    # Fallback: merge channels from ALL instruments' audiofiles
    channels_dict = {}
    for inst in instruments:
        try:
            tree = ET.parse(inst['path'])
            root = tree.getroot()
            samples_parent = root.find('samples')
            if samples_parent is not None:
                for sample in samples_parent.findall('sample'):
                    for af in sample.findall('audiofile'):
                        ch_name = af.attrib.get('channel')
                        fc = int(af.attrib.get('filechannel') or 0)
                        if ch_name and fc not in channels_dict:
                            channels_dict[fc] = ch_name
                    break  # Only need first sample per instrument
        except Exception:
            pass

    if channels_dict:
        channels = [channels_dict[idx] for idx in sorted(channels_dict.keys())]
        return channels

    return []


def _find_kit_xml(kit_dir: Path) -> Path:
    """Find the main kit XML (the one with the most channels)."""
    best = None
    best_channel_count = 0
    candidates = list(kit_dir.glob('*.xml'))
    for subdir in sorted(kit_dir.iterdir()):
        if subdir.is_dir():
            candidates.extend(subdir.glob('*.xml'))
    for candidate in candidates:
        try:
            tree = ET.parse(candidate)
            root = tree.getroot()
            channels_elem = root.find('channels')
            if channels_elem is not None:
                count = len(channels_elem.findall('channel'))
                if count > best_channel_count:
                    best = candidate
                    best_channel_count = count
        except Exception:
            pass
    return best


def get_kit_info(kit_dir: Path):
    """Build kit info by scanning filesystem and extracting from instruments.
    
    Returns dict with 'kit_name', 'channels', 'instruments'.
    This is independent of kit XML instrument list, discovering all real instruments.
    """
    kit_name = kit_dir.name
    kit_xml = _find_kit_xml(kit_dir)
    if kit_xml:
        try:
            tree = ET.parse(kit_xml)
            root = tree.getroot()
            kit_name = root.attrib.get('name', kit_name)
        except Exception:
            pass
    
    # Scan for all instruments in the kit directory
    instruments = find_all_instruments(kit_dir)
    
    # Extract channels from kit XML or instruments
    channels = extract_channels_from_kit(kit_dir, instruments, kit_xml)
    
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
    # Parse velocity ranges if available (used by some kits instead of power)
    velocity_map = {}
    velocities_parent = root.find('velocities')
    if velocities_parent is not None:
        for vel in velocities_parent.findall('velocity'):
            lower = float(vel.attrib.get('lower', 0))
            upper = float(vel.attrib.get('upper', 1))
            for sref in vel.findall('sampleref'):
                ref_name = sref.attrib.get('name')
                if ref_name:
                    velocity_map[ref_name] = (lower, upper)
    for s in samples_parent.findall('sample'):
        sname = s.attrib.get('name')
        power = float(s.attrib.get('power') or 0.0)
        if power == 0.0 and sname in velocity_map:
            lower, upper = velocity_map[sname]
            power = (lower + upper) / 2.0
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
    if 'amb' in channel_lower or 'room' in channel_lower:
        return 'rfs'
    if 'oh' in channel_lower:
        return 'ons'
    if 'snare' in channel_lower and ('bottom' in channel_lower or 'btm' in channel_lower):
        return 'dbm'
    if 'snare' in channel_lower and 'trigger' in channel_lower:
        return 'dtm1'
    if 'snare' in channel_lower:
        return 'dtm'
    if ('kick' in channel_lower or 'kdrum' in channel_lower) and ('out' in channel_lower or 'outside' in channel_lower):
        return 'dom'
    if ('kick' in channel_lower or 'kdrum' in channel_lower) and is_right_channel(channel_name):
        return 'dim'
    if ('kick' in channel_lower or 'kdrum' in channel_lower) and is_left_channel(channel_name):
        return 'dim'
    for key, value in CHANNEL_KEYWORD_MAP.items():
        if key in channel_lower:
            return value

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
        'china': 'china',
        'cymb': 'crash',
        'zilbel': 'percussion',
        'cowbell': 'percussion',
        'bellchime': 'percussion',
        'stagg': 'percussion',
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
        'foot': 'pedal',
        'pdl': 'pedal',
        'ped': 'pedal',
        'edg': 'edge',
        'stop': 'choke',
        'stopped': 'choke',
        'choke': 'choke',
    }

    # Phrase-level articulation checks before token pass.
    # Strip separators to match compound names like "rim_shot" -> "rimshot"
    clean_lname = lname.replace('_', '').replace('-', '')
    if 'rimshot' in clean_lname:
        articulation = 'rimshot'
    elif 'pedalhit' in clean_lname:
        articulation = 'pedal[splash]'
    elif 'closednopedal' in clean_lname:
        articulation = 'edge[half]'
    elif 'semiopen' in clean_lname:
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

    resolved_type = INSTRUMENT_TYPE_ALIASES.get(inst_type or lname, inst_type or lname)
    return {'type': resolved_type, 'source_type': inst_type or lname, 'size': size, 'articulation': articulation, 'hihat_variant': hihat_variant}


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
        'open',
        'bell',
        'rest',
        'rim',
    ]
    changed = True
    while changed:
        changed = False
        # Strip trailing separators only
        stripped = re.sub(r'[_\-]+$', '', cleaned)
        if stripped != cleaned:
            cleaned = stripped
            changed = True
            continue
        # Try stripping articulation suffixes
        for suffix in suffixes:
            if cleaned.endswith(suffix):
                cleaned = cleaned[:-len(suffix)]
                changed = True
                break
        if changed:
            continue
        # Strip trailing digits only if they expose an articulation suffix
        digit_stripped = re.sub(r'\d+$', '', cleaned)
        if digit_stripped != cleaned:
            for suffix in suffixes:
                if digit_stripped.rstrip('_-').endswith(suffix):
                    cleaned = digit_stripped
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

    # Hi-hat family: keep openness/articulation variants for one physical hat in the same slot.
    hihat_entries = []
    for inst in instruments:
        path_key = str(inst['path'])
        parsed = parsed_by_path[path_key]
        if (parsed.get('type') or '').lower() == 'hihat':
            stem_key = _strip_articulation_suffix(inst['name'])
            hihat_entries.append((stem_key, inst['name'].lower(), path_key))
    hihat_entries.sort()
    hihat_stem_to_index = {}
    next_hihat_index = 1
    for stem_key, _, path_key in hihat_entries:
        if stem_key not in hihat_stem_to_index:
            hihat_stem_to_index[stem_key] = next_hihat_index
            next_hihat_index += 1
        slot_by_path[path_key] = f"X{hihat_stem_to_index[stem_key]}"

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
                    if 6 <= value <= 26:
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
            if 6 <= number <= 26:
                slot_by_path[path_key] = base_size
            else:
                fallback_counters[inst_type] += 1
                slot_by_path[path_key] = f"X{fallback_counters[inst_type]}"
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
    if t == 'ride':
        if ambient or overhead:
            return True
        if inst_name:
            return cl.startswith(inst_name.lower())
        return has_word(['ride'])
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
    output_name = kit_dir.name
    channels = kit['channels']
    bat_lines = ["@echo off", "setlocal"]

    def emit(line: str):
        print(line)
        stripped = line.lstrip()
        if stripped.startswith('#'):
            bat_lines.append("REM " + stripped)
        elif stripped.startswith(('Missing', 'Instrument file', 'No samples', 'Summary', 'Skipping')):
            bat_lines.append("REM " + stripped)
        else:
            bat_lines.append(line)

    emit(f"# Kit: {kit_name} ({kit_dir}) - channels: {len(channels)} instruments: {len(kit['instruments'])}")

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
            target_dir = output_dir / output_name / inst_folder / artic
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

    safe_kit = re.sub(r'[^A-Za-z0-9_.-]+', '_', output_name).strip('_') or 'kit'
    bat_dir = (output_dir / output_name).resolve()
    bat_dir.mkdir(parents=True, exist_ok=True)
    bat_path = bat_dir / f"convert-{safe_kit}.bat"
    bat_text = "\n".join(bat_lines) + "\n"
    with bat_path.open('w', encoding='utf-8', newline='\n') as bat_file:
        bat_file.write(bat_text)
    emit(f"# BAT: {bat_path}")

    return summary


def legacy_copy_main():
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


VALID_MIC_RE = re.compile(r'^[dor][ustbionwcf][msi][0-9]?$')


@dataclass
class SourceChannelDecision:
    channel_name: str
    source_file: str
    filechannel: int
    source_channel_index: int
    keep: bool
    mic_code: str
    group_key: str
    group_label: str
    reason: str


@dataclass
class OutputFilePlan:
    kit: str
    source_instrument: str
    instrument_key: str
    sample_name: str
    sample_index: int
    power: float
    velocity: int
    variant: str
    source_xml: str
    instrument_default: str
    articulation_default: str
    param_default: str
    group_key: str
    group_label: str
    mic_default: str
    keep_var: str
    mic_var: str
    source_files: list
    source_channels: list
    source_channel_indices: list
    target_relative: str
    filename_template: str
    default_keep: bool = True
    quarantined: bool = False
    quarantine_reasons: list = field(default_factory=list)


@dataclass
class KitGenerationSummary:
    kit: str
    instruments: int = 0
    samples: int = 0
    channel_decisions: int = 0
    output_plans: int = 0
    clean_outputs: int = 0
    quarantined_outputs: int = 0
    missing_sources: int = 0
    written_files: list = field(default_factory=list)
    warnings: list = field(default_factory=list)


def split_articulation_param(articulation: str) -> tuple:
    parts = articulation.split('-')
    if len(parts) >= 2 and re.match(r'^[a-z_]+\d{1,3}$', parts[-1]):
        return '-'.join(parts[:-1]), '-' + parts[-1]
    return articulation, ''


def safe_var_token(value: str) -> str:
    token = re.sub(r'[^A-Za-z0-9]+', '_', value.upper()).strip('_')
    if not token:
        token = 'ITEM'
    if token[0].isdigit():
        token = 'N_' + token
    return token


def rel_to_root(path: Path, root: Path) -> str:
    try:
        return str(path.resolve().relative_to(root.resolve())).replace('/', '\\')
    except Exception:
        return str(path.resolve()).replace('/', '\\')


def batch_path_var(path: str, root_var: str) -> str:
    normalized = path.replace('/', '\\')
    return f"%{root_var}%\\{normalized}"


def normalize_channel_name(channel_name: str) -> str:
    parts = [part.lower() for part in split_camel(channel_name)]
    parts = [part for part in parts if part not in ('left', 'right', 'l', 'r') and not part.isdigit()]
    if parts:
        compact = ''.join(parts)
        if len(compact) > 1 and compact[-1] in ('l', 'r'):
            return compact[:-1]
        return compact
    lowered = channel_name.lower()
    lowered = re.sub(r'\b(left|right|l|r)\b', '', lowered)
    lowered = re.sub(r'\d+', '', lowered)
    compact = re.sub(r'[^a-z0-9]+', '', lowered)
    if len(compact) > 1 and compact[-1] in ('l', 'r'):
        return compact[:-1]
    return compact


def is_left_channel(channel_name: str) -> bool:
    parts = [part.lower() for part in split_camel(channel_name)]
    compact = re.sub(r'[^a-z0-9]+', '', channel_name.lower())
    return any(part in ('left', 'l') for part in parts) or compact.endswith('l') or re.search(r'(^|[^a-z])(left|l)([^a-z]|$)', channel_name.lower()) is not None


def is_right_channel(channel_name: str) -> bool:
    parts = [part.lower() for part in split_camel(channel_name)]
    compact = re.sub(r'[^a-z0-9]+', '', channel_name.lower())
    return any(part in ('right', 'r') for part in parts) or compact.endswith('r') or re.search(r'(^|[^a-z])(right|r)([^a-z]|$)', channel_name.lower()) is not None


def is_stereo_pair(left_name: str, right_name: str, left_mic: str, right_mic: str) -> bool:
    if left_mic != right_mic:
        return False
    if left_mic not in ('rfs', 'ons', 'ows', 'oni'):
        return False
    return normalize_channel_name(left_name) == normalize_channel_name(right_name) and is_left_channel(left_name) and is_right_channel(right_name)


def filechannel_to_source_index(audiofiles: list, filechannel: int) -> int:
    positive_channels = [int(af.get('filechannel') or 0) for af in audiofiles if int(af.get('filechannel') or 0) > 0]
    if positive_channels:
        return max(0, filechannel - 1)
    return max(0, filechannel)


def map_channel_group_key(channel_name: str, mic_code: str) -> str:
    lowered = channel_name.lower()
    compact = re.sub(r'[^a-z0-9]+', '', lowered)
    if 'amb' in lowered or 'room' in lowered:
        return 'AMB'
    if 'ohcenter' in lowered or ('oh' in lowered and 'center' in lowered):
        return 'OH_CENTER'
    if 'oh' in lowered:
        return 'OH'
    if compact == 'kdruml':
        return 'KDRUML'
    if compact == 'kdrumr':
        return 'KDRUMR'
    if ('kick' in lowered or 'kdrum' in lowered) and ('trg' in lowered or 'trigger' in lowered):
        return 'KICK_TRIGGER'
    if 'kdrumoutside' in lowered:
        return 'KICK_OUT'
    if 'kdruminside' in lowered:
        return 'KICK_IN'
    if 'kick' in lowered and is_left_channel(channel_name):
        return 'KICK_L'
    if 'kick' in lowered and is_right_channel(channel_name):
        return 'KICK_R'
    if 'kick' in lowered:
        return 'KICK_IN'
    if 'snare' in lowered and ('bottom' in lowered or 'btm' in lowered):
        return 'SNARE_BOTTOM'
    if 'snare' in lowered and 'trigger' in lowered:
        return 'SNARE_TRIGGER'
    if 'snare' in lowered:
        return 'SNARE_TOP'
    if 'hihat' in lowered or 'hh' in lowered:
        return 'HIHAT'
    if compact == 'ridel':
        return 'RIDEL'
    if compact == 'rider':
        return 'RIDER'
    if 'ride' in lowered:
        return 'RIDE'
    ftom_match = re.search(r'ftom\s*([0-9]+)', lowered)
    if ftom_match:
        return 'FTOM' + ftom_match.group(1)
    if 'tom' in lowered:
        digit_match = re.search(r'tom\s*([0-9]+)', lowered)
        if digit_match:
            return 'TOM' + digit_match.group(1)
        return 'TOM'
    return safe_var_token(mic_code or channel_name)


def build_channel_decisions(inst_file: Path, sample: dict, inst_type: str, inst_name: str, source_root: Path) -> list:
    audiofiles = sorted(sample['audiofiles'], key=lambda item: int(item['filechannel']))
    decisions = []
    for audiofile in audiofiles:
        channel_name = audiofile['channel'] or ''
        source = (inst_file.parent / audiofile['file']).resolve()
        filechannel = int(audiofile['filechannel'] or 0)
        source_channel_index = filechannel_to_source_index(audiofiles, filechannel)
        relevant = is_channel_relevant_for_type(inst_type, channel_name, inst_name)
        mic_code = map_channel_to_mic(channel_name)
        group_key = map_channel_group_key(channel_name, mic_code)
        reason = 'keep useful mic' if relevant else 'drop bleed/non-current instrument mic'
        if not VALID_MIC_RE.match(mic_code):
            reason = 'invalid mic code'
        decisions.append(SourceChannelDecision(
            channel_name=channel_name,
            source_file=rel_to_root(source, source_root),
            filechannel=filechannel,
            source_channel_index=source_channel_index,
            keep=relevant and VALID_MIC_RE.match(mic_code) is not None,
            mic_code=mic_code,
            group_key=group_key,
            group_label=channel_name,
            reason=reason,
        ))
    return decisions


def group_channel_decisions(decisions: list, include_disabled: bool = False) -> list:
    kept = decisions if include_disabled else [decision for decision in decisions if decision.keep]
    groups = []
    decision_idx = 0
    while decision_idx < len(kept):
        current = kept[decision_idx]
        if decision_idx + 1 < len(kept):
            following = kept[decision_idx + 1]
            if is_stereo_pair(current.channel_name, following.channel_name, current.mic_code, following.mic_code):
                group = [current, following]
                group_label = current.channel_name + ' + ' + following.channel_name
                groups.append((current.group_key, group_label, current.mic_code, group))
                decision_idx += 2
                continue
        groups.append((current.group_key, current.channel_name, current.mic_code, [current]))
        decision_idx += 1
    return groups


def build_filename_template(inst_var: str, art_var: str, velocity: int, variant: str, mic_var: str, audio_format: str) -> str:
    return f"%{inst_var}%-%{art_var}%-{velocity:03d}{variant}-%{mic_var}%.{audio_format}"


def build_default_filename(instrument: str, articulation: str, velocity: int, variant: str, mic_code: str, audio_format: str) -> str:
    return f"{instrument}-{articulation}-{velocity:03d}{variant}-{mic_code}.{audio_format}"


def parse_filename_like_kitlib(file_name: str) -> tuple:
    stem = Path(file_name).stem
    parts = stem.split('-')
    if len(parts) < 5:
        return False, 'too few dash-separated filename parts'
    vel_rr = parts[3]
    split_index = 0
    while split_index < len(vel_rr) and vel_rr[split_index].isdigit():
        split_index += 1
    if split_index == 0:
        return False, 'velocity segment does not start with digits'
    velocity = int(vel_rr[:split_index])
    if velocity <= 0 or velocity > 127:
        return False, 'velocity out of 1-127 range'
    mic_tokens = [token for token in parts[4].split('_') if token]
    if not mic_tokens:
        return False, 'missing mic token'
    invalid_mics = [token for token in mic_tokens if not VALID_MIC_RE.match(token)]
    if invalid_mics:
        return False, 'invalid mic token(s): ' + ', '.join(invalid_mics)
    return True, ''


def build_output_plans(kit_dir: Path, source_root: Path, audio_format: str) -> tuple:
    kit = get_kit_info(kit_dir)
    output_name = kit_dir.name
    parsed_map = {}
    parsed_by_path = {}
    for inst in kit['instruments']:
        parsed = parse_instrument_name(inst['name'])
        parsed_map[inst['name']] = parsed
        parsed_by_path[str(inst['path'])] = parsed

    slot_by_path = enumerate_instrument_slots(kit['instruments'], parsed_by_path)
    plans = []
    channel_decisions = []
    summary = KitGenerationSummary(kit=output_name, instruments=len(kit['instruments']))
    used_instrument_keys = {}

    for inst in kit['instruments']:
        inst_name = inst['name']
        parsed = parsed_map[inst_name]
        inst_type = (parsed['type'] or inst_name).lower()
        inst_type = INSTRUMENT_TYPE_ALIASES.get(inst_type, inst_type)
        size_label = slot_by_path.get(str(inst['path']), 'X1')
        instrument_default = inst_type + (f"-{size_label}" if size_label else '')
        articulation_with_param = resolve_articulation(parsed, inst_type)
        articulation_default, param_default = split_articulation_param(articulation_with_param)
        instrument_key = safe_var_token(inst_name)
        if instrument_key in used_instrument_keys and used_instrument_keys[instrument_key] != inst_name:
            instrument_key = instrument_key + '_' + str(len(used_instrument_keys) + 1)
        used_instrument_keys[instrument_key] = inst_name

        inst_file = inst.get('path')
        if not inst_file or not Path(inst_file).exists():
            summary.warnings.append(f"Instrument XML missing: {inst_file}")
            continue

        samples = parse_instrument_xml(inst_file)
        summary.samples += len(samples)
        max_power = max((sample['power'] for sample in samples), default=0.0)
        velocity_counters = defaultdict(int)

        for sample_idx, sample in enumerate(samples):
            if not sample['audiofiles']:
                summary.warnings.append(f"No audiofiles: {inst_name}/{sample.get('name')}")
                continue

            hihat_variant = parsed.get('hihat_variant') if parsed else None
            velocity = infer_velocity(sample['power'], max_power, sample['name'], sample_idx, len(samples), inst_type=inst_type, hihat_variant=hihat_variant)
            variant_count = velocity_counters[velocity]
            variant = assign_variant(variant_count)
            velocity_counters[velocity] += 1

            decisions = build_channel_decisions(Path(inst_file), sample, inst_type, inst_name, source_root)
            channel_decisions.extend([{
                'kit': output_name,
                'source_instrument': inst_name,
                'sample': sample['name'],
                **asdict(decision)
            } for decision in decisions])

            groups = group_channel_decisions(decisions, include_disabled=True)
            if not groups:
                plans.append(OutputFilePlan(
                    kit=output_name,
                    source_instrument=inst_name,
                    instrument_key=instrument_key,
                    sample_name=sample['name'],
                    sample_index=sample_idx,
                    power=sample['power'],
                    velocity=velocity,
                    variant=variant,
                    source_xml=rel_to_root(Path(inst_file), source_root),
                    instrument_default=instrument_default,
                    articulation_default=articulation_default,
                    param_default=param_default,
                    group_key='NO_MIC',
                    group_label='No kept mic',
                    mic_default='',
                    keep_var=f"__KEEP_{instrument_key}_NO_MIC",
                    mic_var=f"__MIC_{instrument_key}_NO_MIC",
                    source_files=[],
                    source_channels=[],
                    source_channel_indices=[],
                    target_relative='',
                    filename_template='',
                    default_keep=False,
                    quarantined=True,
                    quarantine_reasons=['no kept useful mic channels'],
                ))
                continue

            for group_key, group_label, mic_code, group in groups:
                default_keep = all(decision.keep for decision in group)
                group_token = safe_var_token(group_key)
                keep_var = f"__KEEP_{instrument_key}_{group_token}"
                mic_var = f"__MIC_{instrument_key}_{group_token}"
                filename_template = build_filename_template(f"__INS_{instrument_key}", f"__ART_{instrument_key}", velocity, variant, mic_var, audio_format)
                default_filename = build_default_filename(instrument_default, articulation_default, velocity, variant, mic_code, audio_format)
                target_relative = str(Path(output_name) / instrument_default / (articulation_default + param_default) / default_filename).replace('/', '\\')
                plan = OutputFilePlan(
                    kit=output_name,
                    source_instrument=inst_name,
                    instrument_key=instrument_key,
                    sample_name=sample['name'],
                    sample_index=sample_idx,
                    power=sample['power'],
                    velocity=velocity,
                    variant=variant,
                    source_xml=rel_to_root(Path(inst_file), source_root),
                    instrument_default=instrument_default,
                    articulation_default=articulation_default,
                    param_default=param_default,
                    group_key=group_token,
                    group_label=group_label,
                    mic_default=mic_code,
                    keep_var=keep_var,
                    mic_var=mic_var,
                    source_files=[decision.source_file for decision in group],
                    source_channels=[decision.channel_name for decision in group],
                    source_channel_indices=[decision.source_channel_index for decision in group],
                    target_relative=target_relative,
                    filename_template=filename_template,
                    default_keep=default_keep,
                )
                default_ok, default_error = parse_filename_like_kitlib(default_filename)
                if not default_ok:
                    plan.quarantined = True
                    plan.quarantine_reasons.append(default_error)
                if inst_type not in KNOWN_INSTRUMENT_TYPES:
                    plan.quarantined = True
                    plan.quarantine_reasons.append(f"unknown instrument type: {inst_type}")
                if variant_count >= 26:
                    plan.quarantined = True
                    plan.quarantine_reasons.append('round-robin letter overflow')
                for source_file in plan.source_files:
                    if not (source_root / source_file).exists():
                        plan.quarantined = True
                        plan.quarantine_reasons.append(f"missing source: {source_file}")
                        summary.missing_sources += 1
                plans.append(plan)

    target_counts = defaultdict(list)
    for plan in plans:
        if plan.target_relative:
            target_counts[plan.target_relative.lower()].append(plan)
    for duplicates in target_counts.values():
        if len(duplicates) <= 1:
            continue
        disabled_duplicates = [plan for plan in duplicates if not plan.quarantined and not plan.default_keep]
        if disabled_duplicates:
            for plan in disabled_duplicates:
                if 'disabled duplicate target if enabled' not in plan.quarantine_reasons:
                    plan.quarantine_reasons.append('disabled duplicate target if enabled')
        clean_duplicates = [plan for plan in duplicates if not plan.quarantined and plan.default_keep]
        if len(clean_duplicates) <= 1:
            continue
        for plan in clean_duplicates:
            plan.quarantined = True
            if 'duplicate target path' not in plan.quarantine_reasons:
                plan.quarantine_reasons.append('duplicate target path')

    summary.channel_decisions = len(channel_decisions)
    summary.output_plans = len(plans)
    summary.clean_outputs = sum(1 for plan in plans if not plan.quarantined and plan.default_keep)
    summary.quarantined_outputs = sum(1 for plan in plans if plan.quarantined)
    return kit, plans, channel_decisions, summary


def write_text_if_allowed(path: Path, text: str, overwrite: bool = True) -> bool:
    if path.exists() and not overwrite:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8', newline='\n')
    return True

def format_group_label_with_indices(label: str, source_channel_indices: list) -> str:
    if not source_channel_indices:
        return label
    labels = label.split(' + ')
    if len(labels) == len(source_channel_indices):
        return ' + '.join(f'{name}[srcch{source_channel_indices[idx]}]' for idx, name in enumerate(labels))
    return label + ' [' + ','.join(f'srcch{idx}' for idx in source_channel_indices) + ']'

def collect_instrument_defaults(plans: list) -> dict:
    result = {}
    for plan in plans:
        if plan.instrument_key not in result:
            result[plan.instrument_key] = {
                'source_instrument': plan.source_instrument,
                'instrument': plan.instrument_default,
                'articulation': plan.articulation_default,
                'param': plan.param_default,
                'groups': {},
            }
        if plan.group_key and plan.mic_default:
            existing = result[plan.instrument_key]['groups'].get(plan.group_key)
            group_keep = plan.default_keep or (existing is not None and existing.get('keep', False))
            result[plan.instrument_key]['groups'][plan.group_key] = {
                'label': format_group_label_with_indices(plan.group_label, plan.source_channel_indices),
                'mic': plan.mic_default,
                'keep': group_keep,
                'keep_var': plan.keep_var,
                'mic_var': plan.mic_var,
            }
    return result


def python_module_name(name: str) -> str:
    module_name = re.sub(r'[^A-Za-z0-9_]+', '_', name).strip('_') or 'kit'
    if module_name[0].isdigit():
        module_name = 'kit_' + module_name
    return module_name


def python_repr(value) -> str:
    return repr(value)


def generate_settings_py(source_root: Path, output_dir: Path, tmp_root: str, ffmpeg: str, audio_format: str) -> str:
    source_root_text = os.path.abspath(source_root)
    output_dir_text = os.path.abspath(output_dir)
    lines = [
        'from pathlib import Path',
        'import os',
        '',
        '# Shared DrumGizmo -> Karatala conversion settings.',
        '# Edit these before running generated convert_*.py files.',
        f'SRC_ROOT = Path(r{python_repr(source_root_text)})',
        f'OUT_ROOT = Path(r{python_repr(output_dir_text)})',
        f'TMP_ROOT = Path(os.path.expandvars(r{python_repr(tmp_root)}))',
        f'FFMPEG = r{python_repr(ffmpeg)}',
        f'AUDIO_FORMAT = {python_repr(audio_format)}',
        '',
    ]
    return '\n'.join(lines)


def generate_runtime_py() -> str:
    return r'''import argparse
import csv
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Operation:
    kit: str
    source_instrument: str
    mic_group: str
    sample_name: str
    source_xml: str
    power: float
    velocity: int
    round_robin: str
    group_label: str
    sources: list
    output_channels: str
    default_mic_code: str
    default_keep: bool
    quarantined: bool
    quarantine_reasons: list


def _relative_path(path_text: str) -> Path:
    return Path(*str(path_text).replace('\\', '/').split('/'))


def _source_path(settings, source_text: str) -> Path:
    return settings.SRC_ROOT / _relative_path(source_text)


def operation_output_path(operation: Operation, instruments: dict, microphones: dict, settings) -> Path:
    instrument_info = instruments[operation.source_instrument]
    microphone_info = microphones.get((operation.source_instrument, operation.mic_group), {})
    instrument = instrument_info['instrument']
    articulation = instrument_info['articulation']
    parameter = instrument_info.get('parameter', '')
    microphone_code = microphone_info.get('code', operation.default_mic_code)
    filename = f'{instrument}-{articulation}-{operation.velocity:03d}{operation.round_robin}-{microphone_code}.{settings.AUDIO_FORMAT}'
    return settings.OUT_ROOT / operation.kit / instrument / f'{articulation}{parameter}' / filename


def operation_target_relative(operation: Operation, instruments: dict, microphones: dict, settings) -> str:
    try:
        return str(operation_output_path(operation, instruments, microphones, settings).relative_to(settings.OUT_ROOT))
    except Exception:
        try:
            return str(operation_output_path(operation, instruments, microphones, settings))
        except Exception:
            return ''


def operation_enabled(operation: Operation, microphones: dict) -> bool:
    microphone_info = microphones.get((operation.source_instrument, operation.mic_group))
    return microphone_info is not None and microphone_info.get('keep', True)


def enabled_duplicate_targets(operations: list, instruments: dict, microphones: dict, settings) -> set:
    target_counts = {}
    for operation in operations:
        if operation.quarantined or not operation_enabled(operation, microphones):
            continue
        target = operation_target_relative(operation, instruments, microphones, settings).lower()
        if target:
            target_counts[target] = target_counts.get(target, 0) + 1
    return {target for target, count in target_counts.items() if count > 1}


def ffmpeg_command(operation: Operation, instruments: dict, microphones: dict, settings) -> list:
    output_path = operation_output_path(operation, instruments, microphones, settings)
    sources = operation.sources
    if len(sources) == 1:
        source_path = _source_path(settings, sources[0]['path'])
        channel_index = int(sources[0].get('channel', 0))
        return [settings.FFMPEG, '-y', '-i', str(source_path), '-map_channel', f'0.0.{channel_index}', '-c:a', settings.AUDIO_FORMAT, str(output_path)]
    if len(sources) == 2 and sources[0]['path'].lower() == sources[1]['path'].lower():
        source_path = _source_path(settings, sources[0]['path'])
        left_index = int(sources[0].get('channel', 0))
        right_index = int(sources[1].get('channel', 1))
        return [settings.FFMPEG, '-y', '-i', str(source_path), '-filter_complex', f'[0:a]pan=stereo|c0=c{left_index}|c1=c{right_index}[a]', '-map', '[a]', '-c:a', settings.AUDIO_FORMAT, str(output_path)]
    if len(sources) == 2:
        left_path = _source_path(settings, sources[0]['path'])
        right_path = _source_path(settings, sources[1]['path'])
        return [settings.FFMPEG, '-y', '-i', str(left_path), '-i', str(right_path), '-filter_complex', '[0:a][1:a]amerge=inputs=2[a]', '-map', '[a]', '-c:a', settings.AUDIO_FORMAT, str(output_path)]
    raise ValueError(f'Unsupported source grouping for {operation.source_instrument}/{operation.sample_name}/{operation.mic_group}')


def _cmd_quote(part) -> str:
    text = str(part)
    if text == '':
        return '""'
    if any(char.isspace() or char in '&|<>()^' for char in text):
        return '"' + text.replace('"', '\\"') + '"'
    return text


def _format_command(command: list) -> str:
    return ' '.join(_cmd_quote(part) for part in command)


def review_status(operation: Operation, microphones: dict, duplicate_targets: set, target_relative: str) -> str:
    if operation.quarantined:
        return 'QUARANTINED'
    if target_relative.lower() in duplicate_targets:
        return 'QUARANTINED'
    if not operation_enabled(operation, microphones):
        return 'SKIPPED'
    return 'READY'


def review_action(operation: Operation, microphones: dict, duplicate_targets: set, target_relative: str) -> str:
    if target_relative.lower() in duplicate_targets:
        return 'fix map; enabled rows would write the same target file'
    if operation.quarantined:
        return 'review generator warning before enabling manually'
    if not operation_enabled(operation, microphones):
        return 'optional/off by default; set keep True in map if you intentionally want this mic'
    return 'review optional; emitted by convert script'


def write_review_csv(operations: list, instruments: dict, microphones: dict, settings, review_path: Path) -> None:
    duplicate_targets = enabled_duplicate_targets(operations, instruments, microphones, settings)
    review_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        'status',
        'action',
        'reasons',
        'kit',
        'source_instrument',
        'sample_name',
        'velocity',
        'round_robin',
        'mic_group',
        'mic_code',
        'source_channels',
        'source_channel_indices',
        'source_files',
        'target_relative',
        'instrument',
        'articulation',
        'parameter',
        'source_xml',
        'power',
    ]
    with review_path.open('w', encoding='utf-8', newline='') as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        for operation in operations:
            instrument_info = instruments.get(operation.source_instrument, {})
            microphone_info = microphones.get((operation.source_instrument, operation.mic_group), {})
            target_relative = operation_target_relative(operation, instruments, microphones, settings)
            reasons = [reason for reason in operation.quarantine_reasons if reason != 'disabled duplicate target if enabled']
            if target_relative.lower() in duplicate_targets and 'enabled duplicate target path after map edits' not in reasons:
                reasons.append('enabled duplicate target path after map edits')
            writer.writerow({
                'status': review_status(operation, microphones, duplicate_targets, target_relative),
                'action': review_action(operation, microphones, duplicate_targets, target_relative),
                'reasons': '; '.join(reasons),
                'kit': operation.kit,
                'source_instrument': operation.source_instrument,
                'sample_name': operation.sample_name,
                'velocity': f'{operation.velocity:03d}',
                'round_robin': operation.round_robin,
                'mic_group': operation.mic_group,
                'mic_code': microphone_info.get('code', operation.default_mic_code),
                'source_channels': ' + '.join(source.get('name', '') for source in operation.sources),
                'source_channel_indices': ' + '.join(str(source.get('channel', '')) for source in operation.sources),
                'source_files': ' + '.join(source.get('path', '') for source in operation.sources),
                'target_relative': target_relative,
                'instrument': instrument_info.get('instrument', ''),
                'articulation': instrument_info.get('articulation', ''),
                'parameter': instrument_info.get('parameter', ''),
                'source_xml': operation.source_xml,
                'power': f'{operation.power:.12g}',
            })


def run_operations(operations: list, instruments: dict, microphones: dict, settings, kit_name: str, review_path: Path) -> int:
    parser = argparse.ArgumentParser(description=f'Run generated DrumGizmo -> Karatala conversion for {kit_name}')
    parser.add_argument('--execute', action='store_true', help='Actually run ffmpeg. Without this, commands are only printed.')
    parser.add_argument('--write-review', action='store_true', help='Rewrite review CSV using the current edited map_*.py values.')
    parser.add_argument('--only-instrument', default='', help='Run only one source instrument name from map_*.py.')
    parser.add_argument('--only-mic', default='', help='Run only one mic group key, for example AMB, OH, DIRECT.')
    parser.add_argument('--limit', type=int, default=0, help='Stop after this many kept operations.')
    args = parser.parse_args()

    if args.write_review:
        write_review_csv(operations, instruments, microphones, settings, review_path)
        print(f'wrote {review_path}')
        return 0

    duplicate_targets = enabled_duplicate_targets(operations, instruments, microphones, settings)
    selected_count = 0
    skipped_count = 0
    for operation in operations:
        if args.only_instrument and operation.source_instrument != args.only_instrument:
            continue
        if args.only_mic and operation.mic_group != args.only_mic:
            continue

        microphone_info = microphones.get((operation.source_instrument, operation.mic_group))
        if operation.quarantined or not microphone_info or not microphone_info.get('keep', True):
            skipped_count += 1
            continue

        output_path = operation_output_path(operation, instruments, microphones, settings)
        target_relative = operation_target_relative(operation, instruments, microphones, settings).lower()
        if target_relative in duplicate_targets:
            print(f'skip duplicate target after map edits: {output_path}')
            skipped_count += 1
            continue

        command = ffmpeg_command(operation, instruments, microphones, settings)
        print(_format_command(command))
        selected_count += 1

        if args.execute:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(command, check=True)

        if args.limit and selected_count >= args.limit:
            break

    mode = 'executed' if args.execute else 'planned'
    print(f'{kit_name}: {mode} {selected_count} kept operation(s), skipped {skipped_count} disabled operation(s).')
    return 0
'''


def generate_map_py(kit_dir: Path, plans: list) -> str:
    defaults = collect_instrument_defaults(plans)
    lines = [
        f'# Auto-generated review map for {kit_dir.name}.',
        '# Edit values here, then run the corresponding convert_*.py script.',
        '',
        'INSTRUMENTS = {',
    ]
    for instrument_key, info in defaults.items():
        lines.extend([
            # f'    # {info["source_instrument"]} -> {info["instrument"]}\\{info["articulation"]}{info["param"]}',
            f'    {python_repr(info["source_instrument"])}: {{',
												f'        "instrument": {python_repr(info["instrument"])},',
            f'        "articulation": {python_repr(info["articulation"])},',
												f'        "parameter": {python_repr(info["param"])},',
            '    },',
        ])
    lines.extend([
        '}',
        '',
        'MICS = {',
    ])
    for _, info in defaults.items():
        lines.append(f'    # Microphone groups for {info["source_instrument"]}.')
        for group_key, group in sorted(info['groups'].items()):
            comment_prefix = '# ' if not group['keep'] else ''
            lines.extend([
                f'    {comment_prefix}({python_repr(info["source_instrument"])}, {python_repr(group_key)}): {{"keep": {group["keep"]}, "code": {python_repr(group["mic"])} }},			# {group["label"]}',
            ])
        lines.append('')
    lines.extend([
        '}',
        '',
    ])
    return '\n'.join(lines)


def generate_convert_py(kit_dir: Path, plans: list) -> str:
    module_name = python_module_name(kit_dir.name)
    map_module = f'map_{module_name}'
    safe_kit = re.sub(r'[^A-Za-z0-9_.-]+', '_', kit_dir.name).strip('_') or 'kit'
    lines = [
        'from pathlib import Path',
        'import sys',
        '',
        'SCRIPT_DIR = Path(__file__).resolve().parent',
        'OUTPUT_ROOT = SCRIPT_DIR.parent',
        'if str(OUTPUT_ROOT) not in sys.path:',
        '    sys.path.insert(0, str(OUTPUT_ROOT))',
        '',
        'from _common import drumgizmo_settings as settings',
        'from _common.drumgizmo_runtime import Operation, run_operations',
        f'from {map_module} import INSTRUMENTS, MICS',
        '',
        f'KIT_NAME = {python_repr(kit_dir.name)}',
        f'REVIEW_PATH = SCRIPT_DIR / "_audit" / {python_repr("review-" + safe_kit + ".csv")}',
        '',
        'OPERATIONS = [',
    ]
    for plan in plans:
        lines.extend([
            '    Operation(',
            f'        kit={python_repr(plan.kit)},',
            f'        source_instrument={python_repr(plan.source_instrument)},',
            f'        mic_group={python_repr(plan.group_key)},',
            f'        sample_name={python_repr(plan.sample_name)},',
            f'        source_xml={python_repr(plan.source_xml)},',
            f'        power={plan.power:.12g},',
            f'        velocity={plan.velocity},',
            f'        round_robin={python_repr(plan.variant)},',
            f'        group_label={python_repr(plan.group_label)},',
            '        sources=[',
        ])
        for source_file, channel_name, channel_index in zip(plan.source_files, plan.source_channels, plan.source_channel_indices):
            lines.append(f'            {{"path": {python_repr(source_file)}, "channel": {channel_index}, "name": {python_repr(channel_name)}}},')
        lines.extend([
            '        ],',
            f'        output_channels={python_repr("stereo" if len(plan.source_files) == 2 else "mono")},',
            f'        default_mic_code={python_repr(plan.mic_default)},',
            f'        default_keep={plan.default_keep},',
            f'        quarantined={plan.quarantined},',
            f'        quarantine_reasons={python_repr(plan.quarantine_reasons)},',
            '    ),',
            '',
        ])
    lines.extend([
        ']',
        '',
        '',
        "if __name__ == '__main__':",
        '    raise SystemExit(run_operations(OPERATIONS, INSTRUMENTS, MICS, settings, KIT_NAME, REVIEW_PATH))',
        '',
    ])
    return '\n'.join(lines)


def review_action_for_plan(plan: OutputFilePlan) -> str:
    reasons = set(plan.quarantine_reasons)
    if not plan.quarantined and not plan.default_keep:
        if 'disabled duplicate target if enabled' in reasons:
            return 'optional/off by default; change mic code or enable only one row before setting keep True'
        return 'optional bleed/off by default; set keep True in map if you intentionally want this mic'
    if not plan.quarantined:
        return 'review optional; emitted in convert script'
    if 'duplicate target path' in reasons:
        return 'fix mic code/group or articulation so target filename becomes unique'
    if any(reason.startswith('unknown instrument type:') for reason in reasons):
        return 'edit generated map instrument name or add script alias if this source type is real'
    if any(reason.startswith('missing source:') for reason in reasons):
        return 'check source XML/file path; do not run until source exists'
    if 'no kept useful mic channels' in reasons:
        return 'usually ignore; no useful mic was found for this source sample'
    if any(reason.startswith('invalid mic token') for reason in reasons):
        return 'fix mic code in generated map; valid examples: dtm, dbm, dim, dom, ons, rfs'
    if 'round-robin letter overflow' in reasons:
        return 'split velocity layer or adjust variant naming manually'
    return 'review manually before adding to conversion script'


def review_status_for_plan(plan: OutputFilePlan) -> str:
    if plan.quarantined:
        return 'QUARANTINED'
    if not plan.default_keep:
        return 'SKIPPED'
    return 'READY'


def write_audit_files(kit_dir: Path, output_dir: Path, audit_dir: Path, plans: list, channel_decisions: list, summary: KitGenerationSummary) -> list:
    safe_kit = re.sub(r'[^A-Za-z0-9_.-]+', '_', kit_dir.name).strip('_') or 'kit'
    audit_dir.mkdir(parents=True, exist_ok=True)
    written = []

    review_csv = audit_dir / f'review-{safe_kit}.csv'
    with review_csv.open('w', encoding='utf-8', newline='') as csv_file:
        fieldnames = [
            'status',
            'action',
            'reasons',
            'kit',
            'source_instrument',
            'sample_name',
            'velocity',
            'round_robin',
            'mic_group',
            'mic_code',
            'source_channels',
            'source_channel_indices',
            'source_files',
            'target_relative',
            'instrument',
            'articulation',
            'parameter',
            'source_xml',
            'power',
        ]
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        for plan in plans:
            writer.writerow({
                'status': review_status_for_plan(plan),
                'action': review_action_for_plan(plan),
                'reasons': '; '.join(plan.quarantine_reasons),
                'kit': plan.kit,
                'source_instrument': plan.source_instrument,
                'sample_name': plan.sample_name,
                'velocity': f'{plan.velocity:03d}',
                'round_robin': plan.variant,
                'mic_group': plan.group_key,
                'mic_code': plan.mic_default,
                'source_channels': ' + '.join(plan.source_channels),
                'source_channel_indices': ' + '.join(str(channel_index) for channel_index in plan.source_channel_indices),
                'source_files': ' + '.join(plan.source_files),
                'target_relative': plan.target_relative,
                'instrument': plan.instrument_default,
                'articulation': plan.articulation_default,
                'parameter': plan.param_default,
                'source_xml': plan.source_xml,
                'power': f'{plan.power:.12g}',
            })
    written.append(str(review_csv))

    summary_md = audit_dir / f'summary-{safe_kit}.md'
    skipped_outputs = sum(1 for plan in plans if not plan.quarantined and not plan.default_keep)
    summary_lines = [
        f'# DrumGizmo Migration Summary: {kit_dir.name}',
        '',
        f'- Instruments: {summary.instruments}',
        f'- Samples: {summary.samples}',
        f'- Channel decisions: {summary.channel_decisions}',
        f'- Output plans: {summary.output_plans}',
        f'- Ready outputs enabled by default: {summary.clean_outputs}',
        f'- Skipped optional outputs in convert script: {skipped_outputs}',
        f'- Quarantined outputs: {summary.quarantined_outputs}',
        f'- Missing sources: {summary.missing_sources}',
        f'- ffmpeg on PATH: {"yes" if shutil.which("ffmpeg") else "no"}',
        '',
        '## Review Files',
        '',
        f'- `review-{safe_kit}.csv`: main review file. Use `status=READY`, `SKIPPED`, or `QUARANTINED`.',
        f'- `../map_{python_module_name(kit_dir.name)}.py`: edit instrument/articulation/mic keep/code values here.',
        f'- `../convert_{python_module_name(kit_dir.name)}.py`: rewrite review CSV, dry-run, or execute rows after map review.',
        '',
        '## Warnings',
        '',
    ]
    if summary.warnings:
        summary_lines.extend(f'- {warning}' for warning in summary.warnings)
    else:
        summary_lines.append('- None')
    summary_lines.extend([
        '',
        '## Next Actions',
        '',
        '1. Open the review CSV and inspect `status=QUARANTINED` and any `SKIPPED` rows you may want to promote.',
        '2. Fix mapping mistakes or flip skipped mic groups in the map Python file.',
        '3. Run the convert script with `--write-review` and inspect the refreshed CSV.',
        '4. Run the convert script without `--execute` to inspect commands.',
        '5. Run the convert script with `--execute --limit 5` for a tiny real conversion check.',
        '',
    ])
    summary_md.write_text('\n'.join(summary_lines), encoding='utf-8', newline='\n')
    written.append(str(summary_md))
    return written


def process_kit_v2(kit_dir: Path, output_dir: Path, source_root: Path, args) -> KitGenerationSummary:
    _, plans, channel_decisions, summary = build_output_plans(kit_dir, source_root, args.audio_format)
    module_name = python_module_name(kit_dir.name)
    kit_output_dir = output_dir / kit_dir.name
    audit_dir = Path(args.audit_dir) if args.audit_dir and len(args.kits_to_process) == 1 else kit_output_dir / '_audit'
    common_dir = output_dir / '_common'

    settings_path = common_dir / 'drumgizmo_settings.py'
    runtime_path = common_dir / 'drumgizmo_runtime.py'
    map_path = kit_output_dir / f'map_{module_name}.py'
    convert_path = kit_output_dir / f'convert_{module_name}.py'

    write_text_if_allowed(settings_path, generate_settings_py(source_root, output_dir, args.tmp_root, args.ffmpeg, args.audio_format), overwrite=True)
    write_text_if_allowed(runtime_path, generate_runtime_py(), overwrite=True)
    map_written = write_text_if_allowed(map_path, generate_map_py(kit_dir, plans), overwrite=args.overwrite_map)
    write_text_if_allowed(convert_path, generate_convert_py(kit_dir, plans), overwrite=True)
    summary.written_files.extend([str(settings_path), str(runtime_path), str(convert_path)])
    if map_written:
        summary.written_files.append(str(map_path))
    else:
        summary.warnings.append(f"Map file exists and was not overwritten: {map_path}")

    summary.written_files.extend(write_audit_files(kit_dir, output_dir, audit_dir, plans, channel_decisions, summary))
    if args.write_review:
        subprocess.run([sys.executable, str(convert_path), '--write-review'], check=True)
    print(
        f"{kit_dir.name}: instruments={summary.instruments} samples={summary.samples} "
        f"clean_outputs={summary.clean_outputs} quarantined={summary.quarantined_outputs}"
    )
    return summary


def main():
    parser = argparse.ArgumentParser(description='Generate DrumGizmo -> Karatala audit reports and Python conversion scripts')
    parser.add_argument('todo_path', nargs='?', default='todo', help='Path to one DrumGizmo kit or a root containing kit folders')
    parser.add_argument('--output-dir', default='instruments', help='Output directory for generated scripts and planned library output')
    parser.add_argument('--audit-dir', default='', help='Override audit output directory for single-kit runs')
    parser.add_argument('--tmp-root', default=r'%TEMP%\karatala-drumgizmo', help='Conversion temp root used by generated settings')
    parser.add_argument('--ffmpeg', default='ffmpeg', help='ffmpeg executable/path to place in generated settings')
    parser.add_argument('--audio-format', default='flac', choices=['flac'], help='Generated audio format')
    parser.add_argument('--overwrite-map', action='store_true', help='Overwrite existing map_<kit>.py files')
    parser.add_argument('--write-review', action='store_true', help='After generating scripts, refresh review CSVs using existing map_<kit>.py files')
    args = parser.parse_args()

    todo = Path(args.todo_path)
    if not todo.exists():
        print(f"Path not found: {todo}")
        sys.exit(1)

    kit_xml = todo / f"{todo.name}.xml"
    is_single_kit = kit_xml.exists() or (list(todo.glob('*.xml')) and find_all_instruments(todo))
    kits = [todo] if is_single_kit else find_kits(todo)
    args.kits_to_process = kits
    source_root = todo.parent if is_single_kit else todo
    output_dir = Path(args.output_dir)

    totals = KitGenerationSummary(kit='TOTAL')
    for kit_dir in kits:
        kit_summary = process_kit_v2(kit_dir, output_dir, source_root, args)
        totals.instruments += kit_summary.instruments
        totals.samples += kit_summary.samples
        totals.channel_decisions += kit_summary.channel_decisions
        totals.output_plans += kit_summary.output_plans
        totals.clean_outputs += kit_summary.clean_outputs
        totals.quarantined_outputs += kit_summary.quarantined_outputs
        totals.missing_sources += kit_summary.missing_sources

    print(
        f"Total: kits={len(kits)} instruments={totals.instruments} samples={totals.samples} "
        f"clean_outputs={totals.clean_outputs} quarantined={totals.quarantined_outputs}"
    )


if __name__ == '__main__':
    main()
