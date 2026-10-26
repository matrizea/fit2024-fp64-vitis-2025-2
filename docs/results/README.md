# 公開時の測定データ

このフォルダは2026-10-26に検証したII=1版の保存値です。再実行スクリプトはルートの`results/`へ出力し、このフォルダは上書きしません。

| ファイル | 内容 |
|---|---|
| [summary.json](summary.json)、[summary.csv](summary.csv) | 全27条件のCsim・合成・RTL集計 |
| [ii_verification.json](ii_verification.json) | 単体II=1と各トランザクション間隔の確認 |
| [arithmetic_tests.json](arithmetic_tests.json) | 算術4 suiteの終了値と入力ハッシュ |
| [source_hashes.json](source_hashes.json) | 評価時点のソース・テスト・実行スクリプトのハッシュ |
| [paper_reference.json](paper_reference.json) | 論文の表2・表3の転記値 |
| [latency_comparison.json](latency_comparison.json) | HLS生値と開始サイクルを含む比較規約 |
| [optimization.json](optimization.json) | 論文・初版・最終版の比較 |
| [baseline.json](baseline.json) | 最適化比較スクリプトが使う初版3条件の測定値 |
| [environment.json](environment.json) | ツール、part、clockなどの実行条件 |

保存値の`build/...`は評価当時の生成パスです。[evaluation-2026-10-26タグ](https://github.com/matrizea/fit2024-fp64-vitis-2025-2/tree/evaluation-2026-10-26)に完全な評価時の構成があり、生レポートは`reports/ii1/`、候補は`experiments/`、過去版は`history/`にあります。`environment.json`中の候補パスもこのタグ内を指します。

`source_hashes.json`は評価時点のスクリプトも含みます。mainではファイル整理に伴い集計先と参照先を変更しているため、スクリプトのハッシュは異なります。`src/`と`tests/`は評価時と同一です。ローカルの絶対パスは相対パスへ置き換え、実行コマンドと生ログは同梱していません。
