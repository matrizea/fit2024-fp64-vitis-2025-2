# FIT2024 C-003 — Vitis HLS 2025.2

倍精度浮動小数点の加算・減算・乗算と、10元Gauss–Jordan消去法の独立再実装です。ZCU104・100 MHz指定で評価しています。

```text
src/                 演算器とHLS top
tests/               算術モデル・丸め境界・C/RTLテスト
scripts/             検証、HLS実行、レポート生成
docs/
  reproduction.md    環境構築と再実行
  implementation.md  数値仕様と回路構成
  evaluation.md      論文との比較、測定条件と制約
  results/           公開時の測定データ
  references/        論文情報と再現条件
```

## 実行

Windows、AMD Vitis / Vivado 2025.2、Python 3が必要です。リポジトリのルートから実行します。

```powershell
python scripts/check_arithmetic.py --tool-root C:/AMDDesignTools/2025.2
python scripts/run_hls.py --tool-root C:/AMDDesignTools/2025.2 --cases add_custom sub_custom mul_custom --stages csim synth cosim
```

全27条件の評価とWindowsのパス長対策は[再実行手順](docs/reproduction.md)にまとめています。生成物は`build/`、再実行した集計値は`results/`に出力され、Gitの管理対象から外れます。

## 評価結果

| 独自演算器 | LUT | FF | DSP | HLS latency | II |
|---|---:|---:|---:|---:|---:|
| 加算 | 1,798 | 206 | 0 | 1 | 1 |
| 減算 | 1,800 | 206 | 0 | 1 | 1 |
| 乗算 | 559 | 80 | 9 | 1 | 1 |

単体17条件でII=1を合成確認し、標準・独自の加減乗算6条件で連続入力のRTL検証が成功しています。独自版は各1,168回、標準版は各16回です。[検証データ](docs/results/ii_verification.json)と[論文との比較](docs/evaluation.md)を参照してください。

論文の原コードを移植したものではありません。資源量・FmaxはHLS推定で、配置配線と実機評価は行っていません。

## 資料

- [解説記事](https://yuyuyuroom.site/2026/10/26/fit2024-fp64-vitis-hls-2025-2/)
- [原論文の公式PDF](https://www.ipsj.or.jp/award/9faeag0000004f4v-att/C-003_3.pdf)：周東裕也ほか、FIT2024 C-003、pp.185–186
- [評価時の全ファイル](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26)：試行候補、過去版、合成XML、RTLレポートをGitタグに保存
