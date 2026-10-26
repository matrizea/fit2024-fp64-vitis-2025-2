# 評価結果

ZCU104（`xczu7ev-ffvc1156-2-e`）、clock 10 ns、uncertainty 2.7 ns、Vitis HLS 2025.2で評価しました。公開時のデータは[results/](results/)へ保存しています。論文はVitis HLS 2022.1を使用しています。

## 単体演算器

| 独自演算器 | 論文 LUT / FF / DSP | 今回 LUT / FF / DSP | 初版からのLUT削減 | 論文比LUT | HLS latency | II |
|---|---:|---:|---:|---:|---:|---:|
| 加算 | 1,697 / 347 / 0 | 1,798 / 206 / 0 | 18.90% | +5.95% | 1 | 1 |
| 減算 | 1,789 / 460 / 0 | 1,800 / 206 / 0 | 18.88% | +0.61% | 1 | 1 |
| 乗算 | 577 / 123 / 8 | 559 / 80 / 9 | 29.69% | −3.12% | 1 | 1 |

推定周期は加減算6.931 ns、乗算6.704 nsです。標準版は加減算784 LUT・450 FF・3 DSP、乗算205 LUT・304 FF・11 DSPで、raw latency 4、II=1です。短いlatencyを指定した一部BIND_OP版は推定周期が10 nsを超えます。27条件すべての合成成功が全条件のタイミング達成を意味するわけではありません。

[全27条件 JSON](results/summary.json) / [CSV](results/summary.csv) / [最適化前後](results/optimization.json) / [論文値](results/paper_reference.json)

## latencyとII

保存データのlatencyはHLSの生値です。開始C0から完了Cnまでを開始を含むn+1個のサイクル枠で比較すると、latency 0は同一サイクル内の1枠、latency 1は2枠です。これは制御ステップの数え方で、HLSのabsolute latencyへ1周期を加算する補正ではありません。

| 単体top | 論文の記載値 | 初版：生値→開始を含む数 | 最終版：生値→開始を含む数 | 最終II |
|---|---:|---:|---:|---:|
| 標準加算・減算・乗算 | 5 | 4→5 | 4→5 | 1 |
| 独自加算・減算 | 3 | 2→3 | 1→2 | 1 |
| 独自乗算 | 2 | 1→2 | 1→2 | 1 |

論文本文は開始・終了サイクルの数え方を明示していないため、上記の比較規約と生値を分けています。BIND_OPの指定latency、II、Gauss–Jordanの実測経過サイクルへ一律に1を加えません。[機械可読の比較](results/latency_comparison.json)も保存しています。

**論文と同じII=1を達成しています。** 単体17条件で合成確認し、標準・独自の加減乗算6条件はRTL Cosimも成功しました。独自版は各1,168回、標準版は各16回です。トランザクション間隔の最小・平均・最大はすべて1です。末尾のinterval `x`は次の入力がなく未定義なので、検査した間隔は独自版各1,167個、標準版各15個です。

[II検証データ](results/ii_verification.json) / [全生レポート](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26/reports/ii1)

## 算術・行列検証

| 検証 | 比較数・条件 | 結果 |
|---|---|---|
| CPU参照 | 63,072比較 | 成功 |
| AMD公式モデル | 63,072比較 | 成功 |
| AMD境界値 | 85,920比較 | 成功 |
| AMD丸め回帰 | 32,928比較 | 成功 |
| Csim・合成 | 27条件 | 成功 |
| 単体RTL | 標準・独自の6条件 | 成功 |
| Gauss–Jordan RTL | 標準・独自・inlineの3条件、同じ4入力 | 成功 |

算術の合計244,992比較は重複しない入力数やRTL検証数ではありません。AMDモデル3 suiteが181,920比較、CPU参照が63,072比較です。NaN payload/signは比較対象外です。[算術検証記録](results/arithmetic_tests.json)と[評価時のソースハッシュ](results/source_hashes.json)を保存しています。

Gauss–Jordanは標準12,880 cycle、独自両置換8,700 cycle、inline両置換6,620 cycleです。標準/inlineの比は約1.946倍です。論文の14,138 / 8,638 cycleによる約1.63倍とは入力・構成が同一と確認できていません。4入力の最大解誤差は4.441e−16、相対残差は9.027e−17でした。

## 出典と測定範囲

原論文は周東裕也、大本裕真、窪田昌史、弘中哲夫「高位合成を用いて記述したFPGA上の倍精度浮動小数点演算器の論理合成最適化と性能評価」、FIT2024 C-003、第1分冊 pp.185–186です。[IEICE概要](https://www.ieice.org/publications/conferences/summary.php?id=FIT0000016548&expandable=2&ConfCd=F&session_num=3c&lecture_number=C-003&year=2024&conf_type=F) / [公式本文PDF](https://www.ipsj.or.jp/award/9faeag0000004f4v-att/C-003_3.pdf) / [論文情報](references/source.json)

資源量・FmaxはHLS合成推定、サイクル数は生成RTLのシミュレーション結果です。配置配線、実機クロック到達性、消費電力、ボード上の実行時間は評価していません。有限入力集合の試験であり、全入力の形式的証明ではありません。原コードと元の評価行列は入手していません。
