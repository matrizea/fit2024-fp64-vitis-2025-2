"""Require current synthesis and consecutive-transaction RTL proof for II=1."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent

def main():
    rows = json.loads((ROOT / 'results/summary.json').read_text(encoding='utf-8'))
    scalar = [r for r in rows if not r['case'].startswith('gj_')]
    for row in scalar:
        assert row.get('source_matches_current') and row.get('synth') == 'PASS', row['case']
        assert row.get('interval_min_cycles') == row.get('interval_max_cycles') == '1', row
    cases = []
    for op in ('add', 'sub', 'mul'):
        for mode in ('native', 'custom'):
            name = f'{op}_{mode}'
            row = next(r for r in scalar if r['case'] == name)
            assert row.get('cosim_rtl_status') == 'Pass', row
            assert row.get('cosim_interval_min') == row.get('cosim_interval_avg') == row.get('cosim_interval_max') == '1', row
            report = ROOT / 'build' / name / 'hls/sim/report/verilog/result.transaction.rpt'
            transactions = [(int(a), int(b), None if c == 'x' else int(c)) for a, b, c in re.findall(r'transaction\s+(\d+):\s+(\d+)\s+(\d+|x)', report.read_text(encoding='utf-8'))]
            count = 1168 if mode == 'custom' else 16
            assert len(transactions) == count, (name, len(transactions))
            assert [n for n, _, _ in transactions] == list(range(count)), name
            assert all(latency == int(row['latency_max_cycles']) for _, latency, _ in transactions), name
            assert all(interval == 1 for _, _, interval in transactions[:-1]), name
            assert transactions[-1][2] is None, name
            cases.append({'case': name, 'latency_cycles': int(row['latency_max_cycles']),
                'interval_cycles': 1, 'cosim_status': 'Pass', 'transactions': count,
                'transaction_intervals': {'min': 1, 'max': 1, 'measured_gaps': count - 1,
                    'terminal_interval': 'undefined: no following transaction'},
                'LUT': int(row['LUT']), 'FF': int(row['FF']), 'DSP': int(row['DSP']),
                'estimated_period_ns': float(row['estimated_period_ns']),
                'csynth_report': f'build/{name}/hls/syn/report/{name}_csynth.xml',
                'cosim_report': f'build/{name}/hls/sim/report/{name}_cosim.rpt',
                'transactions_report': str(report.relative_to(ROOT)).replace('\\', '/')})
    header = ROOT / 'src/fp64.hpp'
    baseline = ROOT / 'history/before_ii1/src/fp64.hpp'
    assert header.read_bytes() == baseline.read_bytes(), 'Arithmetic datapath changed; run arithmetic suite again.'
    output = {'claim': 'All 17 scalar synthesis cases have interval 1; native/custom scalar tops additionally pass consecutive-input RTL Cosim.',
        'original_implementation_interval_cycles_user_confirmed': 1,
        'scalar_synthesis_cases': len(scalar), 'verified_rtl_cases': cases,
        'change': 'PIPELINE II=1 on scalar entry points; Gauss-Jordan loops and arithmetic header are unchanged.',
        'fp64_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
        'arithmetic_header_unchanged_from_before_ii1': True,
        'limits': 'HLS estimates and generated-RTL simulation only; no post-route or board measurement.'}
    (ROOT / 'results/ii_verification.json').write_text(json.dumps(output, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'scalar_synthesis_cases': len(scalar), 'rtl_cases': len(cases), 'verified_transactions': sum(r['transactions'] for r in cases)}, ensure_ascii=False))

if __name__ == '__main__':
    main()
