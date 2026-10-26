# Vitis HLS 2025.2 再実装の実行結果

FIT2024 C-003の手法を独立に再実装し、既存のVitis/Vivado 2025.2で実行した結果です。元コードは本文に掲載されておらず、Gauss-Jordanのループ・メモリ構成の詳細も記載されていないため、論文と同一の設計を移植した結果ではありません。

最終ソースでCsim成功 **27/27条件**、HLS合成成功 **27/27条件**、RTL Cosim成功 **6条件**。

## 単体演算器

latencyはHLS top reportのWorst-caseLatency、intervalはInterval-maxを補正せず記載します。論文のレイテンシとtop handshakeを含む測定範囲が異なる可能性があります。標準IPの論文5 cycleに対し2025.2のtop latencyは4、intervalは5です。原top非公開のため同一性は断定しません。

| 条件 | 論文 latency | 2025.2 top latency | interval | 論文 Fmax MHz | HLS推定 Fmax MHz | DSP | FF | LUT |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| add_native | 5 | 4 | 5 | 197.28 | 197.278 | 3 | 450 | 813 |
| add_bind_fabric | 3 | 3 | 4 | 78.98 | 157.58 | 0 | 332 | 836 |
| add_bind_dsp | 3 | 3 | 4 | 63.89 | 108.897 | 3 | 434 | 788 |
| add_bind_fulldsp | 3 | 3 | 4 | 63.89 | 108.897 | 3 | 434 | 788 |
| sub_native | 5 | 4 | 5 | 197.28 | 197.278 | 3 | 450 | 813 |
| sub_bind_fabric | 3 | 3 | 4 | 78.98 | 157.58 | 0 | 332 | 836 |
| sub_bind_dsp | 3 | 3 | 4 | 63.89 | 108.897 | 3 | 434 | 788 |
| sub_bind_fulldsp | 3 | 3 | 4 | 63.89 | 108.897 | 3 | 434 | 788 |
| mul_native | 5 | 4 | 5 | 142.01 | 142.005 | 11 | 304 | 234 |
| mul_bind_fabric | 2 | 2 | 3 | 70.20 | 84.246 | 0 | 259 | 2660 |
| mul_bind_dsp | 2 | 2 | 3 | 63.89 | 53.169 | 11 | 259 | 199 |
| mul_bind_meddsp | 2 | 2 | 3 | 63.89 | 56.609 | 9 | 259 | 250 |
| mul_bind_fulldsp | 2 | 2 | 3 | 63.89 | 58.682 | 10 | 259 | 232 |
| mul_bind_maxdsp | 2 | 2 | 3 | 63.89 | 53.169 | 11 | 259 | 199 |
| add_custom | 3 | 2 | 3 | 137.58 | 140.964 | 0 | 321 | 2217 |
| sub_custom | 3 | 2 | 3 | 137.17 | 140.964 | 0 | 321 | 2219 |
| mul_custom | 2 | 1 | 2 | 142.63 | 137.061 | 9 | 124 | 795 |

## 10元 Gauss-Jordan

部分pivot、逆数を1回求める正規化、外部ap_memory、非pipelineループを採用しています。これらは再構成の仮定です。HLS静的maxは条件分岐の上界、minは特異行列での早期returnを含み、通常入力の実測時間と同じではありません。論文のBRAM4に対し本設計のcore BRAM0は、外部メモリを面積に含めないことによる構成差です。

| 条件 | 論文 cycles | HLS静的 min..max cycles | HLS推定 Fmax MHz | BRAM18K | DSP | LUT | 4入力RTL実測 min..max cycles |
|---|---:|---:|---:|---:|---:|---:|---:|
| gj_native | 14,138 | 47..14100 | 142.005 | 0 | 14 | 2502 | 12880..12880 |
| gj_bind_mul | 10,838 | 47..11680 | 84.246 | 0 | 3 | 4926 | 未実施 |
| gj_bind_sub | 13,038 | 47..13000 | 142.005 | 0 | 11 | 2527 | 未実施 |
| gj_bind_both | 9,739 | 47..10580 | 84.246 | 0 | 0 | 4950 | 未実施 |
| gj_custom_mul | 10,938 | 47..11680 | 137.061 | 0 | 12 | 3074 | 未実施 |
| gj_custom_sub | 14,138 | 47..13000 | 140.964 | 0 | 11 | 3929 | 未実施 |
| gj_custom_both | 9,938 | 47..10580 | 137.061 | 0 | 9 | 4501 | 9690..9690 |
| gj_inline_mul | 9,838 | 47..9380 | 137.061 | 0 | 12 | 3801 | 未実施 |
| gj_inline_sub | 11,938 | 47..11900 | 140.964 | 0 | 11 | 3912 | 未実施 |
| gj_inline_both | 8,638 | 47..8280 | 137.061 | 0 | 9 | 5179 | 7610..7610 |

今回の4入力におけるRTL実測の最大cycle数の比は、標準版/inline両置換版で **1.693倍** です。全設計の指定クロックは100 MHzです。これは今回の入力・再実装の結果であり、論文の1.63倍と同一条件の測定ではありません。

## 数値検証

- AMD FPO公式Cモデル: 63,072算術比較と独立した85,920境界値比較。NaN payload/signは同一性の対象外、quiet NaNであることを確認。
- CPU oracle: 63,072比較。通常のCPUのgradual underflowとAMDモデルの差があるため極小値境界を補正。公式モデルを正解の基準とする。
- custom単体RTL: 各1,168回のtop呼出し。NaN/Inf、符号付きゼロ、denormal、丸め、極小値境界を含む。
- Gauss-Jordan: 対角・密・pivot交換・scaledの4系。long double CPU参照解、既知解、相対残差、消去後の単位行列を検証。

| RTL検証対象 | 結果 | latency min/avg/max | interval min/avg/max |
|---|---|---:|---:|
| add_custom | Pass | 2/2/2 | 3/3/3 |
| sub_custom | Pass | 2/2/2 | 3/3/3 |
| mul_custom | Pass | 1/1/1 | 2/2/2 |
| gj_native | Pass | 12880/12880/12880 | 12881/12881/12881 |
| gj_custom_both | Pass | 9690/9690/9690 | 9691/9691/9691 |
| gj_inline_both | Pass | 7610/7610/7610 | 7611/7611/7611 |

算術検証の実行記録:

- native_oracle: returncode=0 / FP64: 63072 bit-exact checks, 0 failures (native RNE + FPO FTZ, canonical NaN)
- amd_model: returncode=0 / FP64: 63072 bit-exact checks, 0 failures (AMD FPO model, canonical NaN)
- amd_boundary: returncode=0 / AMD model: 85920 checks, 1355 NaN comparisons, 13616 signed-zero comparisons, 0 failures

## 読み取り上の制約

Fmax・DSP/FF/LUTはHLS推定で、配置配線後のtimingや実機測定ではありません。clock=10ns、uncertainty=2.7nsで全条件を統一しました。1000/estimated periodが100MHz以上でも、uncertaintyを引いた7.3nsのスケジューリング目標や実機到達を保証しません。BIND_OPで短いcycle数を指定してもtiming違反がある場合、100MHzの有効な高速化と解釈できません。

元論文の演算器LUT数、Gauss-Jordanのlatency・BRAM・DSP数は完全には一致していません。資源共有、メモリ構成、演算器の実装、ツールのスケジュールが異なるためです。論文の表2にはBRAM列がなく、転記JSONではnullとしています。表3のBRAM列には18K単位の明記がありません。本設計のBRAM_18K列と同一単位とは断定していません。論文の最大1.63倍高速化の厳密な追試には、著者のソースと評価条件が必要です。

[全測定値 CSV](summary.csv) / [元論文の転記値 JSON](paper_reference.json) / [現在のソース SHA256](source_hashes.json) / [実行手順](../../../README.md)

[元論文の公式PDF](https://www.ipsj.or.jp/award/9faeag0000004f4v-att/C-003_3.pdf) 表1-3、[AMD 2025.2 CLI](https://docs.amd.com/r/2025.2-English/ug1399-vitis-hls/vitis-v-and-vitis-run-Commands)を参照。
