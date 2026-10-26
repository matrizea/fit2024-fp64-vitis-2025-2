# FIT2024 C-003：Vitis HLS 2025.2向け独立再実装

倍精度浮動小数点の加算・減算・乗算と、10元Gauss–Jordan消去法を、ZCU104・100 MHz指定で評価するソース一式です。論文の原コードを移植したものではありません。原コード、元の評価行列、全pragmaは入手していません。

## 必要環境

- Windows、AMD Vitis / Vivado 2025.2（既定の配置は C:/AMDDesignTools/2025.2）。
- Python 3。Pythonスクリプトは標準ライブラリのみを使います。
- 算術テストにはAMDインストールに付属するMinGW、Vitisのap_int/hls_fpoヘッダ、FPO v7.1 Cモデル、GMP、MPFRを使用します。これらのAMDヘッダやライブラリは同梱していません。
- 対象partは xczu7ev-ffvc1156-2-e、clock=10 ns、uncertainty=27%です。

## 再実行

このリポジトリを取得し、READMEがあるルートフォルダをPowerShellで開きます。Windowsの生成パスを短くするため、空いているドライブへ割り当てます。R:が使用済みなら別の文字に読み替えてください。

```powershell
subst R: (Get-Location).Path
python R:/scripts/check_arithmetic.py --tool-root C:/AMDDesignTools/2025.2
python R:/scripts/run_hls.py --tool-root C:/AMDDesignTools/2025.2 --cases all --stages csim synth --jobs 3
python R:/scripts/run_hls.py --tool-root C:/AMDDesignTools/2025.2 --cases add_custom sub_custom mul_custom gj_native gj_custom_both gj_inline_both --stages cosim --jobs 2
python R:/scripts/make_report.py
python R:/scripts/make_optimization_report.py
```

`python`はPython 3の実行ファイルに読み替えられます。AMDの配置が異なる場合は`--tool-root`を変更します。同じソース・設定で途中から再開するときだけ`--resume`を追加できます。ツールの版を変えた場合は付けずに再実行してください。Rドライブを使い終えたら`subst R: /D`で割り当てを解除できます。

`run_hls.py`は各条件のHLS設定を生成し、2025.2の`v++`/`vitis-run --mode hls --config`を起動します。生成設定のソースパスは絶対パスなので、フォルダを移動した後は再実行してください。27条件すべての合成成功は、すべての条件が100 MHzの推定タイミングを満たすことを意味しません。短いlatencyを指定した一部BIND_OP版は推定周期が10 nsを超えます。

## ソースと結果

- [src/fp64.hpp](src/fp64.hpp)：binary64のビット演算。RNE、特殊値、符号付きゼロ、非正規化入力と極小出力のFTZを扱います。通常53 bit精度で丸めた後にアンダーフローを判定し、IEEE754のgradual underflow全体は実装しません。NaN payload/signは保持せず、例外フラグは出力しません。
- [src/tops.cpp](src/tops.cpp)：標準IP、BIND_OP、独自演算器、Gauss–Jordanの比較topです。
- [tests/](tests/)：CPU参照、AMD公式モデル、丸め境界、C/RTL共通テストです。
- [scripts/](scripts/)：算術検証、HLS実行、結果集計の4スクリプトです。
- [results/report.md](results/report.md)、[results/summary.json](results/summary.json)：最終版の記録値です。
- [results/paper_reference.json](results/paper_reference.json)：論文表の転記値です。
- [results/optimization.md](results/optimization.md)、[results/optimization.json](results/optimization.json)：最適化前後の比較です。
- [results/arithmetic_tests.json](results/arithmetic_tests.json)：4 suite、合計244,992比較の記録です。AMDモデル3 suiteは181,920比較、残り63,072比較はCPU参照です。重複しない入力数やRTL検証数ではありません。
- [history/before_optimization/src/](history/before_optimization/src/)、[history/before_optimization/results/summary.json](history/before_optimization/results/summary.json)：最適化前の比較スナップショットです。最適化レポートの生成が参照します。
- [experiments/add/README.md](experiments/add/README.md)、[experiments/mul/README.md](experiments/mul/README.md)：候補の比較とソースです。
- [references/source.json](references/source.json)、[references/reproduction_contract.json](references/reproduction_contract.json)：出典と再現条件です。
- [MANIFEST.sha256](MANIFEST.sha256)：同梱ファイルのSHA-256です。

results/の結果は記事執筆時の保存値です。ローカルの絶対パスは相対パスへ置き換え、実行コマンド、未同梱の生ログへの機密性のある参照は省きました。ソースと数値結果は維持しています。再実行後はその環境の結果で上書きされます。生成RTL・ビルド成果物・生ログ・バイナリ・元論文PDFは公開対象に含めていません。

## 単体latencyの数え方と入力受付間隔

保存レポートのlatency列はHLSの生値です。単体演算器を論文表2と比べる際は、開始サイクルC0から完了サイクルCnまでを、開始を含むn+1個のサイクル枠として数えます。HLS latency 0は同一サイクル内の完了なので、この数え方では1サイクル枠です。これは制御ステップの数え方で、HLSのLatency (absolute)欄の経過時間に1周期を加算する補正ではありません。

| 非パイプライン単体top | 論文 | 初版HLS生値→開始を含む数 | 最終HLS生値→開始を含む数 | 最終interval |
|---|---:|---:|---:|---:|
| 標準加算・減算・乗算 | 5 | 4→5 | 4→5 | 5 |
| 独自加算 | 3 | 2→3 | 1→2 | 2 |
| 独自減算 | 3 | 2→3 | 1→2 | 2 |
| 独自乗算 | 2 | 1→2 | 1→2 | 2 |

標準IPと初版独自演算器は、この換算で論文値に一致します。最終版は加減算が論文より1サイクル枠少なく、乗算は同じ2サイクル枠です。原論文は開始・終了サイクルの数え方を明示していないため、これは上記の比較規約に基づく整理です。BIND_OPの指定latency、interval、Gauss–JordanのRTL実測経過サイクルへ一律に1を加えません。機械可読の比較は[results/latency_comparison.json](results/latency_comparison.json)に保存しています。元のHLS結果列は変更していません。

**II=1は達成していません。** 最終版の独自加算・減算・乗算の単体topは非パイプライン構成で、HLS生値のtop latencyが1、入力の受付間隔intervalが2です。毎クロック新しい入力を受け付ける回路ではありません。Gauss–Jordanのループも`PIPELINE off`です。ここでの最適化は算術回路の資源量と処理のスケジューリングを改善したもので、毎クロック投入可能な演算器への改造を含みません。

詳細な確認記録は[results/ii_audit.json](results/ii_audit.json)にあります。内部IPや診断用整数probeのinterval 1は、公開単体topのII=1達成を意味しません。

## 評価範囲

資源量とFmaxはHLS合成推定、処理サイクルは生成RTLの協調シミュレーション結果です。配置配線、実機クロック到達性、消費電力、ボード上の実行時間は評価していません。単体RTLは3 topを各1,168回、Gauss–Jordanは標準・独自・inlineの3 topを同じ4入力で検証しました。有限入力集合の試験で、全入力の形式的証明ではありません。

原実装もゼロ・NaN・Inf・非正規化入力を例外経路で処理することは確認しています。返却ビット列、判定優先順位、符号規則の完全一致は確認していません。

Gauss–Jordanは10×11拡大行列、部分ピボット、各ピボットで標準除算による逆数を1回算出して11列を乗算で正規化、外部ap_memory、非パイプラインループという再構成です。原論文と構成の同一性は未確認です。今回の4入力での標準12,880 cycle / 最終inline 6,620 cycleは約1.946倍ですが、論文の14,138 / 8,638 cycleによる約1.63倍とは同一条件の比較ではありません。外部メモリの実体はcoreのBRAM推定に含まれません。

## 出典

- 周東裕也、大本裕真、窪田昌史、弘中哲夫「高位合成を用いて記述したFPGA上の倍精度浮動小数点演算器の論理合成最適化と性能評価」、FIT2024 C-003、第1分冊 pp.185–186。
- [IEICE論文概要](https://www.ieice.org/publications/conferences/summary.php?id=FIT0000016548&expandable=2&ConfCd=F&session_num=3c&lecture_number=C-003&year=2024&conf_type=F)
- [情報処理学会の公式本文PDF](https://www.ipsj.or.jp/award/9faeag0000004f4v-att/C-003_3.pdf)
- [AMD Vitis HLS 2025.2 CLI](https://docs.amd.com/r/2025.2-English/ug1399-vitis-hls/vitis-v-and-vitis-run-Commands)
- [AMD 2025.2 HLSコンポーネント作成](https://docs.amd.com/r/2025.2-English/ug1702-vitis-accelerated-reference/Creating-HLS-Components-from-the-Command-Line)
- [AMD Floating-Point Operator PG060](https://docs.amd.com/v/u/en-US/pg060-floating-point)
