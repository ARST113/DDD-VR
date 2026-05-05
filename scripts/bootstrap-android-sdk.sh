#!/usr/bin/env bash
set -eo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

echo "Project root: $PROJECT_ROOT"

if command -v java >/dev/null 2>&1; then
  JAVA_BIN="$(readlink -f "$(command -v java)")"
  export JAVA_HOME="$(dirname "$(dirname "$JAVA_BIN")")"
elif [ -x "/usr/lib/jvm/java-17-openjdk-amd64/bin/java" ]; then
  export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
elif [ -x "/usr/local/sdkman/candidates/java/current/bin/java" ]; then
  export JAVA_HOME="/usr/local/sdkman/candidates/java/current"
else
  echo "Java 17 not found. Installing openjdk-17-jdk..."
  sudo apt-get update
  sudo apt-get install -y openjdk-17-jdk
  export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-amd64"
fi

export ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"

echo "JAVA_HOME=$JAVA_HOME"
java -version

echo "ANDROID_HOME=$ANDROID_HOME"
mkdir -p "$ANDROID_HOME/cmdline-tools"

if ! command -v curl >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y curl
fi

if ! command -v unzip >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y unzip
fi

if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
  echo "Installing Android command line tools..."

  rm -rf /tmp/dddvr-android-sdk-tools
  mkdir -p /tmp/dddvr-android-sdk-tools
  cd /tmp/dddvr-android-sdk-tools

  curl -fL -o commandlinetools.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
  unzip -q commandlinetools.zip

  rm -rf "$ANDROID_HOME/cmdline-tools/latest"
  mkdir -p "$ANDROID_HOME/cmdline-tools/latest"
  mv cmdline-tools/* "$ANDROID_HOME/cmdline-tools/latest/"
fi

cd "$PROJECT_ROOT"

COMPILE_SDK="$(
  grep -RhoE 'compileSdk[[:space:]]*=?[[:space:]]*[0-9]+' app/build.gradle.kts app/build.gradle 2>/dev/null \
  | grep -oE '[0-9]+' \
  | head -1 || true
)"

if [ -z "$COMPILE_SDK" ]; then
  COMPILE_SDK="36"
fi

echo "compileSdk=$COMPILE_SDK"

yes | sdkmanager --licenses >/dev/null || true

sdkmanager --install \
  "platform-tools" \
  "platforms;android-${COMPILE_SDK}" \
  "build-tools;36.0.0" \
  "build-tools;35.0.0"

cat > local.properties <<PROPS
sdk.dir=$ANDROID_HOME
PROPS

echo "Created local.properties:"
cat local.properties

echo "Android SDK bootstrap completed."
