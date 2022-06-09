package com.mapswithme.maps.location;

import android.content.Context;

import androidx.annotation.NonNull;
import com.google.android.gms.common.ConnectionResult;
import com.google.android.gms.common.GoogleApiAvailability;
import com.mapswithme.util.Config;
import com.mapswithme.util.log.Logger;

public class LocationProviderFactory
{
  private static final Logger LOGGER = new Logger(Logger.Scope.LOCATION, LocationProviderFactory.class);

  public static boolean isGoogleLocationAvailable(@NonNull Context context)
  {
    return GoogleApiAvailability.getInstance().isGooglePlayServicesAvailable(context) == ConnectionResult.SUCCESS;
  }

  public static BaseLocationProvider getProvider(@NonNull Context context, @NonNull BaseLocationProvider.Listener listener)
  {
    if (isGoogleLocationAvailable(context) && Config.useGoogleServices())
    {
      LOGGER.d("Use google provider.");
      return new GoogleFusedLocationProvider(context, listener);
    }
    else
    {
      LOGGER.d("Use native provider");
      return new AndroidNativeProvider(context, listener);
    }
  }
}
