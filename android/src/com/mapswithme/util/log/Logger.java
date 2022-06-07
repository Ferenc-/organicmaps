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
import java.util.concurrent.Executor;

@ThreadSafe
public class Logger
{
  private static final String TAG = Logger.class.getSimpleName();
  @NonNull
  private final String mFileName;
  @NonNull
  private final Executor mExecutor;

  public Logger(@NonNull LoggerFactory.Type type, @NonNull Executor executor)
  {
    mFileName = type.toString().toLowerCase() + ".log";
    mExecutor = executor;
  }

  public void v(String tag, String msg)
  {
    log(Log.VERBOSE, tag, msg, null);
  }

  public void v(String tag, String msg, Throwable tr)
  {
    log(Log.VERBOSE, tag, msg, tr);
  }

  public void d(String tag, String msg)
  {
    log(Log.DEBUG, tag, msg, null);
  }

  public void d(String tag, String msg, Throwable tr)
  {
    log(Log.DEBUG, tag, msg, tr);
  }

  public void i(String tag, String msg)
  {
    log(Log.INFO, tag, msg, null);
  }

  public void i(String tag, String msg, Throwable tr)
  {

    log(Log.INFO, tag, msg, tr);
  }

  public void w(String tag, String msg)
  {
    log(Log.WARN, tag, msg, null);
  }

  public void w(String tag, String msg, Throwable tr)
  {
    log(Log.WARN, tag, msg, tr);
  }

  public void e(String tag, String msg)
  {
    log(Log.ERROR, tag, msg, null);
  }

  public void e(String tag, String msg, Throwable tr)
  {
    log(Log.ERROR, tag, msg, tr);
  }

  public void log(int level, @NonNull String tag, @NonNull String msg, @Nullable Throwable tr)
  {
    final String logsFolder = LoggerFactory.INSTANCE.getEnabledLogsFolder();
    if (logsFolder != null)
    {
      final String data = getLevelChar(level) + "/" + tag + ": " + msg + (tr != null ? '\n' + Log.getStackTraceString(tr) : "");
      mExecutor.execute(new WriteTask(logsFolder + File.separator + mFileName,
                                      data, Thread.currentThread().getName()));
    }
    else if (BuildConfig.DEBUG || level >= Log.INFO)
      Log.println(level, tag, msg + (tr != null ? '\n' + Log.getStackTraceString(tr) : ""));
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
          fw.write(LoggerFactory.INSTANCE.getSystemInformation());
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
