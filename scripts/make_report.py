"""Render comparison tables from actual HLS reports without filling missing data."""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).absolute().parent.parent

# Manually transcribed from the complete two-page paper, Tables 2 and 3.
# Paper add/sub entries share an implementation row; both are kept here.
REFERENCE = {}
def ref(name, cycles, mhz, dsp, ff, lut, bram=None):
    REFERENCE[name] = dict(latency=cycles, fmax=mhz, DSP=dsp, FF=ff, LUT=lut, BRAM_reported=bram)

for op in ('add', 'sub'):
    ref(op+'_native', 5, 197.28, 3, 450, 813)
    for impl, mhz, dsp, ff, lut in [('fabric',78.98,0,331,830), ('dsp',63.89,3,448,802), ('fulldsp',63.89,3,448,802)]:
        ref(op+'_bind_'+impl, 3, mhz, dsp, ff, lut)
ref('mul_native',5,142.01,11,304,234)
for impl, mhz, dsp, ff, lut in [('fabric',70.20,0,479,2655),('dsp',63.89,11,301,217),('meddsp',63.89,9,325,270),('fulldsp',63.89,10,284,227),('maxdsp',63.89,11,301,217)]:
    ref('mul_bind_'+impl,2,mhz,dsp,ff,lut)
ref('add_custom',3,137.58,0,347,1697)
ref('sub_custom',3,137.17,0,460,1789)
ref('mul_custom',2,142.63,8,123,577)
for name, cyc, mhz, dsp, ff, lut in [
    ('native',14138,139.61,25,2577,3515),
    ('bind_mul',10838,70.20,3,2927,8364),
    ('bind_sub',13038,78.98,22,2586,3534),
    ('bind_both',9739,70.20,0,2936,8383),
    ('custom_mul',10938,139.61,11,2265,3989),
    ('custom_sub',14138,139.61,22,2850,4773),
    ('custom_both',9938,139.61,22,2469,5259),
    ('inline_mul',9838,139.61,19,2470,4630),
    ('inline_sub',11938,139.61,22,3017,6926),
    ('inline_both',8638,137.02,16,2779,8526)]:
    ref('gj_'+name,cyc,mhz,dsp,ff,lut,4)


def main():
    result_dir = ROOT / 'results'
    rows = json.loads((result_dir / 'summary.json').read_text(encoding='utf-8'))
    data = {row['case']: row for row in rows}
    def cell(row, key):
        return str(row.get(key, '-'))
    current = [r for r in rows if r.get('source_matches_current') and r.get('synth') == 'PASS']
    passed_csim = sum(r.get('csim') == 'PASS' and r.get('source_matches_current') for r in rows)
    passed_cosim = [r for r in rows if r.get('cosim_rtl_status') == 'Pass']
    text = [
        '# Vitis HLS 2025.2 再実装の実行結果', '',
        'FIT2024 C-003の手法を独立に再実装し、既存のVitis/Vivado 2025.2で実行した結果です。元コードは本文に掲載されておらず、Gauss-Jordanのループ・メモリ構成の詳細も記載されていないため、論文と同一の設計を移植した結果ではありません。', '',
        'ユーザー確認により、原実装もゼロ・NaN・Inf・非正規化数を例外経路で処理します。例外分類の範囲は今回と共通です。返却bit・判定優先順位の詳細は原コード未入手のため同一性を検証していません。', '',
        f'最終ソースでCsim成功 **{passed_csim}/27条件**、HLS合成成功 **{len(current)}/27条件**、RTL Cosim成功 **{len(passed_cosim)}条件**。', '',
        '## 単体演算器', '',
        'latencyはHLS top reportのWorst-caseLatency、intervalはInterval-maxを補正せず記載します。論文のレイテンシとtop handshakeを含む測定範囲が異なる可能性があります。標準IPの論文5 cycleに対し2025.2のtop latencyは4、intervalは5です。原top非公開のため同一性は断定しません。', '',
        '| 条件 | 論文 latency | 2025.2 top latency | interval | 論文 Fmax MHz | HLS推定 Fmax MHz | DSP | FF | LUT |',
        '|---|---:|---:|---:|---:|---:|---:|---:|---:|'
    ]
    for name, paper in REFERENCE.items():
        if name.startswith('gj_'): continue
        r = data.get(name, {})
        text.append(f'| {name} | {paper["latency"]} | {cell(r,"latency_max_cycles")} | {cell(r,"interval_max_cycles")} | {paper["fmax"]:.2f} | {cell(r,"estimated_fmax_mhz")} | {cell(r,"DSP")} | {cell(r,"FF")} | {cell(r,"LUT")} |')
    text += ['', '## 10元 Gauss-Jordan', '',
             '部分pivot、逆数を1回求める正規化、外部ap_memory、非pipelineループを採用しています。これらは再構成の仮定です。HLS静的maxは条件分岐の上界、minは特異行列での早期returnを含み、通常入力の実測時間と同じではありません。論文のBRAM4に対し本設計のcore BRAM0は、外部メモリを面積に含めないことによる構成差です。', '',
             '| 条件 | 論文 cycles | HLS静的 min..max cycles | HLS推定 Fmax MHz | BRAM18K | DSP | LUT | 4入力RTL実測 min..max cycles |',
             '|---|---:|---:|---:|---:|---:|---:|---:|']
    for name, paper in REFERENCE.items():
        if not name.startswith('gj_'): continue
        r = data.get(name, {})
        measured = f'{r["cosim_latency_min"]}..{r["cosim_latency_max"]}' if 'cosim_latency_min' in r else '未実施'
        text.append(f'| {name} | {paper["latency"]:,} | {cell(r,"latency_min_cycles")}..{cell(r,"latency_max_cycles")} | {cell(r,"estimated_fmax_mhz")} | {cell(r,"BRAM_18K")} | {cell(r,"DSP")} | {cell(r,"LUT")} | {measured} |')
    native = data.get('gj_native', {})
    inline = data.get('gj_inline_both', {})
    if 'cosim_latency_max' in native and 'cosim_latency_max' in inline:
        speed = float(native['cosim_latency_max'])/float(inline['cosim_latency_max'])
        text += ['', f'今回の4入力におけるRTL実測の最大cycle数の比は、標準版/inline両置換版で **{speed:.3f}倍** です。全設計の指定クロックは100 MHzです。これは今回の入力・再実装の結果であり、論文の1.63倍と同一条件の測定ではありません。']
    text += ['', '## 数値検証', '',
             '- AMD FPO公式Cモデル: 63,072算術比較と独立した85,920境界値比較。NaN payload/signは同一性の対象外、quiet NaNであることを確認。',
             '- CPU oracle: 63,072比較。通常のCPUのgradual underflowとAMDモデルの差があるため極小値境界を補正。公式モデルを正解の基準とする。',
             '- 最適化回帰テスト: AMDモデルとの32,928比較。RNE halfwayの偶奇、指数差1～60と大差、近接キャンセル、乗算stickyと分割境界のcarryを追加検証。',
             '- custom単体RTL: 各1,168回のtop呼出し。NaN/Inf、符号付きゼロ、denormal、丸め、極小値境界を含む。',
             '- Gauss-Jordan: 対角・密・pivot交換・scaledの4系。long double CPU参照解、既知解、相対残差、消去後の単位行列を検証。', '',
             '| RTL検証対象 | 結果 | latency min/avg/max | interval min/avg/max |',
             '|---|---|---:|---:|']
    for row in passed_cosim:
        text.append('| '+row['case']+' | '+row['cosim_rtl_status']+' | '+ '/'.join(cell(row,'cosim_latency_'+s) for s in ('min','avg','max'))+' | '+ '/'.join(cell(row,'cosim_interval_'+s) for s in ('min','avg','max'))+' |')
    arithmetic = json.loads((result_dir/'arithmetic_tests.json').read_text()) if (result_dir/'arithmetic_tests.json').exists() else []
    text += ['', '算術検証の実行記録:', '']
    for test in arithmetic:
        log = Path(test['log'])
        lines = [line for line in log.read_text(encoding='utf-8',errors='replace').splitlines() if 'checks' in line] if log.exists() else []
        text.append(f'- {test["name"]}: returncode={test["returncode"]} / '+ '; '.join(lines))
    text += ['', '## 読み取り上の制約', '',
             'Fmax・DSP/FF/LUTはHLS推定で、配置配線後のtimingや実機測定ではありません。clock=10ns、uncertainty=2.7nsで全条件を統一しました。1000/estimated periodが100MHz以上でも、uncertaintyを引いた7.3nsのスケジューリング目標や実機到達を保証しません。BIND_OPで短いcycle数を指定してもtiming違反がある場合、100MHzの有効な高速化と解釈できません。', '',
             '元論文の演算器LUT数、Gauss-Jordanのlatency・BRAM・DSP数は完全には一致していません。資源共有、メモリ構成、演算器の実装、ツールのスケジュールが異なるためです。論文の表2にはBRAM列がなく、転記JSONではnullとしています。表3のBRAM列には18K単位の明記がありません。本設計のBRAM_18K列と同一単位とは断定していません。論文の最大1.63倍高速化の厳密な追試には、著者のソースと評価条件が必要です。', '',
             '[単体演算器の修正前後比較](optimization.md) / [全測定値 CSV](summary.csv) / [元論文の転記値 JSON](paper_reference.json) / [現在のソース SHA256](source_hashes.json) / [実行手順](../README.md)', '',
             '[元論文の公式PDF](https://www.ipsj.or.jp/award/9faeag0000004f4v-att/C-003_3.pdf) 表1-3、[AMD 2025.2 CLI](https://docs.amd.com/r/2025.2-English/ug1399-vitis-hls/vitis-v-and-vitis-run-Commands)を参照。', '']
    (result_dir/'report.md').write_text('\n'.join(text), encoding='utf-8')
    (result_dir/'paper_reference.json').write_text(json.dumps(REFERENCE, indent=2), encoding='utf-8')
    comparison = []
    for name,paper in REFERENCE.items():
        r = data.get(name, {})
        item = {'case': name, **{'paper_'+k:v for k,v in paper.items()}, **r}
        comparison.append(item)
    fields = list(dict.fromkeys(k for row in comparison for k in row))
    with (result_dir/'comparison.csv').open('w', newline='', encoding='utf-8-sig') as f:
        writer=csv.DictWriter(f,fieldnames=fields)
        writer.writeheader(); writer.writerows(comparison)
    print('Wrote results/report.md and comparison.csv')


if __name__ == '__main__':
    main()
