package com.mapswithme.maps.location;

import android.content.Context;
import android.location.Location;
import android.os.Looper;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import com.google.android.gms.location.FusedLocationProviderClient;
import com.google.android.gms.location.LocationAvailability;
import com.google.android.gms.location.LocationCallback;
import com.google.android.gms.location.LocationRequest;
import com.google.android.gms.location.LocationResult;
import com.google.android.gms.location.LocationServices;
import com.google.android.gms.location.LocationSettingsRequest;
import com.google.android.gms.location.SettingsClient;
import com.mapswithme.util.log.Logger;

import static com.mapswithme.maps.location.LocationHelper.ERROR_NOT_SUPPORTED;

class GoogleFusedLocationProvider extends BaseLocationProvider
{
  private static final Logger LOGGER = new Logger(Logger.Scope.LOCATION, GoogleFusedLocationProvider.class);

  @NonNull
  private final FusedLocationProviderClient mFusedLocationClient;
  @NonNull
  private final SettingsClient mSettingsClient;

  private class GoogleLocationCallback extends LocationCallback
  {
    @Override
    public void onLocationResult(@NonNull LocationResult result)
    {
      final Location location = result.getLastLocation();
      // Documentation is inconsistent with the code: "returns null if no locations are available".
      // https://developers.google.com/android/reference/com/google/android/gms/location/LocationResult#getLastLocation()
      //noinspection ConstantConditions
      if (location != null)
        mListener.onLocationChanged(location);
    }

    @Override
    public void onLocationAvailability(@NonNull LocationAvailability availability)
    {
      if (!availability.isLocationAvailable()) {
        LOGGER.w("isLocationAvailable returned false");
        //mListener.onLocationError(ERROR_GPS_OFF);
      }
    }
  }

  @Nullable
  private final GoogleLocationCallback mCallback = new GoogleLocationCallback();

  private boolean mActive = false;

  GoogleFusedLocationProvider(@NonNull Context context, @NonNull BaseLocationProvider.Listener listener)
  {
    super(listener);
    mFusedLocationClient = LocationServices.getFusedLocationProviderClient(context);
    mSettingsClient = LocationServices.getSettingsClient(context);
  }

  @SuppressWarnings("MissingPermission")
  // A permission is checked externally
  @Override
  public void start(long interval)
  {
    LOGGER.d("start()");
    if (mActive)
      throw new IllegalStateException("Already subscribed");
    mActive = true;

    final LocationRequest locationRequest = LocationRequest.create();
    locationRequest.setPriority(LocationRequest.PRIORITY_HIGH_ACCURACY);
    locationRequest.setInterval(interval);
    // Wait a few seconds for accurate locations initially, when accurate locations could not be computed on the device immediately.
    // https://github.com/organicmaps/organicmaps/issues/2149
    locationRequest.setWaitForAccurateLocation(true);
    LOGGER.d("Request Google fused provider to provide locations at this interval = "
             + interval + " ms");
    locationRequest.setFastestInterval(interval / 2);

    LocationSettingsRequest.Builder builder = new LocationSettingsRequest.Builder();
    builder.addLocationRequest(locationRequest);
    final LocationSettingsRequest locationSettingsRequest = builder.build();

    mSettingsClient.checkLocationSettings(locationSettingsRequest).addOnSuccessListener(locationSettingsResponse -> {
      LOGGER.d("Service is available");
      mFusedLocationClient.requestLocationUpdates(locationRequest, mCallback, Looper.myLooper());
    }).addOnFailureListener(e -> {
      LOGGER.e("Service is not available: " + e);
      mListener.onLocationError(ERROR_NOT_SUPPORTED);
    });

    // onLocationResult() may not always be called regularly, however the device location is known.
    mFusedLocationClient.getLastLocation().addOnSuccessListener(location -> {
      LOGGER.d("onLastLocation, location = " + location);
      if (location == null)
        return;
      mListener.onLocationChanged(location);
    });
  }

  @Override
  protected void stop()
  {
    LOGGER.d("stop()");
    mFusedLocationClient.removeLocationUpdates(mCallback);
    mActive = false;
  }

  @Override
  protected boolean trustFusedLocations() { return true; }
}
