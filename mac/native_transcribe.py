"""Transcribe one Dot reply locally, using the multilingual Whisper base model."""
import os
import sys
from pathlib import Path

import numpy as np
import whisper

pcm = sys.stdin.buffer.read(60 * 16000 * 2)
audio = np.frombuffer(pcm, dtype='<i2').astype(np.float32) / 32768
model = whisper.load_model('base', download_root=str(Path.home() / '.cache/whisper'), device='cpu')
print(model.transcribe(audio, fp16=False, language=os.environ.get('DOT_TRANSCRIPTION_LANGUAGE') or None, condition_on_previous_text=False)['text'].strip())
