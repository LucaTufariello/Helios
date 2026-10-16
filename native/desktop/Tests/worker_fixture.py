"""Generated test protocol only; never deployed as an application worker."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

parser = argparse.ArgumentParser()
parser.add_argument('operation')
parser.add_argument('--input')
parser.add_argument('--output')
args = parser.parse_args()
behavior = Path(args.input).read_text()
output = Path(args.output)
output.mkdir()

def event(state, stage, progress=None):
    print(json.dumps(dict(state=state, stage=stage, progress=progress)), flush=True)

event('validating', 'Generated worker validation', 0.1)
if behavior == 'oversized':
    print('X' * (128 * 1024 + 1), flush=True)
    time.sleep(30)
elif behavior in ('child', 'child-cooperative'):
    (output / 'preview.png').write_bytes(b'generated partial image')
    (output / 'result.json').write_text(json.dumps(dict(state='processing')))
    (output / 'diagnostics.log').write_text('Generated cancellation diagnostic')
    if behavior == 'child':
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    child = subprocess.Popen([sys.executable, '-I', '-c',
        'import pathlib,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); '
        f'p=pathlib.Path({str(output / "heartbeat")!r}); '
        '\nwhile True: p.write_text(str(time.monotonic())); time.sleep(.03)'])
    (output / 'child.pid').write_text(str(child.pid))
    while True:
        time.sleep(0.1)
elif behavior == 'sleep':
    while True:
        time.sleep(0.1)
elif behavior == 'failed':
    event('failed', 'Generated expected failure')
    raise SystemExit(7)
else:
    event('processing', 'Generated operation with indeterminate progress')
    event('exporting', 'Generated export', 0.9)
    if behavior != 'missing-result':
        lock_stat = Path('.run.lock').stat()
        inherited_lock = False
        for fd in range(3, 256):
            try:
                opened = os.fstat(fd)
                inherited_lock |= opened.st_dev == lock_stat.st_dev and opened.st_ino == lock_stat.st_ino
            except OSError:
                pass
        (output / 'result.json').write_text(json.dumps(dict(state='completed', originalInput=args.input,
                                                          inheritedJobLock=inherited_lock)))
    event('completed', 'Generated fixture complete', 1)
