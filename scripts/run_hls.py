"""Reproducible Vitis HLS 2025.2 sweep. Standard-library Python only."""
from __future__ import annotations
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).absolute().parent.parent
CASES = {}
for op in ('add', 'sub', 'mul'):
    for mode in ('native', 'custom'):
        name = f'{op}_{mode}'
        CASES[name] = {'top': name, 'defines': []}
    impls = ('fabric', 'dsp', 'meddsp', 'fulldsp', 'maxdsp') if op == 'mul' else ('fabric', 'dsp', 'fulldsp')
    for impl in impls:
        CASES[f'{op}_bind_{impl}'] = {'top': f'{op}_bind', 'defines': [f'REPRO_{"MUL" if op == "mul" else "ADD"}_IMPL={impl}']}
for name in ('native', 'bind_mul', 'bind_sub', 'bind_both', 'custom_mul', 'custom_sub', 'custom_both', 'inline_mul', 'inline_sub', 'inline_both'):
    CASES[f'gj_{name}'] = {'top': f'gj_{name}', 'defines': []}


def source_hashes():
    return {str(p.relative_to(ROOT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
            for d in ('src', 'tests', 'scripts') for p in sorted((ROOT / d).glob('*')) if p.is_file()}


def run_case(name, args):
    case = CASES[name]
    top = case['top']
    folder = args.build_root / name
    folder.mkdir(parents=True, exist_ok=True)
    flags = f'-std=c++14 -ffp-contract=off -I{(ROOT / "src").as_posix()} ' + ' '.join('-D' + d for d in case['defines'])
    tbflags = flags + f' -DREPRO_TOP={top} -DREPRO_TEST_GJ={int(top.startswith("gj_"))}'
    if args.bit_tests:
        tbfile = ROOT / 'tests/tb_fp64.cpp'
    else:
        tbfile = ROOT / 'tests/tb_kernels.cpp'
    config = folder / 'hls_config.cfg'
    config.write_text('\n'.join([
        f'part={args.part}', '[hls]', 'flow_target=vivado', f'syn.top={top}',
        f'syn.file={(ROOT / "src/tops.cpp").as_posix()}', f'syn.cflags={flags}',
        f'tb.file={tbfile.as_posix()}', f'tb.cflags={tbflags}',
        'clock=10ns', 'clock_uncertainty=27%',
        'syn.compile.pipeline_loops=0', 'syn.compile.unsafe_math_optimizations=false',
        'syn.compile.no_signed_zeros=false',
        'cosim.rtl=verilog', 'cosim.tool=xsim',
        ''
    ]), encoding='utf-8')
    statefile = folder / ('state_bit_tests.json' if args.bit_tests else 'state.json')
    state = json.loads(statefile.read_text()) if statefile.exists() else {}
    # Provenance follows the source inputs of this component, independent of
    # report scripts or unrelated testbenches added during an experiment.
    inputs = sorted((ROOT / 'src').glob('*')) + [tbfile]
    fingerprint = hashlib.sha256(b''.join(p.read_bytes() for p in inputs if p.is_file()) + config.read_bytes()).hexdigest()
    if state.get('fingerprint') != fingerprint:
        state = {'case': name, 'top': top, 'fingerprint': fingerprint, 'stages': {}}
    for stage in args.stages:
        if stage in ('cosim', 'impl', 'package') and state['stages'].get('synth', {}).get('returncode') != 0:
            print(f'ERROR {name}: {stage} requires successful synth with the current sources/config; run --stages csim synth {stage}', flush=True)
            return False
        if args.resume and state['stages'].get(stage, {}).get('returncode') == 0:
            print(f'SKIP {name} {stage} (previous success)', flush=True)
            continue
        if stage == 'synth':
            for dependent in ('cosim', 'impl', 'package'):
                state['stages'].pop(dependent, None)
        state['stages'].pop(stage, None)
        statefile.write_text(json.dumps(state, indent=2), encoding='utf-8')
        executable = args.tool_root / 'Vitis/bin' / ('v++.bat' if stage == 'synth' else 'vitis-run.bat')
        cmd = [str(executable)] + (['-c', '--mode', 'hls'] if stage == 'synth' else ['--mode', 'hls', '--' + stage])
        cmd += ['--config', str(config), '--work_dir', str(folder)]
        print(f'START {name} {stage}', flush=True)
        log = folder / f'{stage}{"_bits" if args.bit_tests else ""}.log'
        started = time.time()
        with log.open('w', encoding='utf-8') as output:
            process = subprocess.run(cmd, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT)
        state['stages'][stage] = {'returncode': process.returncode, 'seconds': round(time.time()-started, 2),
                                  'command': cmd, 'log': str(log), 'completed_at': time.strftime('%Y-%m-%dT%H:%M:%S%z')}
        statefile.write_text(json.dumps(state, indent=2), encoding='utf-8')
        print(f'END {name} {stage}: rc={process.returncode}, {time.time()-started:.1f}s', flush=True)
        if process.returncode:
            print(log.read_text(encoding='utf-8', errors='replace')[-5000:], flush=True)
            return False
    return True


def summarize(args):
    rows = []
    for name, case in CASES.items():
        folder = args.build_root / name
        xml = folder / f'hls/syn/report/{case["top"]}_csynth.xml'
        statefile = folder / 'state.json'
        state = json.loads(statefile.read_text()) if statefile.exists() else {}
        row = {'case': name, 'top': case['top']}
        config = folder / 'hls_config.cfg'
        inputs = sorted((ROOT / 'src').glob('*')) + [ROOT / 'tests/tb_kernels.cpp']
        current_fingerprint = hashlib.sha256(b''.join(p.read_bytes() for p in inputs if p.is_file()) + config.read_bytes()).hexdigest() if config.exists() else None
        row['source_matches_current'] = state.get('fingerprint') == current_fingerprint if state else False
        row['source_fingerprint'] = state.get('fingerprint', '')
        for stage, result in state.get('stages', {}).items():
            row[stage] = 'PASS' if result['returncode'] == 0 else 'FAIL'
        valid_report = row['source_matches_current'] and row.get('synth') == 'PASS'
        row['stale_report'] = xml.exists() and not valid_report
        if xml.exists() and valid_report:
            doc = ET.parse(xml).getroot()
            def val(path):
                return doc.findtext(path)
            row.update({
                'latency_min_cycles': val('PerformanceEstimates/SummaryOfOverallLatency/Best-caseLatency'),
                'latency_max_cycles': val('PerformanceEstimates/SummaryOfOverallLatency/Worst-caseLatency'),
                'interval_min_cycles': val('PerformanceEstimates/SummaryOfOverallLatency/Interval-min'),
                'interval_max_cycles': val('PerformanceEstimates/SummaryOfOverallLatency/Interval-max'),
                'estimated_period_ns': val('PerformanceEstimates/SummaryOfTimingAnalysis/EstimatedClockPeriod'),
                'tool_version': val('ReportVersion/Version'), 'part': val('UserAssignments/Part'),
                'target_period_ns': val('UserAssignments/TargetClockPeriod'),
                'uncertainty_ns': val('UserAssignments/ClockUncertainty'),
                'report': str(xml),
            })
            for resource in ('BRAM_18K', 'DSP', 'FF', 'LUT', 'URAM'):
                row[resource] = val('AreaEstimates/Resources/' + resource)
            period = float(row['estimated_period_ns'])
            row['estimated_fmax_mhz'] = round(1000 / period, 3)
            row['meets_100mhz_estimate'] = period <= 10
            row['meets_scheduling_budget'] = period <= float(row['target_period_ns']) - float(row['uncertainty_ns'])
        cosim_report = folder / f'hls/sim/report/{case["top"]}_cosim.rpt'
        if cosim_report.exists() and valid_report and row.get('cosim') == 'PASS':
            for line in cosim_report.read_text(encoding='utf-8', errors='replace').splitlines():
                fields = [field.strip() for field in line.split('|')]
                if len(fields) >= 10 and fields[1] in ('Verilog', 'VHDL'):
                    row['cosim_rtl_status'] = fields[2]
                    for column, index in [('cosim_latency_min', 3), ('cosim_latency_avg', 4), ('cosim_latency_max', 5),
                                          ('cosim_interval_min', 6), ('cosim_interval_avg', 7), ('cosim_interval_max', 8)]:
                        row[column] = fields[index]
                    row['cosim_report'] = str(cosim_report)
            log = folder / 'cosim.log'
            if log.exists():
                counts = re.findall(r'RTL Simulation\s*:\s*\d+\s*/\s*(\d+)', log.read_text(encoding='utf-8', errors='replace'))
                if counts:
                    row['cosim_transactions'] = max(map(int, counts))
        if len(row) > 2:
            rows.append(row)
    results = ROOT / 'results'
    results.mkdir(exist_ok=True)
    (results / 'summary.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    fields = list(dict.fromkeys(k for row in rows for k in row))
    if fields:
        with (results / 'summary.csv').open('w', newline='', encoding='utf-8-sig') as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
    (results / 'source_hashes.json').write_text(json.dumps(source_hashes(), indent=2), encoding='utf-8')
    print(f'Wrote {len(rows)} cases to results/summary.csv', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool-root', type=Path, default=Path(os.environ.get('AMD_TOOL_ROOT', 'C:/AMDDesignTools/2025.2')))
    parser.add_argument('--build-root', type=Path, default=ROOT / 'build')
    parser.add_argument('--part', default='xczu7ev-ffvc1156-2-e')
    parser.add_argument('--cases', nargs='+', default=['all'])
    parser.add_argument('--stages', nargs='+', choices=['csim', 'synth', 'cosim', 'impl', 'package'], default=['csim', 'synth'])
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--bit-tests', action='store_true', help='Use the exhaustive header arithmetic testbench (csim only).')
    parser.add_argument('--summarize-only', action='store_true')
    args = parser.parse_args()
    args.build_root = args.build_root.absolute()
    if args.summarize_only:
        summarize(args)
        return
    if args.bit_tests and args.stages != ['csim']:
        parser.error('--bit-tests requires --stages csim; RTL verification uses tb_kernels.cpp')
    names = list(CASES) if args.cases == ['all'] else args.cases
    unknown = set(names) - set(CASES)
    if unknown:
        parser.error(f'Unknown cases {unknown}. Available: {list(CASES)}')
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        outcomes = list(pool.map(lambda n: run_case(n, args), names))
    summarize(args)
    raise SystemExit(0 if all(outcomes) else 1)


if __name__ == '__main__':
    main()
