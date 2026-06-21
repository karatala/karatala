import argparse
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


def _format_command(command: list) -> str:
    return subprocess.list2cmdline([str(part) for part in command])


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
