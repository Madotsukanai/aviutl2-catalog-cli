# ============================================================
#  aviutl2-catalog CLI  —  Cross-platform Makefile
#  対応: Linux / macOS / Windows (Zig クロスコンパイル)
#
#  使い方:
#    make                    自動検出して現在のプラットフォーム向けにビルド
#    make linux              Linux 向けにビルド
#    make windows            Windows 向けにビルド (Zig)
#    make zig-linux          Zig CC で Linux 向けにビルド
#    make install            ビルドして各OSのパスが通る場所にインストール
# ============================================================

# ---------- ターゲット名 ----------
TARGET_UNIX = au2cat
TARGET_WIN  = au2cat.exe

# ---------- ソース ----------
SRC = src/main.c src/miniz.c src/lang.c

# ---------- OS 自動判定 (PLATFORM 未指定時) ----------
ifndef PLATFORM
  ifeq ($(OS),Windows_NT)
    PLATFORM := windows
  else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Darwin)
      PLATFORM := macos
    else
      PLATFORM := linux
    endif
  endif
endif

# ---------- コンパイラ自動選択 ----------
CC ?= cc
RM := rm -f
MKDIR := mkdir -p

# ---------- フラグ ----------
CFLAGS  = -std=c99 -O2 -Wall -Wextra -Wpedantic
CFLAGS += -Wno-unused-parameter -Wno-format-security

# ---------- プラットフォーム別設定 ----------
ifeq ($(PLATFORM),windows)
    # Windows は Zig ビルドを使用するため、出力パスは zig-out 配下になる
    TARGET := zig-out/bin/$(TARGET_WIN)
else
    # Linux / macOS
    TARGET := $(TARGET_UNIX)
    CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null)
    CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || curl-config --libs   2>/dev/null || echo -lcurl)
    LDFLAGS     += $(CURL_LIBS) -lm
    CFLAGS      += $(CURL_CFLAGS)
endif

# ---------- デバッグビルド ----------
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
    ifeq ($(PLATFORM),windows)
        # Windows の場合は Zig 側に任せるか、必要に応じて設定
    else
        TARGET := $(basename $(TARGET))_debug
    endif
endif

# ---------- インストール先 ----------
# MSYS2/WSL/Linux/macOS いずれでも基本は /usr/local/bin に入れば PATH が通る
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

# ============================================================
#  ターゲット
# ============================================================

.PHONY: all clean install uninstall help check-deps linux windows win zig-linux debug

# all: 現在のプラットフォーム向けにビルド
all: check-deps $(TARGET)

# ---------- プラットフォーム指定ショートカット ----------
linux:
	$(MAKE) PLATFORM=linux

windows:
	$(MAKE) PLATFORM=windows

win: windows

# ---------- Zig CC ショートカット ----------
zig-linux:
	$(MAKE) PLATFORM=linux CC="zig cc"

# ---------- ビルド実体 ----------
$(TARGET): $(SRC)
ifeq ($(PLATFORM),windows)
ifdef DEBUG
	zig build -Dtarget=x86_64-windows-gnu
else
	zig build -Dtarget=x86_64-windows-gnu --release=small
endif
	@echo ""
	@echo "  ✔ Windows向けビルド完了: $(TARGET)"
	@echo ""
else
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	@echo ""
	@echo "  ✔ ビルド完了: $(TARGET)  [platform=$(PLATFORM), cc=$(CC)]"
	@echo ""
endif

# ---------- 依存チェック ----------
check-deps:
ifneq ($(PLATFORM),windows)
	@which $(firstword $(CC)) >/dev/null 2>&1 || \
	    (echo "[ERROR] コンパイラ $(CC) が見つかりません。" && exit 1)
	@pkg-config --exists libcurl 2>/dev/null || curl-config --version >/dev/null 2>&1 || \
	    (echo "[ERROR] libcurl が見つかりません。" && \
	     echo "  Ubuntu/Debian : sudo apt install libcurl4-openssl-dev" && \
	     echo "  Fedora/RHEL   : sudo dnf install libcurl-devel" && \
	     echo "  macOS         : brew install curl" && \
	     exit 1)
endif

# ---------- デバッグビルド ----------
debug:
	$(MAKE) DEBUG=1

# ---------- クリーン ----------
clean:
	$(RM) $(TARGET_UNIX) $(TARGET_UNIX)_debug 2>/dev/null; true
	$(RM) -r .zig-cache zig-out zig-pkg 2>/dev/null; true
	@echo "  ✔ クリーン完了"

# ---------- インストール ----------
# ビルドしつつ、各OSのパスが通るディレクトリにインストールする
install: all
ifeq ($(PLATFORM),windows)
ifeq ($(OS),Windows_NT)
	@powershell -NoProfile -Command "$$bin = if ($$env:LOCALAPPDATA) { \"$$env:LOCALAPPDATA\au2cat\bin\" } else { \"$$env:USERPROFILE\au2cat\bin\" }; New-Item -ItemType Directory -Force -Path $$bin | Out-Null; Copy-Item -Path '$(TARGET)' -Destination \"$$bin\$(TARGET_WIN)\" -Force; $$p = [Environment]::GetEnvironmentVariable('PATH', 'User'); if ($$p -notmatch [regex]::Escape($$bin)) { [Environment]::SetEnvironmentVariable('PATH', $$p + ';' + $$bin, 'User'); Write-Host \"  ✔ Windowsのユーザー環境変数 PATH に $$bin を追加しました。ターミナルを再起動するとパスが通ります。\" }; Write-Host \"  ✔ Windows向けインストール完了: $$bin\$(TARGET_WIN)\""
else
	$(MKDIR) $(DESTDIR)$(BINDIR)
	cp $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET_WIN)
	chmod 755 $(DESTDIR)$(BINDIR)/$(TARGET_WIN)
	@echo "  ✔ Windows向けインストール完了 (クロスコンパイル): $(DESTDIR)$(BINDIR)/$(TARGET_WIN)"
endif
else
	$(MKDIR) $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET_UNIX)
	@echo "  ✔ インストール完了: $(DESTDIR)$(BINDIR)/$(TARGET_UNIX)"
endif

uninstall:
ifeq ($(PLATFORM),windows)
ifeq ($(OS),Windows_NT)
	@powershell -NoProfile -Command "$$bin = if ($$env:LOCALAPPDATA) { \"$$env:LOCALAPPDATA\au2cat\bin\" } else { \"$$env:USERPROFILE\au2cat\bin\" }; if (Test-Path \"$$bin\$(TARGET_WIN)\") { Remove-Item \"$$bin\$(TARGET_WIN)\" -Force; Write-Host \"  ✔ アンインストール完了\" } else { Write-Host \"  ℹ 見つかりませんでした\" }"
else
	$(RM) $(DESTDIR)$(BINDIR)/$(TARGET_WIN)
	@echo "  ✔ アンインストール完了"
endif
else
	$(RM) $(DESTDIR)$(BINDIR)/$(TARGET_UNIX)
	@echo "  ✔ アンインストール完了"
endif

# ---------- ヘルプ ----------
help:
	@echo ""
	@echo "  aviutl2-catalog CLI  Makefile"
	@echo "  ─────────────────────────────────────────"
	@echo "  make              自動検出してビルド"
	@echo "  make linux        Linux 向けにビルド"
	@echo "  make windows      Windows 向けにビルド (Zig)"
	@echo "  make zig-linux    Zig CC で Linux 向けにビルド"
	@echo "  make debug        デバッグビルド (-g -O0)"
	@echo "  make clean        生成物を削除"
	@echo "  make install      ビルドしてパスが通る場所にインストール"
	@echo "  make uninstall    インストールを取り消す"
	@echo "  make help         このヘルプ"
	@echo ""
	@echo "  変数:"
	@echo "    PLATFORM=linux|windows|macos  ターゲットプラットフォーム"
	@echo "    CC=clang                      コンパイラ変更"
	@echo "    PREFIX=/opt                   インストール先変更"
	@echo "    DEBUG=1                       デバッグビルド"
	@echo ""
	@echo "  検出プラットフォーム: $(PLATFORM)"
	@echo "  コンパイラ: $(CC)"
	@echo ""
