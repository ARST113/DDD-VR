#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

log() {
  printf '\n[DDD-VR bootstrap] %s\n' "$*"
}

fail() {
  printf '\n[DDD-VR bootstrap][ERROR] %s\n' "$*" >&2
  exit 1
}

log "Project root: $PROJECT_ROOT"

# --------------------------------------------------------------------
# Java 17
# --------------------------------------------------------------------
if command -v java >/dev/null 2>&1; then
  JAVA_BIN="$(readlink -f "$(command -v java)")"
  export JAVA_HOME="$(dirname "$(dirname "$JAVA_BIN")")"
elif [ -x "/usr/lib/jvm/java-17-openjdk-amd64/bin/java" ]; then
  export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
elif [ -x "/usr/lib/jvm/msopenjdk-current/bin/java" ]; then
  export JAVA_HOME="/usr/lib/jvm/msopenjdk-current"
elif [ -x "/usr/local/sdkman/candidates/java/current/bin/java" ]; then
  export JAVA_HOME="/usr/local/sdkman/candidates/java/current"
else
  if command -v apt-get >/dev/null 2>&1; then
    log "Java 17 not found. Installing openjdk-17-jdk..."
    sudo apt-get update || true
    sudo apt-get install -y openjdk-17-jdk
    export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
  else
    fail "Java 17 not found and apt-get is unavailable. Use a Java 17 devcontainer or GitHub Actions runner."
  fi
fi

export PATH="$JAVA_HOME/bin:$PATH"

log "JAVA_HOME=$JAVA_HOME"
java -version || fail "Java is not executable."

# --------------------------------------------------------------------
# Android SDK root
# --------------------------------------------------------------------
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/android-sdk}}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
mkdir -p "$ANDROID_HOME"

# Common SDK locations in CI/devcontainers.
CANDIDATE_SDKMANAGERS=(
  "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
  "$ANDROID_HOME/cmdline-tools/bin/sdkmanager"
  "$ANDROID_HOME/tools/bin/sdkmanager"
  "/opt/android-sdk/cmdline-tools/latest/bin/sdkmanager"
  "/usr/local/lib/android/sdk/cmdline-tools/latest/bin/sdkmanager"
  "/usr/local/android-sdk/cmdline-tools/latest/bin/sdkmanager"
)

find_sdkmanager() {
  if command -v sdkmanager >/dev/null 2>&1; then
    command -v sdkmanager
    return 0
  fi

  local candidate
  for candidate in "${CANDIDATE_SDKMANAGERS[@]}"; do
    if [ -x "$candidate" ]; then
      echo "$candidate"
      return 0
    fi
  done

  return 1
}

SDKMANAGER="$(find_sdkmanager || true)"

# --------------------------------------------------------------------
# Dependencies for download
# --------------------------------------------------------------------
if [ -z "$SDKMANAGER" ]; then
  if ! command -v curl >/dev/null 2>&1; then
    if command -v apt-get >/dev/null 2>&1; then
      sudo apt-get update || true
      sudo apt-get install -y curl
    else
      fail "curl is missing and apt-get is unavailable."
    fi
  fi

  if ! command -v unzip >/dev/null 2>&1; then
    if command -v apt-get >/dev/null 2>&1; then
      sudo apt-get update || true
      sudo apt-get install -y unzip
    else
      fail "unzip is missing and apt-get is unavailable."
    fi
  fi
fi

# --------------------------------------------------------------------
# Install command line tools if missing
# --------------------------------------------------------------------
if [ -z "$SDKMANAGER" ]; then
  log "sdkmanager not found. Installing Android command line tools..."

  rm -rf /tmp/dddvr-android-sdk-tools
  mkdir -p /tmp/dddvr-android-sdk-tools
  cd /tmp/dddvr-android-sdk-tools

  # Prefer project-provided GitHub release cache first, then official Google mirrors.
  CMDLINE_TOOLS_URLS=(
    "https://github.com/ARST113/DDD-VR/releases/download/sdk-cache-v1/commandlinetools-linux-14742923_latest.zip"
    "https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip"
    "https://dl.google.com/android/repository/commandlinetools-linux-10406996_latest.zip"
    "https://dl.google.com/android/repository/commandlinetools-linux-9477386_latest.zip"
  )

  DOWNLOADED=0

  for url in "${CMDLINE_TOOLS_URLS[@]}"; do
    log "Trying download: $url"

    if curl -fL --retry 3 --retry-delay 2 \
      -A "Mozilla/5.0 DDD-VR Android SDK Bootstrap" \
      -o commandlinetools.zip "$url"; then
      DOWNLOADED=1
      break
    fi

    rm -f commandlinetools.zip
  done

  if [ "$DOWNLOADED" != "1" ]; then
    cat >&2 <<MSG

[DDD-VR bootstrap][ERROR]
Failed to download Android command line tools from GitHub Release and dl.google.com.

This is an environment/network limitation, not a DDD-VR source-code error.

Options:
1. Run the checks in GitHub Actions.
2. Run the checks in Codespaces after rebuilding the devcontainer.
3. Provide a preinstalled Android SDK and set:
   ANDROID_HOME=/path/to/android-sdk
   ANDROID_SDK_ROOT=/path/to/android-sdk
4. If using Codex, tell it:
   "If dl.google.com returns 403, do not mark the code as broken.
    Commit the code and rely on GitHub Actions/Codespaces for final verification."

MSG
    exit 42
  fi

  unzip -q commandlinetools.zip

  rm -rf "$ANDROID_HOME/cmdline-tools/latest"
  mkdir -p "$ANDROID_HOME/cmdline-tools/latest"
  mv cmdline-tools/* "$ANDROID_HOME/cmdline-tools/latest/"

  SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
fi

[ -x "$SDKMANAGER" ] || fail "sdkmanager still not executable: $SDKMANAGER"

export PATH="$(dirname "$SDKMANAGER"):$ANDROID_HOME/platform-tools:$PATH"

log "ANDROID_HOME=$ANDROID_HOME"
log "SDKMANAGER=$SDKMANAGER"

cd "$PROJECT_ROOT"

# --------------------------------------------------------------------
# Detect compileSdk
# --------------------------------------------------------------------
COMPILE_SDK="$(
  grep -RhoE 'compileSdk[[:space:]]*=?[[:space:]]*[0-9]+' app/build.gradle.kts app/build.gradle 2>/dev/null \
  | grep -oE '[0-9]+' \
  | head -1 || true
)"

if [ -z "$COMPILE_SDK" ]; then
  COMPILE_SDK="36"
fi

log "compileSdk=$COMPILE_SDK"

yes | "$SDKMANAGER" --licenses >/dev/null || true

"$SDKMANAGER" --install \
  "platform-tools" \
  "platforms;android-${COMPILE_SDK}" \
  "build-tools;36.0.0" \
  "build-tools;35.0.0"

cat > local.properties <<PROPS
sdk.dir=$ANDROID_HOME
PROPS

log "Created local.properties:"
cat local.properties

log "Android SDK bootstrap completed."
