import argparse
import csv
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


RECHECKABLE_QUARANTINE_REASONS = {'duplicate target path'}
RECHECKABLE_QUARANTINE_PREFIXES = ('unknown instrument type:', 'invalid mic token(s):', 'mic code channel count does not match ')
VALID_MIC_POSITIONS = {
    'd': 'ustbio',
    'o': 'nw',
    'r': 'cf',
}
VALID_MIC_FORMATS = 'msi'
VALID_INSTRUMENT_TYPES = {
    'percussion', 'other', 'drum', 'cymbal', 'kick', 'tom', 'octoban', 'rototom', 'timbale', 'timpani', 'conga', 'bongo', 'djembe', 'cajon', 'taiko', 'surdo', 'mridangam', 'talking_drum', 'snare', 'ride', 'hihat', 'hi_hat', 'crash', 'splash', 'china', 'bell', 'stack', 'stax', 'chopper', 'gong', 'karatala', 'cowbell', 'cowbel', 'tambourine', 'shaker', 'maracas', 'cabasa', 'guiro', 'claves', 'agogo', 'wood_block', 'woodblock', 'temple_block', 'triangle', 'vibraslap', 'castanets', 'sleigh_bells', 'whip', 'ratchet',
}


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


def _source_path(settings, source_text: str) -> Path:
    normalized_text = str(source_text).replace('\\', '/')
    source_path = Path(normalized_text)
    is_windows_absolute = len(normalized_text) >= 3 and normalized_text[0].isalpha() and normalized_text[1:3] == ':/'
    if source_path.is_absolute() or is_windows_absolute:
        return source_path
    return settings.SRC_ROOT / source_path


def is_valid_mic_token(microphone_token: str) -> bool:
    if len(microphone_token) not in (3, 4):
        return False
    if microphone_token[0] not in VALID_MIC_POSITIONS:
        return False
    if microphone_token[1] not in VALID_MIC_POSITIONS[microphone_token[0]]:
        return False
    if microphone_token[2] not in VALID_MIC_FORMATS:
        return False
    if len(microphone_token) == 4 and not microphone_token[3].isdigit():
        return False
    return True


def is_valid_mic_code(microphone_code: str) -> bool:
    microphone_tokens = str(microphone_code or '').split('_')
    return bool(microphone_tokens) and all(is_valid_mic_token(microphone_token) for microphone_token in microphone_tokens)


def mic_code_channel_count(microphone_code: str) -> int:
    microphone_tokens = str(microphone_code or '').split('_')
    if not microphone_tokens or not all(is_valid_mic_token(microphone_token) for microphone_token in microphone_tokens):
        return 0
    return sum(1 if microphone_token[2] == 'm' else 2 for microphone_token in microphone_tokens)


def operation_microphone_code(operation: Operation, microphones: dict) -> str:
    microphone_info = microphones.get((operation.source_instrument, operation.mic_group), {})
    return microphone_info.get('code', operation.default_mic_code)


def operation_hard_quarantine_reasons(operation: Operation) -> list:
    if not operation.quarantined:
        return []
    if not operation.quarantine_reasons:
        return ['unspecified generator quarantine']
    return [reason for reason in operation.quarantine_reasons if reason not in RECHECKABLE_QUARANTINE_REASONS and not reason.startswith(RECHECKABLE_QUARANTINE_PREFIXES)]


def operation_output_path(operation: Operation, instruments: dict, microphones: dict, settings) -> Path:
    instrument_info = instruments[operation.source_instrument]
    instrument = instrument_info['instrument']
    articulation = instrument_info['articulation']
    parameter = instrument_info.get('parameter', '')
    microphone_code = operation_microphone_code(operation, microphones)
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


def operation_validation_reasons(operation: Operation, instruments: dict, microphones: dict) -> list:
    if not operation_enabled(operation, microphones):
        return []
    instrument_info = instruments.get(operation.source_instrument, {})
    instrument_code = str(instrument_info.get('instrument', ''))
    instrument_type = instrument_code.split('-', 1)[0].lower()
    reasons = []
    if '-' not in instrument_code or not instrument_code.split('-', 1)[1]:
        reasons.append(f'invalid current instrument code: {instrument_code or "<empty>"}')
    if instrument_type not in VALID_INSTRUMENT_TYPES:
        reasons.append(f'invalid current instrument type: {instrument_type or "<empty>"}')
    microphone_code = operation_microphone_code(operation, microphones)
    if not is_valid_mic_code(microphone_code):
        reasons.append(f'invalid current mic code: {microphone_code}')
    elif mic_code_channel_count(microphone_code) != len(operation.sources):
        reasons.append(f'mic code channel count does not match {len(operation.sources)} source channel(s): {microphone_code}')
    expected_output_channels = 'stereo' if len(operation.sources) == 2 else 'mono'
    if operation.output_channels != expected_output_channels:
        reasons.append(f'output channel label does not match sources: {operation.output_channels}')
    return reasons


def operation_source_summary(operation: Operation) -> str:
    source_text = ' + '.join(f"{source.get('name', '?')}[srcch{source.get('channel', '?')}]" for source in operation.sources)
    return f'{operation.source_instrument} sample={operation.sample_name} mic={operation.mic_group} source={source_text}'


def enabled_duplicate_targets(operations: list, instruments: dict, microphones: dict, settings) -> dict:
    target_operations = {}
    for operation in operations:
        if not operation_enabled(operation, microphones):
            continue
        if operation_hard_quarantine_reasons(operation) or operation_validation_reasons(operation, instruments, microphones):
            continue
        target = operation_target_relative(operation, instruments, microphones, settings).lower()
        if target:
            target_operations.setdefault(target, []).append(operation)
    return {target: duplicates for target, duplicates in target_operations.items() if len(duplicates) > 1}


def ffmpeg_base_command(settings) -> list:
    return [settings.FFMPEG, '-hide_banner', '-loglevel', 'warning', '-nostats', '-y']


def ffmpeg_command(operation: Operation, instruments: dict, microphones: dict, settings) -> list:
    output_path = operation_output_path(operation, instruments, microphones, settings)
    sources = operation.sources
    command = ffmpeg_base_command(settings)
    if len(sources) == 1:
        source_path = _source_path(settings, sources[0]['path'])
        channel_index = int(sources[0].get('channel', 0))
        return command + ['-i', str(source_path), '-filter_complex', f'[0:a]pan=mono|c0=c{channel_index}[a]', '-map', '[a]', '-c:a', settings.AUDIO_FORMAT, str(output_path)]
    if len(sources) == 2 and sources[0]['path'].lower() == sources[1]['path'].lower():
        source_path = _source_path(settings, sources[0]['path'])
        left_index = int(sources[0].get('channel', 0))
        right_index = int(sources[1].get('channel', 1))
        return command + ['-i', str(source_path), '-filter_complex', f'[0:a]pan=stereo|c0=c{left_index}|c1=c{right_index}[a]', '-map', '[a]', '-c:a', settings.AUDIO_FORMAT, str(output_path)]
    if len(sources) == 2:
        left_path = _source_path(settings, sources[0]['path'])
        right_path = _source_path(settings, sources[1]['path'])
        left_index = int(sources[0].get('channel', 0))
        right_index = int(sources[1].get('channel', 0))
        return command + ['-i', str(left_path), '-i', str(right_path), '-filter_complex', f'[0:a]pan=mono|c0=c{left_index}[left];[1:a]pan=mono|c0=c{right_index}[right];[left][right]join=inputs=2:channel_layout=stereo[a]', '-map', '[a]', '-c:a', settings.AUDIO_FORMAT, str(output_path)]
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


def _mkdir_command(path: Path) -> str:
    quoted = _cmd_quote(path)
    return f'if not exist {quoted} mkdir {quoted}'


def _log(message: str) -> None:
    print(message, file=sys.stderr)


def review_status(operation: Operation, instruments: dict, microphones: dict, duplicate_targets: set, target_relative: str) -> str:
    if operation_hard_quarantine_reasons(operation):
        return 'QUARANTINED'
    if not operation_enabled(operation, microphones):
        return 'SKIPPED'
    if operation_validation_reasons(operation, instruments, microphones):
        return 'QUARANTINED'
    if target_relative.lower() in duplicate_targets:
        return 'QUARANTINED'
    return 'READY'


def review_action(operation: Operation, instruments: dict, microphones: dict, duplicate_targets: set, target_relative: str) -> str:
    if operation_hard_quarantine_reasons(operation):
        return 'review generator warning before enabling manually'
    if not operation_enabled(operation, microphones):
        return 'optional/off by default; set keep True in map if you intentionally want this mic'
    if operation_validation_reasons(operation, instruments, microphones):
        return 'assign valid current instrument and mic codes in map_*.py'
    if target_relative.lower() in duplicate_targets:
        return 'fix map; enabled rows would write the same target file'
    return 'review optional; emitted by convert script'


def write_review_csv(operations: list, instruments: dict, microphones: dict, settings, review_path: Path) -> None:
    duplicate_targets = enabled_duplicate_targets(operations, instruments, microphones, settings)
    review_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        'status',
        'action',
        'reasons',
        'duplicate_sources',
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
            target_relative = operation_target_relative(operation, instruments, microphones, settings)
            reasons = operation_hard_quarantine_reasons(operation)
            reasons.extend(operation_validation_reasons(operation, instruments, microphones))
            if target_relative.lower() in duplicate_targets and 'enabled duplicate target path after map edits' not in reasons:
                reasons.append('enabled duplicate target path after map edits')
            writer.writerow({
                'status': review_status(operation, instruments, microphones, duplicate_targets, target_relative),
                'action': review_action(operation, instruments, microphones, duplicate_targets, target_relative),
                'reasons': '; '.join(reasons),
                'duplicate_sources': ' | '.join(operation_source_summary(duplicate) for duplicate in duplicate_targets.get(target_relative.lower(), [])),
                'kit': operation.kit,
                'source_instrument': operation.source_instrument,
                'sample_name': operation.sample_name,
                'velocity': f'{operation.velocity:03d}',
                'round_robin': operation.round_robin,
                'mic_group': operation.mic_group,
                'mic_code': operation_microphone_code(operation, microphones),
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
    parser.add_argument('--bat', action='store_true', help='Print clean runnable .bat text to stdout: @echo off, mkdir commands, then ffmpeg commands. Messages go to stderr.')
    parser.add_argument('--mkdirs', action='store_true', help='In dry run, print mkdir commands before ffmpeg commands.')
    parser.add_argument('--write-review', action='store_true', help='Rewrite review CSV using the current edited map_*.py values.')
    parser.add_argument('--only-instrument', default='', help='Run only one source instrument name from map_*.py.')
    parser.add_argument('--only-mic', default='', help='Run only one mic group key, for example AMB, OH, DIRECT.')
    parser.add_argument('--limit', type=int, default=0, help='Stop after this many kept operations.')
    args = parser.parse_args()

    if args.write_review:
        write_review_csv(operations, instruments, microphones, settings, review_path)
        _log(f'wrote {review_path}')
        return 0

    duplicate_targets = enabled_duplicate_targets(operations, instruments, microphones, settings)
    selected_count = 0
    skipped_count = 0
    made_dirs = set()
    # if args.bat:
    #     print('@echo off')
    for operation in operations:
        if args.only_instrument and operation.source_instrument != args.only_instrument:
            continue
        if args.only_mic and operation.mic_group != args.only_mic:
            continue

        microphone_info = microphones.get((operation.source_instrument, operation.mic_group))
        if not microphone_info or not microphone_info.get('keep', True):
            skipped_count += 1
            continue

        hard_quarantine_reasons = operation_hard_quarantine_reasons(operation)
        if hard_quarantine_reasons:
            _log(f"skip quarantined operation: {operation_source_summary(operation)} ({'; '.join(hard_quarantine_reasons)})")
            skipped_count += 1
            continue

        validation_reasons = operation_validation_reasons(operation, instruments, microphones)
        if validation_reasons:
            _log(f"skip invalid operation: {operation_source_summary(operation)} ({'; '.join(validation_reasons)})")
            skipped_count += 1
            continue

        output_path = operation_output_path(operation, instruments, microphones, settings)
        target_relative = operation_target_relative(operation, instruments, microphones, settings).lower()
        if target_relative in duplicate_targets:
            _log(f'skip duplicate output file: {output_path}')
            _log('  More than one enabled row wants to create this same file:')
            for duplicate in duplicate_targets[target_relative]:
                _log(f'  - {operation_source_summary(duplicate)}')
            _log('  Fix: disable one row, or give one row a different mic code in map_*.py.')
            skipped_count += 1
            continue

        command = ffmpeg_command(operation, instruments, microphones, settings)
        if args.bat or args.mkdirs:
            folder_key = str(output_path.parent).lower()
            if folder_key not in made_dirs:
                print(_mkdir_command(output_path.parent))
                made_dirs.add(folder_key)
        print(_format_command(command))
        selected_count += 1

        if args.execute:
            output_path.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(command, check=True)

        if args.limit and selected_count >= args.limit:
            break

    mode = 'executed' if args.execute else 'planned'
    _log(f'{kit_name}: {mode} {selected_count} command(s), skipped {skipped_count} row(s).')
    return 0
