package com.mapswithme.util.log;

import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.mapswithme.maps.BuildConfig;
import net.jcip.annotations.ThreadSafe;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

@ThreadSafe
public class Logger
{
  private static final String TAG = Logger.class.getSimpleName();

  public enum Scope
  {
    MAIN, LOCATION, TRAFFIC, GPS_TRACKING, TRACK_RECORDER, ROUTING, NETWORK, STORAGE, DOWNLOADER,
    CORE, THIRD_PARTY
  }

  private final static Logger CORE_LOGGER = new Logger(Scope.CORE, "OMcore");
  // Called from JNI to proxy native code logging.
  @SuppressWarnings("unused")
  private static void logCoreMessage(int level, String msg)
  {
    CORE_LOGGER.log(level, msg, null);
  }

  @NonNull
  private final String mFileName;
  @NonNull
  private final String mTag;
  @NonNull
  private final String mScopedTag;

  public Logger(@NonNull Scope scope, @NonNull Class<?> cls)
  {
    this(scope, cls.getSimpleName());
  }

  private Logger(@NonNull Scope scope, @NonNull String tag)
  {
    mFileName = scope.toString().toLowerCase() + ".log";
    mTag = tag;
    mScopedTag = scope + "/" + tag;
  }

  public void v(String msg)
  {
    log(Log.VERBOSE, msg, null);
  }

  public void v(String msg, Throwable tr)
  {
    log(Log.VERBOSE, msg, tr);
  }

  public void d(String msg)
  {
    log(Log.DEBUG, msg, null);
  }

  public void d(String msg, Throwable tr)
  {
    log(Log.DEBUG, msg, tr);
  }

  public void i(String msg)
  {
    log(Log.INFO, msg, null);
  }

  public void i(String msg, Throwable tr)
  {
    log(Log.INFO, msg, tr);
  }

  public void w(String msg)
  {
    log(Log.WARN, msg, null);
  }

  public void w(String msg, Throwable tr)
  {
    log(Log.WARN, msg, tr);
  }

  public void e(String msg)
  {
    log(Log.ERROR, msg, null);
  }

  public void e(String msg, Throwable tr)
  {
    log(Log.ERROR, msg, tr);
  }

  public void log(int level, @NonNull String msg, @Nullable Throwable tr)
  {
    final String logsFolder = LogsManager.INSTANCE.getEnabledLogsFolder();
    if (logsFolder != null)
    {
      final String data = getLevelChar(level) + "/" + mTag + ": " + msg + (tr != null ? '\n' + Log.getStackTraceString(tr) : "");
      LogsManager.EXECUTOR.execute(new WriteTask(logsFolder + File.separator + mFileName,
                                                 data, Thread.currentThread().getName()));
    }
    else if (BuildConfig.DEBUG || level >= Log.INFO)
    {
      // Only Debug builds log DEBUG level to Android system log.
      if (tr != null)
        msg += '\n' + Log.getStackTraceString(tr);
      Log.println(level, mScopedTag, msg);
    }
  }

  private char getLevelChar(int level)
  {
    switch (level)
    {
      case Log.VERBOSE:
        return 'V';
      case Log.DEBUG:
        return 'D';
      case Log.INFO:
        return 'I';
      case Log.WARN:
        return 'W';
      case Log.ERROR:
        return 'E';
    }
    assert false : "Unknown log level " + level;
    return '_';
  }

  private static class WriteTask implements Runnable
  {
    private static final int MAX_SIZE = 3000000;
    @NonNull
    private final String mFilePath;
    @NonNull
    private final String mData;
    @NonNull
    private final String mCallingThread;

    private WriteTask(@NonNull String filePath, @NonNull String data, @NonNull String callingThread)
    {
      mFilePath = filePath;
      mData = data;
      mCallingThread = callingThread;
    }

    @Override
    public void run()
    {
      FileWriter fw = null;
      try
      {
        File file = new File(mFilePath);
        if (!file.exists() || file.length() > MAX_SIZE)
        {
          fw = new FileWriter(file, false);
          fw.write(LogsManager.INSTANCE.getSystemInformation());
        }
        else
        {
          fw = new FileWriter(mFilePath, true);
        }
        DateFormat formatter = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US);
        fw.write(formatter.format(new Date()) + " " + mCallingThread + ": " + mData + "\n");
      }
      catch (IOException e)
      {
        Log.e(TAG, "Failed to write to " + mFilePath + ": " + mData, e);
      }
      finally
      {
        if (fw != null)
          try
          {
            fw.close();
          }
          catch (IOException e)
          {
            Log.e(TAG, "Failed to close file " + mFilePath, e);
          }
      }
    }
  }
}
