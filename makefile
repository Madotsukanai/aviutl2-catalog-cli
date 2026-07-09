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
# ============================================================

# ---------- ターゲット名 ----------
TARGET_UNIX = au2cat
TARGET_WIN  = au2cat.exe

# ---------- ソース ----------
SRC = main.c

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

# ---------- プラットフォーム別設定 ----------
ifeq ($(PLATFORM),windows)
    TARGET  := $(TARGET_WIN)
    EXE_EXT := .exe
    # ネイティブ Windows (MSYS2) か クロスコンパイルか判定
    ifeq ($(OS),Windows_NT)
        # MSYS2 ネイティブ
        CC    ?= gcc
        RM    := del /Q
        MKDIR := mkdir
        MINGW_PREFIX ?=
    else
        # Linux/macOS からのクロスコンパイル
        MINGW_PREFIX ?= x86_64-w64-mingw32
        CC           := $(MINGW_PREFIX)-gcc
        RM           := rm -f
        MKDIR        := mkdir -p
        # クロスコンパイル用 pkg-config
        PKG_CONFIG   ?= $(MINGW_PREFIX)-pkg-config
        ifneq ($(shell which $(PKG_CONFIG) 2>/dev/null),)
            CROSS_PKG_CONFIG := $(PKG_CONFIG)
        else
            CROSS_PKG_CONFIG :=
        endif
    endif
else
    # Linux / macOS
    CC      ?= gcc
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
        ZIP_CFLAGS  ?= $(shell pkg-config --cflags libzip  2>/dev/null)
        ZIP_LIBS    ?= $(shell pkg-config --libs   libzip  2>/dev/null || echo -lzip)
    else ifdef CROSS_PKG_CONFIG
        # クロスコンパイル (pkg-config あり)
        CURL_CFLAGS ?= $(shell $(CROSS_PKG_CONFIG) --cflags libcurl 2>/dev/null)
        CURL_LIBS   ?= $(shell $(CROSS_PKG_CONFIG) --libs   libcurl 2>/dev/null)
        ZIP_CFLAGS  ?= $(shell $(CROSS_PKG_CONFIG) --cflags libzip  2>/dev/null)
        ZIP_LIBS    ?= $(shell $(CROSS_PKG_CONFIG) --libs   libzip  2>/dev/null)
    else
        # クロスコンパイル (pkg-config なし — 手動指定向け)
        CURL_CFLAGS ?=
        CURL_LIBS   ?= -lcurl -lws2_32
        ZIP_CFLAGS  ?=
        ZIP_LIBS    ?= -lzip
    endif
    LDFLAGS += $(CURL_LIBS) $(ZIP_LIBS) -lm
    ifeq ($(OS),Windows_NT)
    else
        # クロスコンパイル時は Winsock 等を追加
        LDFLAGS += -lws2_32
    endif
else ifeq ($(PLATFORM),macos)
    CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null)
    CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || curl-config --libs   2>/dev/null || echo -lcurl)
    ZIP_CFLAGS  ?= $(shell pkg-config --cflags libzip  2>/dev/null)
    ZIP_LIBS    ?= $(shell pkg-config --libs   libzip  2>/dev/null || echo -lzip)
    LDFLAGS     += $(CURL_LIBS) $(ZIP_LIBS) -lm
else
    # Linux
    CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null)
    CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || curl-config --libs   2>/dev/null || echo -lcurl)
    ZIP_CFLAGS  ?= $(shell pkg-config --cflags libzip  2>/dev/null)
    ZIP_LIBS    ?= $(shell pkg-config --libs   libzip  2>/dev/null || echo -lzip)
    LDFLAGS     += $(CURL_LIBS) $(ZIP_LIBS) -lm
endif

CFLAGS += $(CURL_CFLAGS) $(ZIP_CFLAGS)

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

.PHONY: all clean install uninstall help check-deps linux win debug

all: check-deps $(TARGET)

# ---------- プラットフォーム指定ショートカット ----------
linux:
	$(MAKE) PLATFORM=linux

win:
	$(MAKE) PLATFORM=windows

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
	@which $(CC) >/dev/null 2>&1 || \
	    (echo "[ERROR] クロスコンパイラ $(CC) が見つかりません。" && \
	     echo "  Ubuntu/Debian : sudo apt install gcc-mingw-w64-x86-64" && \
	     echo "  Fedora        : sudo dnf install mingw64-gcc" && \
	     exit 1)
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
install: $(TARGET)
ifneq ($(PLATFORM),windows)
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
	@echo "  make win          Windows 向けにビルド (クロスコンパイル)"
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
