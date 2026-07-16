# AviUtl2 Catalog CLI (au2cat)
AviUtl2のプラグインやスクリプトを検索・導入・更新までコマンドラインから一括管理できるパッケージマネージャー
一応マルチプラットフォーム対応謳ってますが、まあWindowsじゃないとそもそもAviUtl2が動かないのであんまり意味はなさそうです。
## 使い方
ターミナル・コマンドプロンプト等から以下のコマンドを実行します。

```bash
# カタログ・本体の更新確認
au2cat update

# パッケージの検索 (名前 / 作者 / 概要 / タグ)
au2cat search <キーワード>

# パッケージの詳細情報
au2cat show <IDまたは名前>

# パッケージのインストール
au2cat install <IDまたは名前>

# パッケージのアンインストール
au2cat uninstall <IDまたは名前>

# インストール済みパッケージの一括更新
au2cat upgrade

# 全コマンドの一覧とヘルプ
au2cat help
```

---

## ダウンロード元の対応
現在以下のダウンロード元に対応しています（公式カタログと同様）：

- 直接ダウンロードURL
- GitHub Releases
- Google Drive
- BOOTH

---

## インストール・ビルド方法

### 実行ファイルのダウンロード

[Releases](https://github.com/Madotsukanai/aviutl2-catalog-cli/releases/latest) ページより、OSにあわせた実行ファイルをダウンロードしてください。  
ダウンロードしたファイルをPATHの通ったディレクトリに配置するか、ファイルのあるディレクトリから直接実行してください。

### ソースコードからのビルド

ビルドには `make`、Cコンパイラ（Linuxなら `cc`、Windowsのクロスコンパイルには `zig`）が必要です。  
※本プロジェクトは `miniz` 等を同梱しているため、追加の依存ライブラリは `libcurl` のみです。

```bash
# 現在のプラットフォーム向けにビルド
make

# クロスコンパイル (Zigが必要)
make windows
make linux

# システムにインストール (自動でOSごとの適切なパスに配置し、PATHを通します)
sudo make install
```

---

## 謝辞

本プロジェクトは、Neosku 様が開発された [AviUtl2 カタログ (GUI版)](https://github.com/Neosku/aviutl2-catalog) と、そのカタログデータリポジトリを参照・利用させて頂いています。

## ライセンス
本プロジェクトは [MIT License](./LICENSE) のもとで公開されています。
