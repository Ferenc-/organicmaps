/*******************************************************************************
 The MIT License (MIT)

 Copyright (c) 2014 Alexander Borsuk <me@alex.bio> from Minsk, Belarus

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 *******************************************************************************/
#include "platform/http_client.hpp"
#include "platform/platform.hpp"

#include "coding/zlib.hpp"

#include "base/assert.hpp"
#include "base/scope_guard.hpp"
#include "base/exception.hpp"
#include "base/logging.hpp"
#include "base/string_utils.hpp"

#include <curl/curl.h>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>


using namespace coding;

namespace
{
DECLARE_EXCEPTION(PipeCallError, RootException);


using HeadersVector = std::vector<std::pair<std::string, std::string>>;

HeadersVector ParseHeaders(std::string const & raw)
{
  std::istringstream stream(raw);
  HeadersVector headers;
  std::string line;
  while (getline(stream, line))
  {
    auto const cr = line.rfind('\r');
    if (cr != std::string::npos)
      line.erase(cr);

    auto const delims = line.find(": ");
    if (delims != std::string::npos)
      headers.emplace_back(line.substr(0, delims), line.substr(delims + 2));
  }
  return headers;
}

bool WriteToFile(std::string const & fileName, std::string const & data)
{
  std::ofstream ofs(fileName);
  if(!ofs.is_open())
  {
    LOG(LERROR, ("Failed to write into a temporary file."));
    return false;
  }

  ofs << data;
  return true;
}

std::string Decompress(std::string const & compressed, std::string const & encoding)
{
  std::string decompressed;

  if (encoding == "deflate")
  {
    ZLib::Inflate inflate(ZLib::Inflate::Format::ZLib);

    // We do not check return value of inflate here.
    // It may return false if compressed data is broken or if there is some unconsumed data
    // at the end of buffer. The second case considered as ok by some http clients.
    // For example, server we use for AsyncGuiThread_GetHotelInfo test adds '\n' to the end of the buffer
    // and MacOS client and some versions of curl return no error.
    UNUSED_VALUE(inflate(compressed, back_inserter(decompressed)));
  }
  else
  {
    ASSERT(false, ("Unsupported Content-Encoding:", encoding));
  }

  return decompressed;
}
}  // namespace

namespace platform
{
// Redirects are handled recursively. TODO(AlexZ): avoid infinite redirects loop.
bool HttpClient::RunHttpRequest()
{
  std::string receivedHeaders;
  FILE *outputFp = nullptr, *inputFp = nullptr;
  struct curl_slist *curlHeaders = nullptr;
  curl_global_init(CURL_GLOBAL_ALL);
  CURL *curl = curl_easy_init();
  if(!curl) { return false;}
  auto curlCleaner = [&](){
                            if(curl!=nullptr) curl_easy_cleanup(curl);
                            if(curlHeaders!=nullptr) curl_slist_free_all(curlHeaders);
                            if(outputFp!=nullptr) std::fclose(outputFp);
                            if(inputFp!=nullptr) std::fclose(inputFp);
                          };
  SCOPE_GUARD(cleanUpCurl, curlCleaner);
  char errbuf[CURL_ERROR_SIZE];
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

  if (!m_outputFile.empty()) {outputFp = std::fopen(m_outputFile.c_str(), "wb"); }
  if (!m_inputFile.empty()) {inputFp = std::fopen(m_inputFile.c_str(), "rb");}

  if (m_httpMethod == "HEAD")
  {
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
      +[](char *buffer, size_t size, size_t nitems, void *stream) -> size_t {
        size_t realsize = size * nitems;
        std::string * receivedHeadersPtr = static_cast<std::string*>(stream);
        receivedHeadersPtr->append(buffer, realsize);
        return realsize;
      });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&receivedHeaders));
  }
  else if (m_httpMethod == "POST")
  {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
  }
  else if (m_httpMethod == "PUT")
  {
    curl_easy_setopt(curl, CURLOPT_PUT, 1L);
  }
  else if (m_httpMethod == "DELETE")
  {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  }
  else if (m_httpMethod == "GET")
  {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  }
  else {
    ASSERT(false, ("Unsupported HTTP method", httpMethod));
  }

  for (auto const & header : m_headers)
  {
    curlHeaders = curl_slist_append(curlHeaders, (header.first + ": " + header.second).c_str());
  }

  if (!m_cookies.empty())
    curl_easy_setopt(curl, CURLOPT_COOKIE, m_cookies.c_str());

  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<int>(m_timeoutSec * 1000));



  // Content-Length is added automatically by curl.
  if (!m_inputFile.empty() && inputFp != nullptr)
  {
    curl_easy_setopt(curl,
            (m_httpMethod == "POST")?CURLOPT_POSTFIELDSIZE_LARGE:CURLOPT_INFILESIZE_LARGE,
            (curl_off_t)m_bodyData.size());

    curl_easy_setopt(curl, CURLOPT_READDATA, static_cast<void*>(inputFp));
    curl_easy_setopt(curl, CURLOPT_READFUNCTION,
      +[](char *ptr, size_t size, size_t nmemb, void *stream) -> size_t {
        std::FILE * inputFp = static_cast<std::FILE*>(stream);
        return std::fread(ptr, size, nmemb, inputFp);
      });
  }

  // Use memory to receive data from server.
  // If user has specified file name to save data, then save data there.
  if (outputFp == nullptr) //( m_outputFile.empty())
  {
    // Write into m_serverResponse directly
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(&m_serverResponse));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
      +[](char *buffer, size_t size, size_t nmemb, void *stream) -> size_t {
        size_t realsize = size * nmemb;
        std::string * m_serverResponsePtr = static_cast<std::string*>(stream);
        m_serverResponsePtr->append(buffer, realsize);
        return realsize;
      });
  }
  else
  {
    // If not empty then write into a file named by m_outputFile
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(outputFp));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
      +[](char *ptr, size_t size, size_t nmemb, void *stream) -> size_t {
        std::FILE * outputFp = static_cast<std::FILE*>(stream);
        return std::fwrite(ptr, size, nmemb, outputFp);
      });
  }

  curl_easy_setopt(curl, CURLOPT_URL, m_urlRequested);

  LOG(LDEBUG, ("Calling curl"));

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK)
  {
    LOG(LERROR, ("Error calling curl:", errbuf));
    return false;
  }
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &m_errorCode);

  m_headers.clear();
  auto const headers = ParseHeaders(receivedHeaders);
  std::string serverCookies;
  std::string headerKey;
  for (auto const & header : headers)
  {
    if (strings::EqualNoCase(header.first, "Set-Cookie"))
    {
      serverCookies += header.second + ", ";
    }
    else
    {
      if (strings::EqualNoCase(header.first, "Location"))
        m_urlReceived = header.second;

      if (m_loadHeaders)
      {
        headerKey = header.first;
        strings::AsciiToLower(headerKey);
        m_headers.emplace(headerKey, header.second);
      }
    }
  }
  m_headers.emplace("Set-Cookie", NormalizeServerCookies(move(serverCookies)));

  if (m_urlReceived.empty())
  {
    m_urlReceived = m_urlRequested;
  }
  else
  {
    // Handle HTTP redirect.
    // TODO(AlexZ): Should we check HTTP redirect code here?
    LOG(LDEBUG, ("HTTP redirect", m_errorCode, "to", m_urlReceived));

    HttpClient redirect(m_urlReceived);
    redirect.SetCookies(CombinedCookies());

    /* Prevent multiple open curl handles and FPs because of recursion*/
    curlCleaner(); cleanUpCurl.release();
    if (!redirect.RunHttpRequest())
    {
      m_errorCode = -1;
      return false;
    }

    m_errorCode = redirect.ErrorCode();
    m_urlReceived = redirect.UrlReceived();
    m_headers = move(redirect.m_headers);
    m_serverResponse = move(redirect.m_serverResponse);
  }

  for (auto const & header : headers)
  {
    if (strings::EqualNoCase(header.first, "content-encoding") &&
        !strings::EqualNoCase(header.second, "identity"))
    {
      m_serverResponse = Decompress(m_serverResponse, header.second);
      LOG(LDEBUG, ("Response with", header.second, "is decompressed."));
      break;
    }
  }
  return true;
}
}  // namespace platform
