#include "coding/transliteration.hpp"

#include "coding/string_utf8_multilang.hpp"

#include "base/logging.hpp"
#include "base/string_utils.hpp"

// ICU includes.
#include <unicode/putil.h>
#include <unicode/translit.h>
#include <unicode/uclean.h>
#include <unicode/unistr.h>
#include <unicode/utrans.h>
#include <unicode/utypes.h>

#include <cstring>
#include <mutex>

struct Transliteration::TransliteratorInfo
{
  std::atomic<bool> m_initialized = false;
  std::mutex m_mutex;
  std::unique_ptr<icu::Transliterator> m_transliterator;
};

Transliteration::Transliteration() : m_inited(false), m_mode(Mode::Enabled) {}

Transliteration::~Transliteration()
{
  // The use of u_cleanup() just before an application terminates is optional,
  // but it should be called only once for performance reasons.
  // The primary benefit is to eliminate reports of memory or resource leaks originating
  // in ICU code from the results generated by heap analysis tools.
  m_transliterators.clear();
  u_cleanup();
}

Transliteration & Transliteration::Instance()
{
  static Transliteration instance;
  return instance;
}

void Transliteration::Init(std::string const & icuDataDir)
{
  // Fast atomic check before mutex lock.
  if (m_inited)
    return;

  std::lock_guard<std::mutex> lock(m_initializationMutex);
  if (m_inited)
    return;

  // This function should be called before the first ICU operation that will require the loading of
  // an ICU data file. On Linux, data file is loaded automatically from the shared library.
#ifndef OMIM_OS_LINUX
  u_setDataDirectory(icuDataDir.c_str());
#else
  UNUSED_VALUE(icuDataDir);
#endif

  for (auto const & lang : StringUtf8Multilang::GetSupportedLanguages())
  {
    for (auto const & t : lang.m_transliteratorsIds)
      if (m_transliterators.count(t) == 0)
        m_transliterators.emplace(t, std::make_unique<TransliteratorInfo>());
  }

  // We need "Hiragana-Katakana" for strings normalization, not for latin transliteration.
  // That's why it is not mentioned in StringUtf8Multilang transliterators list.
  m_transliterators.emplace("Hiragana-Katakana", std::make_unique<TransliteratorInfo>());
  m_inited = true;
}

void Transliteration::SetMode(Mode mode)
{
  m_mode = mode;
}

bool Transliteration::Transliterate(std::string_view transID, icu::UnicodeString & ustr) const
{
  CHECK(m_inited, ());
  ASSERT(!transID.empty(), ());

  auto it = m_transliterators.find(transID);
  if (it == m_transliterators.end())
  {
    LOG(LWARNING, ("Unknown transliterator:", transID));
    return false;
  }

  if (!it->second->m_initialized)
  {
    std::lock_guard<std::mutex> lock(it->second->m_mutex);
    if (!it->second->m_initialized)
    {
      UErrorCode status = U_ZERO_ERROR;
      // Append remove diacritic rule.
      auto const withDiacritic = std::string{transID}.append(";NFD;[\u02B9-\u02D3\u0301-\u0358\u00B7\u0027]Remove;NFC");
      icu::UnicodeString const uTransID(withDiacritic.c_str());

      it->second->m_transliterator.reset(icu::Transliterator::createInstance(uTransID, UTRANS_FORWARD, status));

      if (it->second->m_transliterator == nullptr)
        LOG(LWARNING, ("Cannot create transliterator:", transID, "ICU error =", status));

      it->second->m_initialized = true;
    }
  }

  if (it->second->m_transliterator == nullptr)
    return false;

  it->second->m_transliterator->transliterate(ustr);

  return !ustr.isEmpty();
}

bool Transliteration::TransliterateForce(std::string const & str, std::string const & transliteratorId,
                                         std::string & out) const
{
  CHECK(m_inited, ());
  icu::UnicodeString ustr(str.c_str());
  auto const res = Transliterate(transliteratorId, ustr);
  if (res)
    ustr.toUTF8String(out);
  return res;
}

bool Transliteration::Transliterate(std::string_view sv, int8_t langCode, std::string & out) const
{
  CHECK(m_inited, ());
  if (m_mode != Mode::Enabled)
    return false;

  if (sv.empty() || strings::IsASCIIString(sv))
    return false;

  auto const * transliteratorsIds = StringUtf8Multilang::GetTransliteratorsIdsByCode(langCode);
  if (transliteratorsIds == nullptr || transliteratorsIds->empty())
    return false;

  icu::UnicodeString ustr(sv.data(), static_cast<int32_t>(sv.size()));
  for (auto const & id : *transliteratorsIds)
    Transliterate(id, ustr);

  if (ustr.isEmpty())
    return false;

  ustr.toUTF8String(out);
  return true;
}
