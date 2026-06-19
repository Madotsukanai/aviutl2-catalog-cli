# ============================================================
#  aviutl2-catalog CLI  —  Cross-platform Makefile
#  対応: Linux / macOS / Windows (MinGW / MSYS2)
# ============================================================

# ---------- ターゲット名 ----------
TARGET_UNIX = au2cat
TARGET_WIN  = au2cat.exe

# ---------- ソース ----------
SRC = main.c

# ---------- コンパイラ自動選択 ----------
CC ?= gcc

# ---------- OS 判定 ----------
ifeq ($(OS),Windows_NT)
    PLATFORM   := windows
    TARGET     := $(TARGET_WIN)
    RM         := del /Q
    MKDIR      := mkdir
    EXE_EXT    := .exe
else
    UNAME := $(shell uname -s)
    ifeq ($(UNAME),Darwin)
        PLATFORM := macos
    else
        PLATFORM := linux
    endif
    TARGET  := $(TARGET_UNIX)
    RM      := rm -f
    MKDIR   := mkdir -p
    EXE_EXT :=
endif

# ---------- フラグ ----------
CFLAGS  = -std=c99 -O2 -Wall -Wextra -Wpedantic
CFLAGS += -Wno-unused-parameter

# curl フラグ
ifeq ($(PLATFORM),windows)
    # MinGW / MSYS2: pacman -S mingw-w64-x86_64-curl
    CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null)
    CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || echo -lcurl -lws2_32)
    LDFLAGS     += $(CURL_LIBS) -lm
else ifeq ($(PLATFORM),macos)
    # Homebrew: brew install curl
    CURL_CFLAGS ?= $(shell pkg-config --cflags libcurl 2>/dev/null || curl-config --cflags 2>/dev/null)
    CURL_LIBS   ?= $(shell pkg-config --libs   libcurl 2>/dev/null || curl-config --libs   2>/dev/null || echo -lcurl)
    LDFLAGS     += $(CURL_LIBS) -lm
else
    # Linux: apt install libcurl4-openssl-dev  /  dnf install libcurl-devel
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

.PHONY: all clean install uninstall help check-deps

all: check-deps $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  ✔ ビルド完了: $(TARGET)  [platform=$(PLATFORM)]"
	@echo ""

# ---------- 依存チェック ----------
check-deps:
ifeq ($(PLATFORM),windows)
	@where curl.exe >NUL 2>&1 || (echo "[ERROR] curl が見つかりません。MSYS2: pacman -S mingw-w64-x86_64-curl" && exit 1)
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
ifeq ($(PLATFORM),windows)
	-$(RM) $(TARGET_WIN) $(basename $(TARGET_WIN))_debug.exe
else
	$(RM) $(TARGET_UNIX) $(TARGET_UNIX)_debug
endif
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
	@echo "  make              ビルド"
	@echo "  make debug        デバッグビルド (-g -O0)"
	@echo "  make clean        生成物を削除"
	@echo "  make install      /usr/local/bin にインストール"
	@echo "  make uninstall    インストールを取り消す"
	@echo "  make help         このヘルプ"
	@echo ""
	@echo "  変数:"
	@echo "    CC=clang        コンパイラ変更"
	@echo "    PREFIX=/opt     インストール先変更"
	@echo "    DEBUG=1         デバッグビルド"
	@echo ""
	@echo "  検出プラットフォーム: $(PLATFORM)"
	@echo ""
