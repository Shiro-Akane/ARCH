"""Pilot-only wall-time observations at ARCH's existing flushed progress lines.

No production instrumentation, injected CUDA event, or physical parameter change.
Pipe/reader scheduling affects these observations. The first-step prefix includes
startup, initial I/O, backend upload AND the first step; the interior window is
post-first-step evolution, not a pure kernel timer or certified steady-state rate.
Formal speedup samples must use run_microphysics_timing.py without this wrapper.
"""
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import run_microphysics_timing as timing


class Progress:
    def __init__(self):
        self.header = False
        self.steps = []

    def observe(self, line, elapsed):
        if line.startswith('Step') and 'Time' in line and 'dt_hydro' in line:
            self.header = True
        if not self.header:
            return
        match = re.fullmatch(r'\s*(\d+)\s+([\d.eE+\-]+)\s+([\d.eE+\-]+)(?:\s+[\d.eE+\-]+){1,3}\s*', line)
        if match:
            step = int(match[1])
            if self.steps and step <= self.steps[-1]['step']:
                raise RuntimeError('nonmonotonic progress rows')
            self.steps.append(dict(step=step, physical_time=float(match[2]), elapsed_seconds=elapsed))

    def result(self, wall):
        result = dict(scope=__doc__, steps=self.steps, complete=bool(self.steps))
        if self.steps:
            first, last = self.steps[0], self.steps[-1]
            result.update(startup_io_and_first_step_seconds=first['elapsed_seconds'],
                post_first_step_evolution_seconds=last['elapsed_seconds']-first['elapsed_seconds'],
                final_output_and_exit_seconds=wall-last['elapsed_seconds'],
                evolution_steps=last['step']-first['step'])
        return result


def observed_process(command, cwd, env, directory, timeout):
    expired = False
    observer = Progress()
    errors = []
    with (directory/'arch.stdout').open('wb') as stdout, (directory/'arch.stderr').open('wb') as stderr:
        start = time.perf_counter()
        proc = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=stderr)

        def drain():
            try:
                for line in proc.stdout:
                    elapsed = time.perf_counter()-start
                    stdout.write(line)
                    observer.observe(line.decode('utf-8',errors='replace'),elapsed)
            except BaseException as error:
                errors.append(error)

        def deadline():
            nonlocal expired
            if proc.poll() is None:
                expired = True
                proc.kill()

        reader = threading.Thread(target=drain,daemon=True)
        timer = threading.Timer(timeout,deadline)
        timer.daemon = True
        reader.start()
        timer.start()
        try:
            rc = proc.wait()
            # Include stdout draining in the diagnostic process envelope so a
            # delayed observer never invents a negative output/cleanup interval.
            reader.join(timeout=5)
            if reader.is_alive():
                raise RuntimeError('progress reader did not reach EOF')
            if errors:
                raise errors[0]
        except BaseException:
            if proc.poll() is None:
                proc.kill()
            proc.wait()
            reader.join(timeout=5)
            raise
        finally:
            timer.cancel()
            proc.stdout.close()
        wall = time.perf_counter()-start
    return dict(returncode=rc,timed_out=expired,arch_wall_seconds=wall,
        progress_observation=observer.result(wall),
        progress_recipe=timing.provenance.file_identity(Path(__file__)))


def main():
    if '--pilot' not in sys.argv:
        raise RuntimeError('progress observation requires --pilot; never use for formal speedup')
    timing.timed_process = observed_process
    timing.main()


if __name__ == '__main__': main()
