#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
tools=${BUILD_TOOLS:-${ANDROID_HOME:?Set ANDROID_HOME}/build-tools/35.0.0}
platform=${ANDROID_JAR:-${ANDROID_HOME:?Set ANDROID_HOME}/platforms/android-35/android.jar}
out=${OUTPUT_DIR:-"$PWD/build"}
mkdir -p "$out/classes" "$out/dex"
zxing="$out/zxing-core-3.3.3.jar"
if [[ ! -f "$zxing" ]]; then
    curl -fLsS --retry 2 https://repo.maven.apache.org/maven2/com/google/zxing/core/3.3.3/core-3.3.3.jar -o "$zxing"
fi
echo "5820f81e943e4bce0329306621e2d6255d2930b0a6ce934c5c23c0d6d3f20599  $zxing" | sha256sum -c -
if [[ -n "${ECJ_JAR:-}" ]]; then
    java -jar "$ECJ_JAR" -source 1.8 -target 1.8 -bootclasspath "$platform:$tools/core-lambda-stubs.jar" -classpath "$zxing" -d "$out/classes" RemoteActivity.java ScanActivity.java
else
    javac -source 8 -target 8 -bootclasspath "$platform:$tools/core-lambda-stubs.jar" -classpath "$zxing" -d "$out/classes" RemoteActivity.java ScanActivity.java
fi
"$tools/d8" --min-api 24 --lib "$platform" --output "$out/dex" "$zxing" "$out"/classes/org/modulez/remote/preview/*.class
mkdir -p "$out/res/mipmap-xxxhdpi"
cp -R res/. "$out/res/"
cp ../../icons/modulez-player.png "$out/res/mipmap-xxxhdpi/modulez_player.png"
"$tools/aapt" package -f -A assets -S "$out/res" -M AndroidManifest.xml -I "$platform" -F "$out/unsigned.apk"
(cd "$out/dex" && zip -q "$out/unsigned.apk" classes.dex)
"$tools/zipalign" -f 4 "$out/unsigned.apk" "$out/aligned.apk"
key=${PREVIEW_KEYSTORE:-"$out/preview.jks"}
if [[ ! -e "$key" ]]; then
    keytool -genkeypair -keystore "$key" -alias preview -storepass android -keypass android \
        -keyalg RSA -keysize 2048 -validity 3650 -dname "CN=Modulez Remote Preview"
fi
apk="$out/Modulez-Remote-Preview.apk"
"$tools/apksigner" sign --ks "$key" --ks-pass pass:android --out "$apk" "$out/aligned.apk"
"$tools/apksigner" verify --verbose "$apk"
