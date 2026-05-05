# DDD-VR Codex instructions

This is an Android/Kotlin project.

Before running Gradle checks in Codex, Codespaces, or any fresh Linux container, run:

    bash scripts/bootstrap-android-sdk.sh

Then run:

    ./gradlew assembleDebug test lint

Important constraints:

- Keep minSdk = 23.
- Keep package, namespace and applicationId as top.rootu.dddvr.
- Do not reintroduce top.rootu.dddplayer.
- Do not commit local.properties.
- Do not raise minSdk to silence lint.
- Prefer AndroidX compatibility APIs for newer Android requirements.

If Gradle fails with missing ANDROID_HOME or local.properties, run:

    bash scripts/bootstrap-android-sdk.sh
