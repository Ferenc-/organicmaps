#include "platform/platform_unix_impl.hpp"
#include "platform/platform.hpp"

#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/scope_guard.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <regex>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(OMIM_OS_MAC) || defined(OMIM_OS_IPHONE)
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif

namespace
{
struct CloseDir
{
  void operator()(DIR * dir) const
  {
    if (dir)
      closedir(dir);
  }
};
}  // namespace

// static
Platform::EError Platform::RmDir(std::string const & dirName)
{
  if (rmdir(dirName.c_str()) != 0)
    return ErrnoToError();
  return ERR_OK;
}

// static
Platform::EError Platform::GetFileType(std::string const & path, EFileType & type)
{
  struct stat stats;
  if (stat(path.c_str(), &stats) != 0)
    return ErrnoToError();
  if (S_ISREG(stats.st_mode))
    type = EFileType::Regular;
  else if (S_ISDIR(stats.st_mode))
    type = EFileType::Directory;
  else
    type = EFileType::Unknown;
  return ERR_OK;
}

// static
bool Platform::IsFileExistsByFullPath(std::string const & filePath)
{
  struct stat s;
  return stat(filePath.c_str(), &s) == 0;
}

#if !defined(OMIM_OS_IPHONE)
// static
void Platform::DisableBackupForFile(std::string const & /*filePath*/) {}
#endif

// static
std::string Platform::GetCurrentWorkingDirectory() noexcept
{
  char path[PATH_MAX];
  char const * const dir = getcwd(path, PATH_MAX);
  if (dir == nullptr)
    return {};
  return dir;
}

bool Platform::IsDirectoryEmpty(std::string const & directory)
{
  std::unique_ptr<DIR, CloseDir> dir(opendir(directory.c_str()));
  if (!dir)
    return true;

  struct dirent * entry;

  // Invariant: all files met so far are "." or "..".
  while ((entry = readdir(dir.get())) != nullptr)
  {
    // A file is not a special UNIX file. Early exit here.
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
      return false;
  }
  return true;
}

bool Platform::GetFileSizeByFullPath(std::string const & filePath, uint64_t & size)
{
  struct stat s;
  if (stat(filePath.c_str(), &s) == 0)
  {
    size = s.st_size;
    return true;
  }
  else
    return false;
}

Platform::TStorageStatus Platform::GetWritableStorageStatus(uint64_t neededSize) const
{
  struct statfs st;
  int const ret = statfs(m_writableDir.c_str(), &st);

  LOG(LINFO, ("statfs return =", ret, "; block size =", st.f_bsize, "; blocks available =", st.f_bavail));

  if (ret != 0)
  {
    LOG(LERROR, ("Path:", m_writableDir, "statfs error:", ErrnoToError()));
    return STORAGE_DISCONNECTED;
  }

  auto const availableBytes = st.f_bsize * st.f_bavail;
  LOG(LINFO, ("Free space check: requested =", neededSize, "; available =", availableBytes));
  if (availableBytes < neededSize)
    return NOT_ENOUGH_SPACE;

  return STORAGE_OK;
}

namespace pl
{

void EnumerateFiles(std::string const & directory, std::function<void(char const *)> const & fn)
{
  std::unique_ptr<DIR, CloseDir> dir(opendir(directory.c_str()));
  if (!dir)
    return;

  struct dirent * entry;
  while ((entry = readdir(dir.get())) != 0)
    fn(entry->d_name);
}

void EnumerateFilesByRegExp(std::string const & directory, std::string const & regexp, std::vector<std::string> & res)
{
  std::regex exp(regexp);
  EnumerateFiles(directory, [&](char const * entry)
  {
    std::string const name(entry);
    if (std::regex_search(name.begin(), name.end(), exp))
      res.push_back(name);
  });
}

}  // namespace pl
