package com.mapswithme.util;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.mapswithme.util.log.Logger;

public final class SecureStorage
{
  private static final Logger LOGGER = new Logger(Logger.Scope.MAIN, SecureStorage.class);

  private SecureStorage() {}

  public static void save(@NonNull Context context, @NonNull String key, @NonNull String value)
  {
    LOGGER.d("save: key = " + key);
    SharedPreferences prefs = context.getSharedPreferences("secure", Context.MODE_PRIVATE);
    prefs.edit().putString(key, value).apply();
  }

  @Nullable
  public static String load(@NonNull Context context, @NonNull String key)
  {
    LOGGER.d("load: key = " + key);
    SharedPreferences prefs = context.getSharedPreferences("secure", Context.MODE_PRIVATE);
    return prefs.getString(key, null);
  }

  public static void remove(@NonNull Context context, @NonNull String key)
  {
    LOGGER.d("remove: key = " + key);
    SharedPreferences prefs = context.getSharedPreferences("secure", Context.MODE_PRIVATE);
    prefs.edit().remove(key).apply();
  }
}
