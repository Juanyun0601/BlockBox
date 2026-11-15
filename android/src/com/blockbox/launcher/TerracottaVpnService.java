package com.blockbox.launcher;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.content.Intent;
import android.net.VpnService;
import android.os.Build;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;

import net.burningtnt.terracotta.TerracottaAndroidAPI;

/**
 * 陶瓦联机（Terracotta）的 VPN 服务：EasyTier 在安卓上建立虚拟组网需要一块 TUN 网卡，
 * 只有系统的 VpnService 能提供。流程按官方文档：
 *   1. 原生层发起请求 → TerracottaBridge 的回调拉起本服务；
 *   2. onStartCommand 里取走待处理请求，establish() 建立 VPN 并把 fd 交给 EasyTier；
 *   3. 应用持有 tun fd，EasyTier 退出（回到待机）后由 TerracottaBridge.closeTun() 关闭。
 *
 * 隧道建立成功后本服务转为前台服务常驻：联机核心跑在启动器进程里，用户切到
 * 游戏时该进程退到后台，若没有前台服务保活，进程可能被系统回收，联机随之中断。
 */
public final class TerracottaVpnService extends VpnService {
    private static final String CHANNEL_ID = "terracotta_vpn";
    private static final int NOTIFICATION_ID = 0x5443;

    /** 当前服务实例（closeTun 是静态方法，隧道关闭时需要借它退出前台态） */
    private static TerracottaVpnService instance = null;

    private static ParcelFileDescriptor tun = null;

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return super.onBind(intent);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        closeTun();

        TerracottaAndroidAPI.VpnServiceRequest request;
        try {
            request = TerracottaAndroidAPI.getPendingVpnServiceRequest();
        } catch (IllegalStateException e) {
            // 没有待处理的请求（可能已超时），直接退出
            stopSelf();
            return START_NOT_STICKY;
        }

        Builder builder = new Builder();
        builder.setSession("BlockBox Terracotta");
        builder.setMtu(1400);

        try {
            tun = request.startVpnService(builder);
        } catch (RuntimeException e) {
            // establish() 返回 null：通常是用户尚未授权 VPN
            request.reject();
            stopSelf();
            return START_NOT_STICKY;
        }

        // 隧道已建立：转前台服务，保住启动器进程不被后台回收
        startForegroundCompat(buildNotification());

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopForegroundCompat();
        instance = null;
        super.onDestroy();
    }

    @Override
    public void onRevoke() {
        // 系统或用户撤销了 VPN 授权：关掉网卡并让联机核心回到待机
        closeTun();
        new Thread(() -> TerracottaBridge.setWaiting(), "Terracotta-VpnRevoke").start();
        stopSelf();
    }

    /** EasyTier 退出后由桥接层调用，释放 TUN 文件描述符；隧道关闭后前台态一并退出 */
    static void closeTun() {
        ParcelFileDescriptor fd = tun;
        tun = null;
        if (fd != null) {
            try {
                fd.close();
            } catch (Exception ignored) {
            }
            TerracottaVpnService service = instance;
            if (service != null) {
                service.stopForegroundCompat();
                service.stopSelf();
            }
        }
    }

    private void startForegroundCompat(Notification notification) {
        if (Build.VERSION.SDK_INT >= 34) {
            // targetSdk 34 起要求前台服务显式声明类型；本服务在 manifest 里
            // 声明了 specialUse 并附用途说明属性
            startForeground(NOTIFICATION_ID, notification,
                    android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        } else {
            startForeground(NOTIFICATION_ID, notification);
        }
    }

    private void stopForegroundCompat() {
        if (Build.VERSION.SDK_INT >= 33) {
            stopForeground(STOP_FOREGROUND_REMOVE);
        } else {
            stopForeground(true);
        }
    }

    private Notification buildNotification() {
        final NotificationManager nm =
                (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= 26 && nm.getNotificationChannel(CHANNEL_ID) == null) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "联机运行状态", NotificationManager.IMPORTANCE_LOW);
            channel.setDescription("陶瓦联机进行中的常驻提醒");
            channel.setShowBadge(false);
            nm.createNotificationChannel(channel);
        }
        Notification.Builder builder = Build.VERSION.SDK_INT >= 26
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder
                .setContentTitle("BlockBox 联机运行中")
                .setContentText("虚拟网卡已连接，切换到游戏后联机会保持。")
                .setSmallIcon(getApplicationInfo().icon)
                .setOngoing(true)
                .build();
    }
}
