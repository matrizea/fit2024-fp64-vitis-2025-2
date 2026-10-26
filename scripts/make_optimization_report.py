"""Compare the immutable pre-optimization snapshot with the selected sources."""
import argparse
import csv
import json
from pathlib import Path

ROOT = Path(__file__).absolute().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results-dir', type=Path, default=ROOT / 'results')
    folder = parser.parse_args().results_dir.resolve()
    before = {r['case']: r for r in json.loads((ROOT / 'docs/results/baseline.json').read_text(encoding='utf-8'))}
    after = {r['case']: r for r in json.loads((folder / 'summary.json').read_text())}
    paper = json.loads((folder / 'paper_reference.json').read_text())
    text = ['# 単体演算器の資源最適化', '',
            '同じZCU104対象part、10ns clock、27% uncertainty、Vitis HLS 2025.2で、修正前と採用した修正後を比較しています。資源量・FmaxはHLS推定です。', '',
            '原実装もゼロ・NaN・Inf・非正規化数を例外経路で処理することがユーザーから確認されています。例外分類の範囲は共通なので、対応範囲の広さを資源差の理由にはしません。返却bitや優先順位までの同一性は未検証です。', '',
            '値は「論文 / 修正前 / 修正後」です。レイテンシはHLS topの報告値をそのまま使い、入力の受付間隔とは分けます。論文のII=1に合わせ、現行の単体topはPIPELINE II=1です。修正前とII=2構成はGitタグevaluation-2026-10-26のhistory/に保存しています。', '',
            '| 演算器 | LUT | FF | DSP | top latency [cycle] | interval [cycle]（前→後） | 推定Fmax [MHz] |',
            '|---|---:|---:|---:|---:|---:|---:|']
    rows = []
    for name, label in [('add_custom', '加算'), ('sub_custom', '減算'), ('mul_custom', '乗算')]:
        b, a, p = before[name], after.get(name, {}), paper[name]
        if not a.get('source_matches_current') or a.get('synth') != 'PASS':
            raise RuntimeError(f'{name}: no current synthesis result; refusing to publish stale metrics')
        if a.get('cosim_rtl_status') != 'Pass':
            raise RuntimeError(f'{name}: current RTL cosimulation must pass before publishing the optimization report')
        row = {'case': name}
        for metric in ('LUT', 'FF', 'DSP'):
            row['paper_'+metric] = p[metric]
            row['before_'+metric] = int(b[metric])
            row['after_'+metric] = int(a[metric])
        row['LUT_change_percent'] = round((float(a['LUT']) / float(b['LUT']) - 1) * 100, 2)
        row['paper_LUT_gap_percent'] = round((float(a['LUT']) / p['LUT'] - 1) * 100, 2)
        for metric in ('latency_max_cycles', 'interval_max_cycles', 'estimated_fmax_mhz'):
            row['before_'+metric] = b[metric]
            row['after_'+metric] = a[metric]
        row['cosim'] = a.get('cosim_rtl_status', '未実施')
        row['cosim_transactions'] = a.get('cosim_transactions', 0)
        rows.append(row)
        triple = lambda metric: f'{p[metric]:,} / {int(b[metric]):,} / {int(a[metric]):,}'
        text.append(f'| {label} | {triple("LUT")} | {triple("FF")} | {triple("DSP")} | {p["latency"]} / {b["latency_max_cycles"]} / {a["latency_max_cycles"]} | {b["interval_max_cycles"]}→{a["interval_max_cycles"]} | {p["fmax"]:.2f} / {float(b["estimated_fmax_mhz"]):.2f} / {float(a["estimated_fmax_mhz"]):.2f} |')
    text += ['', '修正前からのLUT変化:', '']
    for row in rows:
        text.append(f'- {row["case"]}: {row["LUT_change_percent"]:+.2f}%。論文値との差は {row["paper_LUT_gap_percent"]:+.2f}%。RTL検証: {row["cosim"]}、{row["cosim_transactions"]}呼出し。')
    text += ['', '## 採用した変更', '',
             '- 加減算: 手書きの先頭ゼロ検出を幅対応のHLS intrinsicへ変更。符号を除く63bitを一括比較し、指数・仮数を別々に比較する回路を簡略化。',
             '- 共通丸め: 53bit仮数に1を加えてcarryが出る場合、その値は厳密に2^53で下位bitはゼロ。この性質を使ってcarry時の幅広い仮数シフト・選択回路を除去。',
             '- 乗算: 正確な106bit積を維持し、保持する53bitとguard/stickyを直接抽出。指数を13bitに限定し、同時には発生しない正規化carryと丸めcarryを一つの更新へ統合。', '',
             'DSP8にする分割積の候補も合成しましたが、964LUT・162FFへ増加しました。採用したDSP9の算術データパスの方がLUT・FFの削減が大きいため、乗算DSP数には論文との差が1個残っています。II=1対応では算術処理を変えず、単体topへのPIPELINE指定で受付制御を変更しています。候補と不採用理由: [加減算](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/experiments/add)、[乗算](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/experiments/mul)。', '',
             '演算精度や特殊値対応を削って面積を減らす変更は採用していません。AMD公式モデルとの算術比較と、選択topを呼び出すRTL Cosimを実施しています。検証した入力集合における一致であり、全入力の形式的証明ではありません。', '',
             '開始サイクルを含む枠数では、HLS生latency1は2枠です。論文3/3/2枠に対し最終独自加減乗算は2/2/2枠と整理できます。論文本文はサイクルの数え方を明示していないため、この比較規約とHLSの生値を分けて記載します。IIは補正せず、RTL Cosimでも確認します。', '',
             '[最終ソース](../src/fp64.hpp) / [全結果](report.md) / [修正前の保存結果](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/history/before_optimization/results) / [修正前の単体合成報告](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/history/before_optimization/reports)', '']
    (folder / 'optimization.md').write_text('\n'.join(text), encoding='utf-8')
    (folder / 'optimization.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    with (folder / 'optimization.csv').open('w', newline='', encoding='utf-8-sig') as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f'Wrote {folder / "optimization.md"} and optimization.csv')


if __name__ == '__main__':
    main()
