# 数値仕様と回路構成

## ファイル

| ファイル | 役割 |
|---|---|
| [fp64.hpp](../src/fp64.hpp) | binary64の分類、加減算、乗算、正規化と丸め |
| [tops.cpp](../src/tops.cpp) | 標準IP・BIND_OP・独自演算器とGauss–JordanのHLS top |
| [kernels.hpp](../src/kernels.hpp) | topの宣言、Gauss–Jordanの行列サイズ |
| [tb_fp64.cpp](../tests/tb_fp64.cpp) | CPU / AMDモデルとの基本算術比較 |
| [tb_amd_model.cpp](../tests/tb_amd_model.cpp) | AMDモデルとの境界値比較 |
| [tb_rounding.cpp](../tests/tb_rounding.cpp) | halfway、sticky、carry、キャンセルの回帰検証 |
| [tb_kernels.cpp](../tests/tb_kernels.cpp) | 単体演算器と行列計算のC/RTL共通テスト |

## 演算仕様

丸めはround-to-nearest, ties-to-evenです。ゼロ・NaN・Inf・非正規化入力を先に分類し、符号付きゼロも扱います。非正規化入力はゼロ経路へ送り、出力は正規化した53 bit精度で丸めた後にアンダーフローを判定してflush-to-zeroとします。IEEE 754のgradual underflow全体は実装しません。NaN payload/signは保持せず、例外フラグも出力しません。

原実装も同じ種類の入力を例外経路へ送ることは確認しています。返却ビット列、判定優先順位、符号規則の完全一致は原コード未入手のため未確認です。[再現条件](references/reproduction_contract.json)に確認範囲を記録しています。

## 資源削減

- 加減算は先頭ゼロ検出に幅対応のHLS intrinsicを使い、符号を除く63 bitの絶対値比較を一括で行います。
- 53 bit仮数の丸めでcarryが出たときは下位52 bitがゼロになるため、幅広いシフト・選択回路を省けます。
- 乗算は106 bitの正確な積から保持する53 bitとguard/stickyを抽出します。指数幅を13 bitに限定し、同時に起こらない正規化carryと丸めcarryを一つの更新へまとめます。
- 単体topに`PIPELINE II=1`を指定します。例外値も含むRTL連続入力で受付間隔を検証します。

DSPを8個にする分割積の候補は964 LUT・162 FFとなり、採用したDSP 9個の559 LUT・80 FFより増加しました。各候補のソースと比較表は[加減算の試行](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/experiments/add)と[乗算の試行](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/experiments/mul)に保存しています。

## Gauss–Jordan

10×11拡大行列、部分ピボット、各ピボットで標準除算による逆数を1回算出して11列を乗算で正規化する構成です。外部`ap_memory`を使い、ループは非パイプラインです。外部メモリの実体はcoreのBRAM推定に含まれません。元論文とアルゴリズム・メモリ構成の同一性は確認できていません。
