from pathlib import Path
import os

# Shared DrumGizmo -> Karatala conversion settings.
# Edit these before running generated convert_*.py files.
SRC_ROOT = Path(r'T:\\source\\repos\\karatala\\assets\\drumgizmo')
OUT_ROOT = Path(r'T:\\source\\repos\\karatala\\instruments_py')
TMP_ROOT = Path(os.path.expandvars(r'%TEMP%\\karatala-drumgizmo'))
FFMPEG = r'ffmpeg'
AUDIO_FORMAT = 'flac'
