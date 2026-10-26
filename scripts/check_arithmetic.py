"""Compile and run the independent AMD FPO arithmetic model checks on Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).absolute().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool-root', type=Path, default=Path('C:/AMDDesignTools/2025.2'))
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'src', help='Header directory, including an isolated optimization candidate.')
    parser.add_argument('--build-root', type=Path, default=ROOT / 'build/arithmetic')
    parser.add_argument('--results-dir', type=Path, default=ROOT / 'results')
    args = parser.parse_args()
    tool = args.tool_root
    compiler_dir = tool / 'tps/mingw/10.0.0/win64.o/nt/bin'
    model_dir = tool / 'win64/tools/fpo_v7_1'
    env = dict(os.environ)
    env['PATH'] = str(compiler_dir) + os.pathsep + str(model_dir) + os.pathsep + env['PATH']
    target = args.build_root.absolute()
    target.mkdir(parents=True, exist_ok=True)
    results = []
    cases = [('native_oracle', 'tb_fp64.cpp', []),
             ('amd_model', 'tb_fp64.cpp', ['-DREPRO_USE_FPO_GOLDEN']),
             ('amd_boundary', 'tb_amd_model.cpp', [])]
    if (ROOT / 'tests/tb_rounding.cpp').exists():
        cases.append(('amd_rounding', 'tb_rounding.cpp', []))
    for name, source, defines in cases:
        exe = target / (name + '.exe')
        cmd = [str(compiler_dir / 'g++.exe'), '-std=c++14', '-O2', '-fno-fast-math', '-ffp-contract=off',
               '-Wno-unknown-pragmas', '-I' + str(args.source_dir.absolute()), '-I' + str(tool / 'Vitis/include')]
        cmd += defines + [str(ROOT / 'tests' / source)]
        if name != 'native_oracle':
            cmd += [str(model_dir / f'{lib}.lib') for lib in ('libIp_floating_point_v7_1_bitacc_cmodel', 'libgmp', 'libmpfr')]
        cmd += ['-o', str(exe)]
        compile_result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        output = compile_result.stdout
        rc = compile_result.returncode
        if rc == 0:
            run = subprocess.run([str(exe)], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            output += run.stdout
            rc = run.returncode
        log = target / f'{name}.log'
        log.write_text(output, encoding='utf-8')
        print(name + ':\n' + output, flush=True)
        hashes = {str(p.resolve().relative_to(ROOT.resolve())).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in [args.source_dir.absolute() / 'fp64.hpp', ROOT / 'tests' / source]}
        results.append({'name': name, 'returncode': rc, 'command': cmd, 'log': str(log), 'input_sha256': hashes})
    folder = args.results_dir.absolute()
    folder.mkdir(exist_ok=True, parents=True)
    (folder / 'arithmetic_tests.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    sys.exit(0 if all(r['returncode'] == 0 for r in results) else 1)


if __name__ == '__main__':
    main()
