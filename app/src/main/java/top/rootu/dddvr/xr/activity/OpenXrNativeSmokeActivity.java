package top.rootu.dddvr.xr.activity;

public class OpenXrNativeSmokeActivity extends android.app.NativeActivity {
    static {
        System.loadLibrary("openxr_loader");
        System.loadLibrary("dddvr_openxr_native_smoke");
    }
}
