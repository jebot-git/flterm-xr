// SPDX-License-Identifier: GPL-3.0-or-later
#include "Fl_Term.h"
#include <cctype>

static bool validLink(std::string_view uri) {
  if (uri.empty() || uri.size() > 4096)
    return false;
  for (unsigned char c : uri)
    if (c <= 32 || c == 127)
      return false;
  auto colon = uri.find(':');
  if (colon == uri.npos || !colon || !std::isalpha((unsigned char)uri[0]))
    return false;
  for (char c : uri.substr(0, colon))
    if (!std::isalnum((unsigned char)c) && c != '+' && c != '-' && c != '.')
      return false;
  // These are not document locations and must never reach a URL handler.
  std::string scheme(uri.substr(0, colon));
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return scheme != "javascript" && scheme != "data" && scheme != "vbscript";
}
void Fl_Term::hyperlink(std::string_view payload) {
  auto separator = payload.find(';', 2);
  active_link_ = 0;
  if (separator == payload.npos)
    return;
  auto uri = payload.substr(separator + 1);
  if (!validLink(uri))
    return;
  auto found = std::find(link_uris_.begin(), link_uris_.end(), uri);
  if (found != link_uris_.end())
    active_link_ = found - link_uris_.begin();
  else if (link_uris_.size() < 65536) {
    active_link_ = link_uris_.size();
    link_uris_.emplace_back(uri);
  }
}
std::string Fl_Term::linkAt(int column, int row) {
  std::lock_guard<std::mutex> lock(append_mtx);
  if (row < 0 || row >= size_y || column < 0 || column >= size_x)
    return {};
  row += viewTop();
  int at = cellOffset(row, column);
  if (at >= line[row + 1])
    return {};
  if (link_ids_[at])
    return link_uris_[link_ids_[at]];
  int first = row, last = row;
  auto wrapped = [&](int r) {
    return line[r + 1] > line[r] && buff[line[r + 1] - 1] != '\n' &&
           cellColumn(r, line[r + 1]) >= size_x;
  };
  while (first > 0 && line[row] - line[first] < 8192 && wrapped(first - 1))
    --first;
  while (last < screen_y + size_y - 1 && line[last + 1] - line[row] < 8192 &&
         wrapped(last))
    ++last;
  const int begin = line[first], end = std::max(begin, line[last + 1]);
  auto delimiter = [](unsigned char c) {
    return c <= 32 || c == 127 || c == '<' || c == '>' || c == '"' || c == '\'';
  };
  int left = at, right = at;
  while (left > begin && at - left <= 4096 && !delimiter(buff[left - 1]))
    --left;
  while (right < end && right - left <= 4096 && !delimiter(buff[right]))
    ++right;
  if (right - left > 4096)
    return {};
  std::string token(buff + left, right - left), lower = token;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  size_t start = token.npos;
  bool web = false, email = false;
  for (auto scheme : {"https://", "http://", "ftp://", "ftps://", "file://",
                      "ssh://", "mailto:"}) {
    auto found = lower.find(scheme);
    if (found < start)
      start = found;
  }
  if (start == token.npos) {
    start = lower.find("www.");
    web = start != token.npos;
  }
  if (start == token.npos) {
    auto address = token.find('@');
    if (address == token.npos || !address)
      return {};
    auto local = [](unsigned char c) {
      return std::isalnum(c) ||
             std::string_view(".!#$%&*+/=?^_`{|}~-").find(c) !=
                 std::string_view::npos;
    };
    start = address;
    while (start && local(token[start - 1]))
      --start;
    size_t stop = address + 1;
    while (stop < token.size() && (std::isalnum((unsigned char)token[stop]) ||
                                   token[stop] == '.' || token[stop] == '-'))
      ++stop;
    token.resize(stop);
    email = true;
  }
  std::string uri = token.substr(start);
  while (!uri.empty() &&
         std::string_view(".,;:!?").find(uri.back()) != std::string_view::npos)
    uri.pop_back();
  for (auto [opening, closing] :
       {std::pair{'(', ')'}, std::pair{'[', ']'}, std::pair{'{', '}'}}) {
    int excess = std::count(uri.begin(), uri.end(), closing) -
                 std::count(uri.begin(), uri.end(), opening);
    while (excess > 0 && !uri.empty() && uri.back() == closing) {
      uri.pop_back();
      --excess;
    }
  }
  size_t offset = at - left;
  if (offset < start || offset >= start + uri.size())
    return {};
  if (email) {
    auto address = uri.find('@'), dot = uri.rfind('.');
    if (address == 0 || dot == uri.npos || dot <= address + 1 ||
        dot + 2 >= uri.size())
      return {};
    for (unsigned char c : std::string_view(uri).substr(dot + 1))
      if (!std::isalpha(c))
        return {};
    uri = "mailto:" + uri;
  } else if (web)
    uri = "https://" + uri;
  return validLink(uri) ? uri : std::string{};
}
