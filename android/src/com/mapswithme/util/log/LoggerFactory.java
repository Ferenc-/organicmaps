package com.mapswithme.util.log;

import android.app.Application;
import android.content.Context;
import android.content.SharedPreferences;
import android.location.LocationManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Build;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.mapswithme.maps.BuildConfig;
import com.mapswithme.maps.MwmApplication;
import com.mapswithme.maps.R;
import com.mapswithme.util.Utils;
import net.jcip.annotations.ThreadSafe;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.EnumMap;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * By default uses Android's system logger.
 * After an initFileLogging() call can use a custom file logging implementation.
 *
 * Its important to have only system logging here to avoid infinite loop
 * (file loggers call getEnabledLogsFolder() in preparation to write).
 */
@ThreadSafe
public class LoggerFactory
{
  public enum Type
  {
    MISC, LOCATION, TRAFFIC, GPS_TRACKING, TRACK_RECORDER, ROUTING, NETWORK, STORAGE, DOWNLOADER,
    CORE, THIRD_PARTY, BILLING
  }

  public interface OnZipCompletedListener
  {
    // Called from the logger thread.
    public void onCompleted(final boolean success, @Nullable final String zipPath);
  }

  private final static String TAG = LoggerFactory.class.getSimpleName();
  private final static String CORE_TAG = "OMcore";

  public final static LoggerFactory INSTANCE = new LoggerFactory();

  @NonNull
  private final EnumMap<Type, Logger> mLoggers = new EnumMap<>(Type.class);
  @Nullable
  private ExecutorService mFileLoggerExecutor;
  @Nullable
  private Application mApplication;
  private boolean mIsFileLoggingEnabled = false;
  @Nullable
  private String mLogsFolder;

  private LoggerFactory()
  {
    Log.i(LoggerFactory.class.getSimpleName(), "Logging started");
  }

  public synchronized void initFileLogging(@NonNull Application application)
  {
    Log.i(TAG, "Init file logging");
    mApplication = application;

    final SharedPreferences prefs = MwmApplication.prefs(mApplication);
    // File logging is enabled by default for beta builds.
    mIsFileLoggingEnabled = prefs.getBoolean(mApplication.getString(R.string.pref_enable_logging),
                                             BuildConfig.BUILD_TYPE.equals("beta"));
    Log.i(TAG, "isFileLoggingEnabled preference: " + mIsFileLoggingEnabled);
    mIsFileLoggingEnabled = mIsFileLoggingEnabled && ensureLogsFolder() != null;

    // Set native logging level, save into shared preferences.
    switchFileLoggingEnabled(mIsFileLoggingEnabled);
  }

  /**
   * Returns logs folder path if file logging is enabled.
   * Switches off file logging if the path doesn't exist and can't be created.
   */
  @Nullable
  synchronized String getEnabledLogsFolder()
  {
    if (!mIsFileLoggingEnabled)
      return null;

    final String logsFolder = ensureLogsFolder();
    if (logsFolder == null)
      switchFileLoggingEnabled(false);

    return logsFolder;
  }

  /**
   * Ensures logs folder exists.
   * Tries to create it and/or re-get a path from the system, falling back to the internal storage.
   * NOTE: initFileLogging() must be called before.
   *
   * @return logs folder path, null if it can't be created
   */
  @Nullable
  private String ensureLogsFolder()
  {
    assert mApplication != null : "mApplication must be initialized first by calling initFileLogging()";

    if (mLogsFolder != null && createWritableDir(mLogsFolder))
      return mLogsFolder;

    mLogsFolder = createLogsFolder(mApplication.getExternalFilesDir(null));
    if (mLogsFolder == null)
      mLogsFolder = createLogsFolder(mApplication.getFilesDir());

    if (mLogsFolder == null)
      Log.e(TAG, "Can't create any logs folder");

    return mLogsFolder;
  }

  private boolean createWritableDir(@NonNull final String path)
  {
    final File dir = new File(path);
    if (!dir.exists())
    {
      Log.i(TAG, "Creating logs folder " + path);
      if (!dir.mkdirs())
      {
        Log.e(TAG, "Can't create a logs folder " + path);
        return false;
      }
    }
    if (!dir.canWrite())
    {
      Log.e(TAG, "Can't write to a logs folder " + path);
      return false;
    }
    return true;
  }

  @Nullable
  private String createLogsFolder(@Nullable final File dir)
  {
    if (dir != null)
    {
      final String path = dir.getPath() + File.separator + "logs";
      if (createWritableDir(path))
        return path;
    }
    return null;
  }

  private void switchFileLoggingEnabled(boolean enabled)
  {
    mIsFileLoggingEnabled = enabled;
    nativeToggleCoreDebugLogs(enabled || BuildConfig.DEBUG);
    MwmApplication.prefs(mApplication)
                  .edit()
                  .putBoolean(mApplication.getString(R.string.pref_enable_logging), enabled)
                  .apply();
    Log.i(TAG, "Logging to " + (enabled ? "logs folder " + mLogsFolder : "system log"));
  }

  public synchronized boolean isFileLoggingEnabled()
  {
    return mIsFileLoggingEnabled;
  }

  /**
   * Returns false if file logging can't be enabled.
   *
   * NOTE: initFileLogging() must be called before.
   */
  public synchronized boolean setFileLoggingEnabled(boolean enabled)
  {
    assert mApplication != null : "mApplication must be initialized first by calling initFileLogging()";

    if (mIsFileLoggingEnabled != enabled)
    {
      Log.i(TAG, "Switching isFileLoggingEnabled to " + enabled);
      if (enabled && ensureLogsFolder() == null)
      {
        Log.e(TAG, "Can't enable file logging: no logs folder.");
        return false;
      }
      else
        switchFileLoggingEnabled(enabled);
    }

    return true;
  }

  @NonNull
  public synchronized Logger getLogger(@NonNull Type type)
  {
    Logger logger = mLoggers.get(type);
    if (logger == null)
    {
      logger = new Logger(type, getFileLoggerExecutor());
      mLoggers.put(type, logger);
    }
    return logger;
  }

  /**
   * NOTE: initFileLogging() must be called before.
   */
  public synchronized void zipLogs(@NonNull OnZipCompletedListener listener)
  {
    assert mApplication != null : "mApplication must be initialized first by calling initFileLogging()";

    if (ensureLogsFolder() == null)
    {
      Log.e(TAG, "Can't zip log files: no logs folder.");
      listener.onCompleted(false, null);
      return;
    }

    Log.i(TAG, "Zipping log files in " + mLogsFolder);
    final Runnable task = new ZipLogsTask(mLogsFolder, mLogsFolder + ".zip", listener);
    getFileLoggerExecutor().execute(task);
  }

  @NonNull
  private ExecutorService getFileLoggerExecutor()
  {
    if (mFileLoggerExecutor == null)
      mFileLoggerExecutor = Executors.newSingleThreadExecutor();
    return mFileLoggerExecutor;
  }

  // Called from JNI.
  @SuppressWarnings("unused")
  private static void logCoreMessage(int level, String msg)
  {
    final Logger logger = INSTANCE.getLogger(Type.CORE);
    switch (level)
    {
      case Log.DEBUG:
        logger.d(CORE_TAG, msg);
        break;
      case Log.INFO:
        logger.i(CORE_TAG, msg);
        break;
      case Log.WARN:
        logger.w(CORE_TAG, msg);
        break;
      case Log.ERROR:
        logger.e(CORE_TAG, msg);
        break;
      default:
        logger.v(CORE_TAG, msg);
    }
  }

  /**
   * NOTE: initFileLogging() must be called before.
   */
  String getSystemInformation()
  {
    assert mApplication != null : "mApplication must be initialized first by calling initFileLogging()";

    String res = "Android version: " + Build.VERSION.SDK_INT +
                 "\nDevice: " + Utils.getFullDeviceModel() +
                 "\nApp version: " + BuildConfig.APPLICATION_ID + " " + BuildConfig.VERSION_NAME +
                 "\nLocale: " + Locale.getDefault() +
                 "\nNetworks: ";
    final ConnectivityManager manager = (ConnectivityManager) mApplication.getSystemService(Context.CONNECTIVITY_SERVICE);
    if (manager != null)
      // TODO: getAllNetworkInfo() is deprecated, for alternatives check
      // https://stackoverflow.com/questions/32547006/connectivitymanager-getnetworkinfoint-deprecated
      for (NetworkInfo info : manager.getAllNetworkInfo())
        res += "\n\t" + info.toString();
    res += "\nLocation providers: ";
    final LocationManager locMngr = (android.location.LocationManager) mApplication.getSystemService(Context.LOCATION_SERVICE);
    if (locMngr != null)
      for (String provider : locMngr.getProviders(true))
        res += provider + " ";

    return res + "\n\n";
  }

  private static native void nativeToggleCoreDebugLogs(boolean enabled);
}
