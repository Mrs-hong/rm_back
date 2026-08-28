#include <memory>

#include "curl/curl_http_client.h"

namespace qifeng {

    // 流式写回调的上下文
    struct StreamContext {
        StreamChunkCallback onChunk;
    };

    size_t CurlHttpClient::StreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userdata) {
        size_t total = size * nmemb;
        auto* ctx = static_cast<StreamContext*>(userdata);
        if (ctx && ctx->onChunk) {
            std::string chunk(static_cast<char*>(contents), total);
            // 原始 chunk，外部自行喂给 SSEParser
            ctx->onChunk(chunk);
        }
        return total;
    }

    CurlHttpClient::CurlHttpClient() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        mCurl = curl_easy_init();
    }

    CurlHttpClient::~CurlHttpClient() {
        if (mCurl) {
            curl_easy_cleanup(mCurl);
        }

        curl_global_cleanup();
    }

    bool CurlHttpClient::PostStream(const std::string& url, const std::string& body,
                                    const std::map<std::string, std::string>& headers, StreamChunkCallback onChunk) {
        if (!mCurl || !onChunk) {
            return false;
        }

        curl_easy_setopt(mCurl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(mCurl, CURLOPT_POST, 1L);
        curl_easy_setopt(mCurl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(mCurl, CURLOPT_POSTFIELDSIZE, body.size());
        curl_easy_setopt(mCurl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);

        auto ctx = std::make_shared<StreamContext>();
        ctx->onChunk = std::move(onChunk);
        curl_easy_setopt(mCurl, CURLOPT_WRITEDATA, ctx.get());

        struct curl_slist* headerList = nullptr;
        for (const auto& [k, v] : headers) {
            std::string h = k + ": " + v;
            headerList = curl_slist_append(headerList, h.c_str());
        }
        curl_easy_setopt(mCurl, CURLOPT_HTTPHEADER, headerList);

        // 流式请求不要设置整体超时，但要设连接超时
        curl_easy_setopt(mCurl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(mCurl, CURLOPT_CONNECTTIMEOUT, 10L);

        CURLcode res = curl_easy_perform(mCurl);
        curl_slist_free_all(headerList);
        return res == CURLE_OK;
    }

}  // namespace qifeng