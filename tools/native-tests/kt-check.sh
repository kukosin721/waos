#!/usr/bin/env bash
# Tipado rápido (sin Android SDK): compila los archivos de audio, EQ y búsqueda por
# letra junto a stubs mínimos de Android/Media3 con kotlinc.
#
#   KOTLINC=~/kotlinc/bin/kotlinc ./tools/native-tests/kt-check.sh
#
# No reemplaza `./gradlew test`: sólo detecta errores de tipos/API en la frontera
# entre el motor nativo y Media3 cuando no hay SDK disponible.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
KOTLINC="${KOTLINC:-kotlinc}"
OUT="${OUT:-$HERE/build/kt-check}"

mkdir -p "$OUT"
# Las corrutinas vienen con el compilador (kotlinx-coroutines-core-jvm.jar): así los
# stubs no tienen que inventar Flow/MutableStateFlow, que es donde se esconden los errores.
COROUTINES="$(dirname "$(command -v "$KOTLINC")")/../lib/kotlinx-coroutines-core-jvm.jar"
CP=""
if [ -f "$COROUTINES" ]; then CP="-cp $COROUTINES"; fi
"$KOTLINC" -nowarn $CP \
  "$ROOT"/tools/kt-check/stubs/*.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/audio/AudioEngine.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/audio/OboeAudioOutput.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/audio/OboeAudioOutputProvider.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/eq/Biquad.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/eq/EqModel.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/eq/EqEngine.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/data/analysis/LyricsIndex.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/data/DiskFingerprint.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/data/LibraryCacheStore.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/playback/DacHardwareVolume.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/playback/FlacDecoderBridge.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/playback/FlacPcmExtractor.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/playback/LocalFlacMediaSourceFactory.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/playback/ContinuityPreset.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/data/StartupCache.kt \
  "$ROOT"/app/src/main/java/com/kukosin/app/playback/UsbDacManager.kt \
  -d "$OUT"
echo "Sin errores de tipado."
