#pragma once

#include "std/target_os.hpp"

#if defined(OMIM_OS_IPHONE)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGLES/ES2/glext.h>
  #include <OpenGLES/ES3/gl.h>
#elif defined(OMIM_OS_MAC)
  #define GL_SILENCE_DEPRECATION
  #include <OpenGL/glext.h>
  #include <OpenGL/gl3.h>
#elif defined(OMIM_OS_WINDOWS)
  #include "std/windows.hpp"
  #define GL_GLEXT_PROTOTYPES
  #include <GL/gl.h>
  #include "3party/GL/glext.h"
#elif defined(OMIM_OS_ANDROID)
  #define GL_GLEXT_PROTOTYPES
  #include <GLES2/gl2.h>
  #include <GLES2/gl2ext.h>
  #include "android/jni/app/organicmaps/opengl/gl3stub.h"
  #include <EGL/egl.h>
#elif defined(OMIM_OS_LINUX)
  #define GL_GLEXT_PROTOTYPES
  #include <GL/gl.h>
  #include <GL/glext.h>
#if __has_include(<EGL/egl.h>)
  #define OMIM_OS_LINUX_HAS_EGL 1
  #include <EGL/egl.h>
  /*
   * EGL transitively includes X11/X.h
   * which has a preprocessor constant named "Always"
   * defined to a numeric value
   * that replaces "Always" in dp::TestFunction::Always
   * unless we undef it here.
   */
  #undef Always
  /*
   * There is a similar problem with "Cancellable::Status"
   */
  #undef Status
#endif  //__has_include(<EGL/egl.h>)
#else
  #define GL_GLEXT_PROTOTYPES
  #include <GL/gl.h>
  #include <GL/glext.h>
#endif
