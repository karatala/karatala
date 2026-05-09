#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from pathlib import Path
from converter import find_kit_xml, parse_kit_xml

kit_dir = Path('T:/install/music/eDamaru/instruments/todo/CrocellKit')
kit_xml = find_kit_xml(kit_dir)
print(f'Selected XML: {kit_xml.name}')

kit = parse_kit_xml(kit_xml)
print(f'Instruments: {len(kit["instruments"])}')
for inst in sorted(kit['instruments'], key=lambda x: x['name']):
    print(f'  - {inst["name"]}')

# Check filesystem
print('\nFolders in CrocellKit:')
for folder in sorted(kit_dir.iterdir()):
    if folder.is_dir() and not folder.name.startswith('_'):
        in_xml = any(i['name'] == folder.name for i in kit['instruments'])
        status = 'OK' if in_xml else 'SKIP'
        print(f'  [{status}] {folder.name}')

# Check other kit XMLs
print('\n--- Checking other Kit XMLs ---')
all_folders = set(f.name for f in kit_dir.iterdir() if f.is_dir() and not f.name.startswith('_'))
print(f'Total folders in filesystem: {len(all_folders)}')

for xml_candidate in ['CrocellKit_full.xml', 'CrocellKit_small.xml']:
    xml_file = kit_dir / xml_candidate
    if xml_file.exists():
        kit_alt = parse_kit_xml(xml_file)
        in_this_xml = set(i['name'] for i in kit_alt['instruments'])
        skipped_here = all_folders - in_this_xml
        print(f'\n{xml_candidate}: {len(kit_alt["instruments"])} instruments')
        if skipped_here:
            print(f'  Skipped: {", ".join(sorted(skipped_here))}')
        else:
            print(f'  --> Includes ALL folders!')

