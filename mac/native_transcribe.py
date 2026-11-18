"""Transcribe one Dot reply locally, using the already cached Whisper model."""
import sys
from pathlib import Path

import numpy as np
import whisper

pcm = sys.stdin.buffer.read(60 * 16000 * 2)
audio = np.frombuffer(pcm, dtype='<i2').astype(np.float32) / 32768
model = whisper.load_model(str(Path.home() / '.cache/whisper/base.pt'), device='cpu')
print(model.transcribe(audio, fp16=False, language='zh', condition_on_previous_text=False)['text'].strip())
