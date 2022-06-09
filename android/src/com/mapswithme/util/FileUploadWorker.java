package com.mapswithme.util;

import android.content.Context;

import androidx.annotation.NonNull;
import androidx.work.Worker;
import androidx.work.WorkerParameters;
import com.google.gson.Gson;
import com.mapswithme.util.log.Logger;

import java.io.File;
import java.util.Objects;

public class FileUploadWorker extends Worker
{
  private static final Logger LOGGER = new Logger(Logger.Scope.NETWORK, FileUploadWorker.class);

  static final String PARAMS = "params";

  public FileUploadWorker(@NonNull Context context, @NonNull WorkerParameters workerParams)
  {
    super(context, workerParams);
  }

  @NonNull
  @Override
  public Result doWork()
  {
    String rawJson = Objects.requireNonNull(getInputData().getString(PARAMS));

    Gson gson = new Gson();
    HttpPayload payload = gson.fromJson(rawJson, HttpPayload.class);
    LOGGER.d("Payload '" + payload + "' going to be uploaded");

    File file = new File(payload.getFilePath());
    if (!file.exists())
      return Result.failure();

    HttpUploader uploader = new HttpUploader(payload);
    HttpUploader.Result result = uploader.upload();
    LOGGER.d("Upload finished with result '" + result + "' ");

    if (result.getHttpCode() / 100 != 2)
      return Result.retry();

    boolean isDeleted = file.delete();
    LOGGER.d("File deleted: " + isDeleted + " " + payload.getFilePath());
    return Result.success();
  }
}
