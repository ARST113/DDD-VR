# PICO Runtime Checklist

1. Install APK on Pico.
2. Open a test MP4 via intent/url.
3. Verify video renders.
4. Verify overlay appears by default.
5. Verify Pico pointer clicks Play/Pause repeatedly.
6. Verify `-15s` and `+15s` repeatedly.
7. Verify MONO/SBS/SBS_R/OU/OU_R switching.
8. Verify Swap Eyes toggles repeatedly.
9. Verify Exit closes player.
10. Verify BACK hides overlay first.
11. Verify BACK exits on second press.
12. Verify Curved is disabled/hidden unless implemented.
13. Verify no black screen after any visible control action.
14. Verify no ExoPlayer wrong-thread crash in logs.
