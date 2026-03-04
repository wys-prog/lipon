#pragma once

#include <curl/curl.h>
#include <fstream>
#include <stdexcept>
#include <string>

namespace lipon {

class lipon_Downloader {
public:
  lipon_Downloader() { curl_global_init(CURL_GLOBAL_DEFAULT); }

  ~lipon_Downloader() { curl_global_cleanup(); }

  static bool download(const std::string &url, const std::string &outputPath,
                       long timeoutSeconds = 30) {
    CURL *curl = curl_easy_init();
    if (!curl)
      throw std::runtime_error("Failed to init curl");

    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
      curl_easy_cleanup(curl);
      throw std::runtime_error("Failed to open output file");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);
    file.close();

    return (res == CURLE_OK);
  }

private:
  static size_t writeCallback(void *ptr, size_t size, size_t nmemb,
                              void *stream) {
    std::ofstream *file = static_cast<std::ofstream *>(stream);
    file->write(static_cast<char *>(ptr), size * nmemb);
    return size * nmemb;
  }
};
} // namespace lipon
