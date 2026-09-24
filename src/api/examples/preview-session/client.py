"""Small local NDJSON session client; use as a Host integration reference.

Run from the project working directory:
  python3 src/api/examples/preview-session/client.py build/bin/ARCH < requests.ndjson
Prints all Core events to stdout. stderr stays separate. No parameter file writes.
"""
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time


class Client:
    def __init__(self, binary, cwd=None):
        self.process = subprocess.Popen([str(Path(binary).resolve()), '--preview-session'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, cwd=cwd)
        self.events = queue.Queue()
        def reader():
            while True:
                line = self.process.stdout.readline(9*1024*1024+1)
                if not line:
                    self.events.put(None)
                    return
                if len(line)>9*1024*1024 or not line.endswith(b'\n'):
                    self.events.put(RuntimeError('Invalid/oversize response frame'))
                    return
                try:
                    self.events.put(json.loads(line))
                except Exception as error:
                    self.events.put(error)
                    return
        self.reader = threading.Thread(target=reader, daemon=True)
        self.reader.start()
        try:
            self.ready = self.next(10)
            if self.ready['kind'] != 'preview-session-ready':
                raise RuntimeError(self.ready)
        except BaseException:
            self.close()
            raise

    def next(self, timeout):
        if timeout <= 0:
            raise TimeoutError("Preview request wall timeout")
        try:
            item = self.events.get(timeout=max(.001, timeout))
        except queue.Empty:
            raise TimeoutError('Preview request wall timeout') from None
        if isinstance(item, Exception):
            raise item
        if item is None:
            raise RuntimeError('Session ended without a complete response')
        return item

    def request(self, obj):
        # Only one outstanding request. Production Host also checks binary,
        # process generation, configRevision and source identity before display.
        timeout = 360 if obj['command'] in ('--preview', '--inspect-case') else 45
        deadline = time.monotonic()+timeout
        wire=(json.dumps(obj, ensure_ascii=False)+'\n').encode()
        if len(wire)-1>8*1024*1024:
            raise ValueError('Request exceeds 8 MiB')
        self.process.stdin.write(wire)
        self.process.stdin.flush()
        while True:
            event = self.next(deadline-time.monotonic())
            yield event
            if event['kind'] != 'preview-session-progress':
                return

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.reader.join(timeout=3)
        self.process.stdin.close()
        self.process.stdout.close()


if __name__ == '__main__':
    if len(sys.argv)!=2:
        raise SystemExit('usage: client.py /path/to/ARCH < requests.ndjson')
    client=Client(sys.argv[1])
    try:
        print(json.dumps(client.ready, ensure_ascii=False), flush=True)
        for line in sys.stdin:
            for event in client.request(json.loads(line)):
                print(json.dumps(event, ensure_ascii=False), flush=True)
    finally:
        client.close()
