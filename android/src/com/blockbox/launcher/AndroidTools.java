package com.blockbox.launcher;

import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * 通用安卓原生小工具，供 C++ 侧通过 JNI 调用。
 * （安卓自带的 tar 不能解 zip，这里用 java.util.zip 补上）
 */
public final class AndroidTools {
    private static final String TAG = "AndroidTools";

    private AndroidTools() {
    }

    /**
     * 把 zipPath 解压到 destDir（自动创建）。成功返回 true。
     * 解出的可执行文件由 C++ 侧再补执行权限。
     */
    public static boolean unzip(String zipPath, String destDir) {
        if (zipPath == null || destDir == null) {
            return false;
        }
        File dest = new File(destDir);
        if (!dest.isDirectory() && !dest.mkdirs()) {
            return false;
        }
        try (InputStream in = new FileInputStream(zipPath);
             ZipInputStream zip = new ZipInputStream(in)) {
            byte[] buffer = new byte[65536];
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                File out = new File(dest, entry.getName());
                // 防 zip 路径穿越
                if (!out.getCanonicalPath().startsWith(dest.getCanonicalPath() + File.separator)) {
                    return false;
                }
                if (entry.isDirectory()) {
                    out.mkdirs();
                    continue;
                }
                File parent = out.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    return false;
                }
                try (FileOutputStream fos = new FileOutputStream(out)) {
                    int n;
                    while ((n = zip.read(buffer)) > 0) {
                        fos.write(buffer, 0, n);
                    }
                }
                zip.closeEntry();
            }
            return true;
        } catch (Throwable t) {
            Log.e(TAG, "unzip failed: " + zipPath, t);
            return false;
        }
    }
}
