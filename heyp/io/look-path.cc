#include "heyp/io/look-path.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace heyp {

std::string LookPath(absl::string_view path) {
  if (absl::StrContains(path, '/')) {
    if (path[0] == '/') {
      return std::string(path);
    }
    size_t size = 64;
    char* buf = new char[size];
    char* got = getcwd(buf, size);

    while (got == nullptr) {
      delete[] buf;
      size *= 2;
      buf = new char[size];
      got = getcwd(buf, size);
    }

    std::string abs_path = absl::StrCat(got, "/", path);
    delete[] buf;
    return abs_path;
  }

  char* env_path = getenv("PATH");
  std::vector<absl::string_view> path_entries = absl::StrSplit(env_path, ":");

  for (absl::string_view path_entry : path_entries) {
    std::string abs_path = absl::StrCat(path_entry, "/", path);
    struct stat ret;
    if (stat(abs_path.c_str(), &ret) == 0) {
      if ((ret.st_mode & S_IFDIR) == 0) {
        return abs_path;
      }
    }
  }

  return std::string(path);  // give up
}

}  // namespace heyp
