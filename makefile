# ============================================================
#  aviutl2-catalog CLI  —  Cross-platform Makefile
#  対応: Linux / macOS / Windows (MinGW / MSYS2 / クロスコンパイル)
#
#  使い方:
#    make                    自動検出してビルド
#    make PLATFORM=linux     Linux 向けにビルド
#    make PLATFORM=windows   Windows 向けにビルド (クロスコンパイル)
#    make linux              ↑ のショートカット
#    make win                ↑ のショートカット
#    make zig-linux          Zig CC で Linux 向けにビルド
#    make zig-win            Zig CC で Windows 向けにビルド
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

# ---------- プラットフォーム別設定 ----------
ifeq ($(PLATFORM),windows)
    TARGET  := $(TARGET_WIN)
    EXE_EXT := .exe
    # ネイティブ Windows (MSYS2) か クロスコンパイルか判定
    ifeq ($(OS),Windows_NT)
        # MSYS2 ネイティブ
        RM    := del /Q
        MKDIR := mkdir
    else
        # Linux/macOS からのクロスコンパイル
        ifndef ZIG_BUILD
            MINGW_PREFIX ?= x86_64-w64-mingw32
            CC           := $(MINGW_PREFIX)-gcc
        endif
        RM           := rm -f
        MKDIR        := mkdir -p
    endif
else
    # Linux / macOS
    TARGET  := $(TARGET_UNIX)
    RM      := rm -f
    MKDIR   := mkdir -p
    EXE_EXT :=
endif

# ---------- フラグ ----------
CFLAGS  = -std=c99 -O2 -Wall -Wextra -Wpedantic
CFLAGS += -Wno-unused-parameter

# ---------- ライブラリ (プラットフォーム別) ----------
ifeq ($(PLATFORM),windows)
    ifeq ($(OS),Windows_NT)
        # MSYS2 ネイティブ
        CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null)
        CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || echo -lcurl -lws2_32)
    else ifdef ZIG_BUILD
        # Zig クロスコンパイル — システムライブラリを使わない
        CURL_CFLAGS ?=
        CURL_LIBS   ?= -lcurl -lws2_32
    else ifdef CROSS_PKG_CONFIG
        CURL_CFLAGS ?= $(shell $(CROSS_PKG_CONFIG) --cflags libcurl 2>/dev/null)
        CURL_LIBS   ?= $(shell $(CROSS_PKG_CONFIG) --libs   libcurl 2>/dev/null)
    else
        CURL_CFLAGS ?=
        CURL_LIBS   ?= -lcurl -lws2_32
    endif
    LDFLAGS += $(CURL_LIBS) -lm
    ifneq ($(OS),Windows_NT)
        LDFLAGS += -lws2_32
    endif
else
    # Linux / macOS
    CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null)
    CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || curl-config --libs   2>/dev/null || echo -lcurl)
    LDFLAGS     += $(CURL_LIBS) -lm
endif

CFLAGS += $(CURL_CFLAGS)

# ---------- デバッグビルド ----------
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
    TARGET := $(basename $(TARGET))_debug$(EXE_EXT)
endif

# ---------- インストール先 ----------
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

# ============================================================
#  ターゲット
# ============================================================

.PHONY: all clean install uninstall help check-deps linux win zig-linux zig-win debug

ifeq ($(INSTALL),true)
all: check-deps $(TARGET) install
else ifeq ($(install),true)
all: check-deps $(TARGET) install
else
all: check-deps $(TARGET)
endif

# ---------- プラットフォーム指定ショートカット ----------
linux:
	$(MAKE) PLATFORM=linux

win:
	$(MAKE) PLATFORM=windows

# ---------- Zig CC ショートカット ----------
zig-linux:
	$(MAKE) PLATFORM=linux CC="zig cc"

zig-win:
	$(MAKE) PLATFORM=windows CC="zig cc -target x86_64-windows-gnu" ZIG_BUILD=1

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  ✔ ビルド完了: $(TARGET)  [platform=$(PLATFORM), cc=$(CC)]"
	@echo ""

# ---------- 依存チェック ----------
check-deps:
ifeq ($(PLATFORM),windows)
  ifeq ($(OS),Windows_NT)
	@where curl.exe >NUL 2>&1 || (echo "[ERROR] curl が見つかりません。MSYS2: pacman -S mingw-w64-x86_64-curl" && exit 1)
  else
	@which $(firstword $(CC)) >/dev/null 2>&1 || \
	    (echo "[ERROR] コンパイラ $(CC) が見つかりません。" && exit 1)
  endif
else
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
	$(RM) $(TARGET_UNIX) $(TARGET_UNIX)_debug $(TARGET_WIN) $(basename $(TARGET_WIN))_debug.exe 2>/dev/null; true
	@echo "  ✔ クリーン完了"

# ---------- インストール (Unix のみ) ----------
install:
ifneq ($(PLATFORM),windows)
	@if [ ! -f $(TARGET) ]; then \
		echo "  $(TARGET) が見つかりません。自動でビルドを開始します..."; \
		$(MAKE) $(TARGET); \
	fi
	$(MKDIR) $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET_UNIX)
	@echo "  ✔ インストール完了: $(DESTDIR)$(BINDIR)/$(TARGET_UNIX)"
else
	@echo "  Windows では手動でパスを通してください。"
endif

uninstall:
ifneq ($(PLATFORM),windows)
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
	@echo "  make win          Windows 向けにビルド (MinGW クロスコンパイル)"
	@echo "  make zig-linux    Zig CC で Linux 向けにビルド"
	@echo "  make zig-win      Zig CC で Windows 向けにビルド"
	@echo "  make debug        デバッグビルド (-g -O0)"
	@echo "  make clean        生成物を削除"
	@echo "  make install      /usr/local/bin にインストール"
	@echo "  make uninstall    インストールを取り消す"
	@echo "  make help         このヘルプ"
	@echo ""
	@echo "  変数:"
	@echo "    PLATFORM=linux|windows|macos  ターゲットプラットフォーム"
	@echo "    CC=clang                      コンパイラ変更"
	@echo "    MINGW_PREFIX=x86_64-w64-mingw32  MinGW プレフィックス"
	@echo "    PREFIX=/opt                   インストール先変更"
	@echo "    DEBUG=1                       デバッグビルド"
	@echo ""
	@echo "  検出プラットフォーム: $(PLATFORM)"
	@echo "  コンパイラ: $(CC)"
	@echo ""
