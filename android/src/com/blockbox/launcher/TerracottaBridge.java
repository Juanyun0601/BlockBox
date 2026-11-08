package com.blockbox.launcher;

import android.content.Context;
import android.content.Intent;
import android.net.VpnService;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import net.burningtnt.terracotta.TerracottaAndroidAPI;

/**
 * 方块盒子 ↔ 陶瓦联机（Terracotta Android）桥接层。
 *
 * C++ 侧（QJniObject）只调用本类的静态方法；所有异常都在这里吞掉并转成
 * 返回值，避免 JNI 异常跨层抛出。方法都可能从任意线程调用（官方 API 线程安全）。
 */
public final class TerracottaBridge {
    private static final String TAG = "TerracottaBridge";

    private static volatile Context appContext = null;
    private static volatile boolean initialized = false;

    private TerracottaBridge() {
    }

    /** 初始化联机核心（进程内仅一次）；失败返回 false，原因看 logcat */
    public static synchronized boolean init(Context context) {
        if (initialized) {
            return true;
        }
        if (context == null) {
            return false;
        }
        appContext = context.getApplicationContext();
        try {
            TerracottaAndroidAPI.Metadata metadata = TerracottaAndroidAPI.initialize(
                    appContext,
                    () -> postToMainThread(TerracottaBridge::onVpnServiceRequested));
            cachedVersion = metadata.getTerracottaVersion();
            Log.i(TAG, "Terracotta initialized: v" + metadata.getTerracottaVersion()
                    + ", easytier=" + metadata.getEasyTierVersion());
            initialized = true;
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "Terracotta initialize failed", t);
            return false;
        }
    }

    /** 当前状态 JSON（与桌面 HTTP /state 同构）；未初始化或出错返回 null */
    public static String getState() {
        if (!initialized) {
            return null;
        }
        try {
            return TerracottaAndroidAPI.getState();
        } catch (Throwable t) {
            Log.e(TAG, "getState failed", t);
            return null;
        }
    }

    /** 建房（host-scanning）。room 为空串表示新建房间，否则复用既有邀请码 */
    public static boolean setScanning(String room, String player) {
        if (!initialized) {
            return false;
        }
        try {
            TerracottaAndroidAPI.setScanning(room == null || room.isEmpty() ? null : room,
                    player == null || player.isEmpty() ? null : player);
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "setScanning failed", t);
            return false;
        }
    }

    /** 加入房间（guest-connecting）；邀请码无效或状态不允许时返回 false */
    public static boolean setGuesting(String room, String player) {
        if (!initialized || room == null || room.isEmpty()) {
            return false;
        }
        try {
            return TerracottaAndroidAPI.setGuesting(room,
                    player == null || player.isEmpty() ? null : player);
        } catch (Throwable t) {
            Log.e(TAG, "setGuesting failed", t);
            return false;
        }
    }

    /** 回到待机并释放 TUN 网卡 */
    public static boolean setWaiting() {
        try {
            if (initialized) {
                TerracottaAndroidAPI.setWaiting();
            }
            TerracottaVpnService.closeTun();
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "setWaiting failed", t);
            return false;
        }
    }

    /** 内置核心版本号（init 成功时记录）；未初始化时返回 null */
    private static volatile String cachedVersion = null;

    public static String terracottaVersion() {
        return cachedVersion;
    }

    // ── VPN 授权与请求处理 ──────────────────────────────────────────────

    /** 已授权返回 true；未授权则拉起系统授权页并返回 false */
    public static boolean requestVpnConsent(Context context) {
        Context ctx = context != null ? context : appContext;
        if (ctx == null) {
            return false;
        }
        Intent consent = VpnService.prepare(ctx);
        if (consent == null) {
            return true;
        }
        consent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            ctx.startActivity(consent);
        } catch (Throwable t) {
            Log.e(TAG, "startActivity for VPN consent failed", t);
        }
        return false;
    }

    /** 原生层（EasyTier）发起 TUN 请求的回调，可能在任意线程触发 */
    private static void onVpnServiceRequested() {
        if (requestVpnConsent(null)) {
            Context ctx = appContext;
            if (ctx != null) {
                try {
                    ctx.startService(new Intent(ctx, TerracottaVpnService.class));
                } catch (Throwable t) {
                    Log.e(TAG, "startService(TerracottaVpnService) failed", t);
                }
            }
        }
        // 未授权的情况：系统授权页已弹出，请求会在 30 秒后超时（等价 reject），
        // 用户授权后再点一次建房/加入即可重试。
    }

    private static void postToMainThread(Runnable runnable) {
        new Handler(Looper.getMainLooper()).post(runnable);
    }
}
