import re

with open("src/main.c", "r", encoding="utf-8") as f:
    content = f.read()

# Extract strings wrapped in _("...")
matches = re.findall(r'_\("((?:[^"\\]|\\.)*)"\)', content)
strings = list(set(matches))

# English translations mapping
en = {
    "使い方:\\n": "Usage:\\n",
    "コマンド:\\n": "Commands:\\n",
    "例:\\n": "Examples:\\n",
    "                    このヘルプ\\n": "                    This help\\n",
    "  search    <キーワード>    パッケージを検索 (名前 / 作者 / 概要 / タグ)\\n": "  search    <Keyword>       Search packages (Name / Author / Desc / Tag)\\n",
    "  list      [種類]         パッケージ一覧 (種類で絞込可)\\n": "  list      [Type]          List packages (Filter by Type)\\n",
    "  show      <ID|名前>      パッケージの詳細情報\\n": "  show      <ID|Name>       Package detailed info\\n",
    "  info      <ID|名前>      show の別名\\n": "  info      <ID|Name>       Alias for show\\n",
    "   <ID|名前> [-y] パッケージをインストール\\n": "   <ID|Name> [-y] Install package\\n",
    " <ID|名前> [-y] パッケージをアンインストール (remove/rm でも可)\\n": " <ID|Name> [-y] Uninstall package (or remove/rm)\\n",
    "    [--app-dir <パス>] [--plugins-dir <パス>]\\n": "    [--app-dir <Path>] [--plugins-dir <Path>]\\n",
    "                            インストール先設定の表示/変更\\n": "                            Show/Change installation paths\\n",
    "                    インストール済みパッケージを検出\\n": "                    Detect installed packages\\n",
    "                  更新可能なパッケージを一括更新\\n": "                  Upgrade all upgradable packages\\n",
    "                  ニコニ・コモンズIDをまとめてコピー用に出力\\n": "                  Output Niconi Commons IDs for copy\\n",
    "                    データベース統計\\n": "                    Database statistics\\n",
    "                    カタログを強制更新\\n": "                    Force update catalog\\n",
    "インストール機能について:\\n": "About Installation:\\n",
    "  - zip展開・ファイルコピーは全OSで動作します。\\n": "  - ZIP extraction/copy works on all OS.\\n",
    "  - exe実行 (run / run_auo_setup / extract_sfx) は Windows、または\\n": "  - Executing exe works on Windows, or\\n",
    "    非Windows環境で wine が見つかった場合のみ動作します。\\n": "    non-Windows with Wine.\\n",
    "  - appDir / pluginsDir は初回 install/uninstall 時に質問されます。\\n": "  - appDir/pluginsDir will be asked on first install/uninstall.\\n",
    "    au2cat config で確認・変更できます。\\n": "    Check/Change with au2cat config.\\n",
    "データ元:\\n": "Data Source:\\n",
    "キャッシュ:\\n": "Cache:\\n",
    "中止しました。\\n": "Aborted.\\n",
    "続行しますか?": "Continue?",
    "更新を続行しますか?": "Continue upgrade?",
    "インストール完了: %s (%s)\\n": "Install complete: %s (%s)\\n",
    "アンインストール完了: %s\\n": "Uninstall complete: %s\\n",
    "更新完了: %s\\n": "Upgrade complete: %s\\n",
    "  %s  (TTL: %d 分)\\n": "  %s  (TTL: %d min)\\n",
    "  %s <コマンド> [オプション]\\n\\n": "  %s <Command> [Options]\\n\\n",
    "  %s help でコマンド一覧を確認できます。\\n": "  Check command list with %s help.\\n",
    "  インストールされているパッケージが見つかりませんでした。\\n": "  No installed packages found.\\n",
    "  人気度: %ld\\n": "  Popularity: %ld\\n",
    "  対応するパッケージは見つかりませんでした。\\n": "  Matching packages not found.\\n",
    "  更新可能なパッケージはありません。\\n": "  No upgradable packages found.\\n",
    "  設定ファイル: %s\\n": "  Config file: %s\\n",
    "  該当するパッケージが見つかりませんでした。\\n": "  Relevant packages not found.\\n",
    " (最新: ": " (Latest: ",
    "(未設定)": "(Unset)",
    "(管理者権限が必要です)\\n": "(Administrator privileges required)\\n",
    "(非推奨) ": "(Deprecated) ",
    ": %ld  トレンド: %ld\\n": ": %ld  Trend: %ld\\n",
    ":: 警告: このパッケージは非推奨です\\n": ":: WARNING: This package is deprecated\\n",
    "AviUtl2 Catalog データベース統計\\n\\n": "AviUtl2 Catalog Database Statistics\\n\\n",
    "AviUtl2 のインストールフォルダのパスを入力してください: ": "Enter AviUtl2 installation folder path: ",
    "AviUtl2 のインストール先が設定されていません。初回設定を行います。\\n": "AviUtl2 installation path is not set. Performing initial setup.\\n",
    "ID を指定してください。\\n": "Please specify an ID.\\n",
    "Wine環境では管理者権限の昇格はサポートされません。通常権限で実行します。\\n": "Admin elevation not supported in Wine. Running with standard privileges.\\n",
    "[非推奨]": "[Deprecated]",
    "\\\"  %d 件\\n\\n": "\\\"  %d items\\n\\n",
    "\\nコモンズID一覧 (カンマ区切り):\\n": "\\nCommons ID List (comma separated):\\n",
    "\\n変更するには:\\n  au2cat config --app-dir <パス> [--plugins-dir <パス>]\\n": "\\nTo change:\\n  au2cat config --app-dir <Path> [--plugins-dir <Path>]\\n",
    "\\n計 %d 件のパッケージが検出されました。\\n": "\\nTotal %d packages detected.\\n",
    "au2cat install \\\"%s\\\" で利用可能": "Available via au2cat install \\\"%s\\\"",
    "au2cat: \\\"%s\\\" にはアンインストール情報がありません。\\n": "au2cat: \\\"%s\\\" has no uninstall info.\\n",
    "au2cat: \\\"%s\\\" には自動インストール情報がありません。手動でインストールしてください: %s\\n": "au2cat: \\\"%s\\\" lacks auto-install info. Please install manually: %s\\n",
    "au2cat: \\\"%s\\\" は複数のパッケージに該当します:\\n": "au2cat: \\\"%s\\\" matches multiple packages:\\n",
    "au2cat: extract: ダウンロード済みファイルがありません\\n": "au2cat: extract: No downloaded file found\\n",
    "au2cat: extract_sfx には Windows または Wine が必要です。スキップします: %s\\n": "au2cat: extract_sfx requires Windows or Wine. Skipping: %s\\n",
    "au2cat: extract_sfx: ダウンロード済みファイルがありません\\n": "au2cat: extract_sfx: No downloaded file found\\n",
    "au2cat: zipを開けません: %s\n": "au2cat: Cannot open zip: %s\n",
    "au2cat: unzip を実行できませんでした\n": "au2cat: Cannot run unzip\n",
    "au2cat: このアクションには Windows または Wine が必要です。スキップします: %s\\n": "au2cat: This action requires Windows or Wine. Skipping: %s\\n",
    "au2cat: コピー元が見つかりません: %s\\n": "au2cat: Copy source not found: %s\\n",
    "au2cat: ダウンロードに失敗しました\\n": "au2cat: Download failed\\n",
    "au2cat: ダウンロードエラー: %s\\n": "au2cat: Download error: %s\\n",
    "au2cat: パッケージ \\\"%s\\\" が見つかりません。\\n": "au2cat: Package \\\"%s\\\" not found.\\n",
    "au2cat: パースに失敗しました\\n": "au2cat: Parse failed\\n",
    "au2cat: パースエラー\\n": "au2cat: Parse error\\n",
    "au2cat: 不明なオプション '%s'\\n": "au2cat: Unknown option '%s'\\n",
    "au2cat: 不明なコマンドまたはオプション '%s'\\n": "au2cat: Unknown command or option '%s'\\n",
    "au2cat: 取得に失敗しました\\n": "au2cat: Fetch failed\\n",
    "au2cat: 展開に失敗: %s\\n": "au2cat: Extraction failed: %s\\n",
    "au2cat: 更新に失敗しました\\n": "au2cat: Update failed\\n",
    "アンインストールに失敗しました: %s\\n": "Uninstall failed: %s\\n",
    "インストール": "Install",
    "インストールに失敗しました: %s\\n": "Install failed: %s\\n",
    "インストール済み (%s)\\n": "Installed (%s)\\n",
    "インストール済みのパッケージを検出しています...\\n\\n": "Detecting installed packages...\\n\\n",
    "インストール済みパッケージのニコニ・コモンズIDを抽出しています...\\n\\n": "Extracting Niconi Commons IDs of installed packages...\\n\\n",
    "インストール状態": "Install Status",
    "カタログを取得しています... (%s)\\n": "Fetching catalog... (%s)\\n",
    "キャッシュから読み込みました (%d 件)\\n": "Loaded from cache (%d items)\\n",
    "コピー中: %s -> %s\\n": "Copying: %s -> %s\\n",
    "タグ": "Tags",
    "ダウンロード中: %s\\n": "Downloading: %s\\n",
    "ダウンロード完了: %s\\n": "Download complete: %s\\n",
    "データベースを更新しています...\\n": "Updating database...\\n",
    "データベースを更新しました (%d 件のパッケージ)\\n": "Database updated (%d packages)\\n",
    "バージョン": "Version",
    "パッケージ一覧 (全種類)  %d 件\\n\\n": "Package List (All Types)  %d items\\n\\n",
    "パッケージ一覧 [種類: %s]  %d 件\\n\\n": "Package List [Type: %s]  %d items\\n\\n",
    "プラグインフォルダのパス [既定: %s]: ": "Plugin folder path [Default: %s]: ",
    "人気トップ %d:\\n": "Top Popularity %d:\\n",
    "人気度": "Popularity",
    "以下のパッケージが更新されます:\\n": "The following packages will be upgraded:\\n",
    "以下のパッケージをアンインストールします: ": "The following package will be uninstalled: ",
    "以下のパッケージをインストールします: ": "The following package will be installed: ",
    "作者": "Author",
    "使い方: au2cat install <ID または 名前> [-y]\\n": "Usage: au2cat install <ID or Name> [-y]\\n",
    "使い方: au2cat search <キーワード>\\n": "Usage: au2cat search <Keyword>\\n",
    "使い方: au2cat show <ID または 名前>\\n": "Usage: au2cat show <ID or Name>\\n",
    "使い方: au2cat uninstall <ID または 名前> [-y]\\n": "Usage: au2cat uninstall <ID or Name> [-y]\\n",
    "依存": "Dependencies",
    "削除中: %s\\n": "Deleting: %s\\n",
    "名前": "Name",
    "実行中: %s\\n": "Executing: %s\\n",
    "展開中: %s\\n": "Extracting: %s\\n",
    "更新に失敗しました: %s\\n": "Upgrade failed: %s\\n",
    "更新中: %s\\n": "Upgrading: %s\\n",
    "更新可能なパッケージを確認しています...\\n\\n": "Checking for upgradable packages...\\n\\n",
    "更新完了  —  %d 件のパッケージ\\n": "Upgrade complete  —  %d packages\\n",
    "未インストール (ファイル未検出)\\n": "Not installed (File not found)\\n",
    "検出情報なし (ハッシュ未登録)\\n": "No detection info (Hash unregistered)\\n",
    "検索結果: \\\"": "Search Results: \\\"",
    "現在の設定:\\n": "Current Config:\\n",
    "種類": "Type",
    "種類別:\\n": "By Type:\\n",
    "総パッケージ数  : ": "Total Packages  : ",
    "総人気度        : ": "Total Popularity: ",
    "自動インストール情報なし (上記URLから手動で入手してください)\\n": "No auto-install info (Get manually from URL above)\\n",
    "自己解凍書庫を展開しています (7-Zip SFX 想定。Windows/Wine使用)...\\n": "Extracting SFX archive (Windows/Wine required)...\\n",
    "設定を保存しました: ": "Config saved: ",
    "設定を保存しました。\\n": "Config saved.\\n",
    "詳細は  au2cat show <名前 または ID>  で確認できます。\\n": "Details can be found with au2cat show <Name or ID>.\\n",
    "非推奨          : ": "Deprecated      : ",
    
    # Types and licenses
    "その他": "Other",
    "オブジェクト": "Object",
    "カスタムライセンス": "Custom License",
    "スクリプト": "Script",
    "スクリプトモジュール": "Script Module",
    "ニコニ・コモンズ": "Niconi Commons",
    "フィルタプラグイン": "Filter Plugin",
    "言語ファイル": "Language File",
    "出力プラグイン": "Export Plugin",
    "独自ライセンス": "Custom License",
    "入力プラグイン": "Input Plugin",
    "汎用プラグイン": "Generic Plugin",
    "不明": "Unknown",
    "本体": "Core",
    
    # New prompts
    "AviUtl2 のインストールフォルダのパス [既定: %s]: ": "AviUtl2 install folder path [Default: %s]: ",
    "パスは空にできません。\\n": "Path cannot be empty.\\n",
    
    # Dependencies
    "%s は既に最新版がインストールされています。\\n": "%s is already installed with the latest version.\\n",
    "au2cat: 依存パッケージ %s のインストールに失敗しました。\\n": "au2cat: Failed to install dependency package %s.\\n",
    "au2cat: 依存パッケージ %s が見つかりません。\\n": "au2cat: Dependency package %s not found.\\n",
    
    # Uninstall fallback
    "au2cat: パッケージ \"%s\" はインストールされていません。\\n": "au2cat: Package \"%s\" is not installed.\\n",
    "削除中: %s\\n": "Deleting: %s\\n",
    
    "インストール先: %s\\n": "Install to: %s\\n",
    
    "スクリプトフォルダのパス [既定: %s]: ": "Script folder path [Default: %s]: ",
    
    # Extract installer directly
    "au2cat: 7zコマンドでの展開に失敗しました。7zがインストールされているか確認してください。\n": "au2cat: Failed to extract using 7z command. Please check if 7z is installed.\n",
    "インストーラーを直接展開しています (7-Zip使用)...\n": "Extracting installer directly (using 7-Zip)...\n",
    "7zでの展開に失敗しました。exeインストーラーを通常起動します...\n": "Extraction with 7z failed. Running exe installer normally...\n"
}

ko = {
    "使い方:\\n": "사용법:\\n",
    "コマンド:\\n": "명령어:\\n",
    "例:\\n": "예시:\\n",
    "                    このヘルプ\\n": "                    도움말\\n"
}

zh_cn = {
    "使い方:\\n": "用法:\\n",
    "コマンド:\\n": "命令:\\n",
    "例:\\n": "示例:\\n",
    "                    このヘルプ\\n": "                    此帮助\\n"
}

zh_tw = {
    "使い方:\\n": "用法:\\n",
    "コマンド:\\n": "指令:\\n",
    "例:\\n": "範例:\\n",
    "                    このヘルプ\\n": "                    此幫助\\n"
}

c_code = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lang.h"

#ifdef _WIN32
#include <windows.h>
#endif

typedef struct {
    const char *key;
    const char *val;
} LangEntry;

"""

def escape_c(s):
    s = s.replace('\\', '\\\\')
    s = s.replace('"', '\\"')
    s = s.replace('\n', '\\n')
    s = s.replace('\r', '\\r')
    s = s.replace('\t', '\\t')
    return s

dicts = [("en", en), ("ko", ko), ("zh_cn", zh_cn), ("zh_tw", zh_tw)]

for lang_code, d in dicts:
    c_code += f"static const LangEntry dict_{lang_code}[] = {{\n"
    all_keys = sorted(list(set(strings) | set(d.keys())))
    for s in all_keys:
        trans = d.get(s, s)
        if trans != s:
            c_code += f'    {{"{escape_c(s)}", "{escape_c(trans)}"}},\n'
    c_code += "    {NULL, NULL}\n};\n\n"

c_code += """static const LangEntry *current_dict = NULL;
static int lang_initialized = 0;

void lang_init(void) {
    if (lang_initialized) return;
    lang_initialized = 1;
    
    char lang_buf[32] = {0};
    const char *env_lang = getenv("AU2CAT_LANG");
    if (!env_lang) env_lang = getenv("LANG");
    
    if (env_lang) {
        strncpy(lang_buf, env_lang, sizeof(lang_buf)-1);
    } else {
#ifdef _WIN32
        LANGID id = GetUserDefaultUILanguage();
        switch (PRIMARYLANGID(id)) {
            case LANG_ENGLISH: strcpy(lang_buf, "en"); break;
            case LANG_KOREAN:  strcpy(lang_buf, "ko"); break;
            case LANG_CHINESE: 
                if (SUBLANGID(id) == SUBLANG_CHINESE_TRADITIONAL) strcpy(lang_buf, "zh_TW");
                else strcpy(lang_buf, "zh_CN");
                break;
            case LANG_JAPANESE: strcpy(lang_buf, "ja"); break;
        }
#endif
    }
    
    if (lang_buf[0]) {
        if (strncmp(lang_buf, "en", 2) == 0) current_dict = dict_en;
        else if (strncmp(lang_buf, "ko", 2) == 0) current_dict = dict_ko;
        else if (strncmp(lang_buf, "zh_TW", 5) == 0 || strncmp(lang_buf, "zh-TW", 5) == 0) current_dict = dict_zh_tw;
        else if (strncmp(lang_buf, "zh", 2) == 0) current_dict = dict_zh_cn;
    }
}

const char * lang_get(const char *key) {
    if (!lang_initialized) lang_init();
    if (!current_dict) return key;
    
    for (int i = 0; current_dict[i].key != NULL; i++) {
        if (strcmp(current_dict[i].key, key) == 0) {
            return current_dict[i].val;
        }
    }
    return key;
}
"""

with open("src/lang.c", "w", encoding="utf-8") as f:
    f.write(c_code)
