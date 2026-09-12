"""Read a running original benchmark report without dumping raw evidence."""
import json
from pathlib import Path
import sys

report = json.loads(Path(sys.argv[1]).read_text())
summary = {key: report.get(key) for key in ('status', 'error', 'statistics')}
summary['source_identity'] = report.get('identity_before', {}).get('source')
summary['lanes'] = [{key: lane.get(key) for key in (
    'blocks_per_axis', 'backend', 'phase', 'repeat', 'arch_wall_seconds',
    'measurement_status', 'numerical_status', 'initial_leaf_count',
    'final_leaf_count')} for lane in report.get('lanes', [])[-4:]]
summary['lane_count'] = len(report.get('lanes', []))
summary['comparison_count'] = len(report.get('comparisons', []))
summary['comparisons_passed'] = all(item.get('workload_aligned') and
    item['field_comparison']['passed'] for item in report.get('comparisons', []))
print(json.dumps(summary, indent=2))
