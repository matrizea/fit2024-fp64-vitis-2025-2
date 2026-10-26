# 再実行手順

## 環境

- Windows、AMD Vitis / Vivado 2025.2。既定の配置は`C:/AMDDesignTools/2025.2`。
- Python 3。スクリプトは標準ライブラリのみを使用。
- 算術検証にはAMD付属のMinGW、ap_int / hls_fpoヘッダ、FPO v7.1 Cモデル、GMP、MPFRを使用。AMDのヘッダとライブラリは同梱していません。
- part：`xczu7ev-ffvc1156-2-e`、clock：10 ns、uncertainty：27%（2.7 ns）。

## 全条件の検証

READMEがあるルートをPowerShellで開きます。WindowsではHLS生成パスが深くなるため、空いているドライブへ割り当ててから実行します。R:が使用済みなら別の文字に読み替えてください。

```powershell
subst R: (Get-Location).Path
python R:/scripts/check_arithmetic.py --tool-root C:/AMDDesignTools/2025.2
python R:/scripts/run_hls.py --tool-root C:/AMDDesignTools/2025.2 --cases all --stages csim synth --jobs 3
python R:/scripts/run_hls.py --tool-root C:/AMDDesignTools/2025.2 --cases add_custom sub_custom mul_custom add_native sub_native mul_native gj_native gj_custom_both gj_inline_both --stages cosim --jobs 2
python R:/scripts/make_report.py
python R:/scripts/make_optimization_report.py
python R:/scripts/make_ii_report.py
```

`run_hls.py`は各条件の設定を生成し、`v++`と`vitis-run --mode hls --config`を起動します。27条件は単体17条件とGauss–Jordan 10条件です。算術テストの入力とHLSソースは変更していません。

ツールの配置が異なる場合は`--tool-root`を変更します。同じソース・設定での再開には`--resume`を追加できます。ツールの版を変えた場合は外してください。生成設定には絶対パスが入るため、プロジェクトを移動した後は設定を再生成します。

R:を使い終えたら`subst R: /D`で割り当てを解除できます。

## 入出力

| 場所 | 内容 | Git管理 |
|---|---|---|
| `src/`、`tests/` | HLS入力とテスト | 対象 |
| `docs/results/` | 公開時の測定スナップショット | 対象 |
| `build/` | 設定、状態記録、合成・RTL出力、生ログ、実行バイナリ | 対象外 |
| `results/` | 自分の環境で再実行した集計値 | 対象外 |

公開済み測定データと再実行結果を分けています。レポート生成スクリプトは既定で`results/`を読み書きします。`make_report.py`と`make_optimization_report.py`は`--results-dir`で別の集計先を指定できます。最適化比較の基準値は`docs/results/baseline.json`です。

II検証は`results/summary.json`、`results/arithmetic_tests.json`と`build/<case>/hls/sim/report/verilog/result.transaction.rpt`を確認します。算術検証4 suiteの成功と入力ハッシュの一致も検査します。別のビルド先を使う場合は`make_ii_report.py --build-root ... --results-dir ...`を指定します。

生レポートと過去の候補を調べる場合は[保存タグ](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26)を参照してください。別フォルダに`git clone --branch evaluation-2026-10-26 https://github.com/matrizea/fit2024-fp64-vitis-2025-2.git`で取得できます。
