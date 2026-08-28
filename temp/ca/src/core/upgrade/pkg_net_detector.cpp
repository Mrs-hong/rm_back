//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "json/reader.h"
#include "json/value.h"
#include "qifeng_framework/common/logger.h"

#include "common/ota_cipher.h"
#include "core/upgrade/pkg_net_detector.h"

namespace qifeng_ca {

    // 设备型号标识: 云端 getNewVersion 接口按型号返回对应的升级包
    static constexpr const char* kDeviceModel = "m50";

    // shell single-quote escape: each ' replaced with '\''
    static std::string ShellQuote(const std::string &s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += "'";
        return out;
    }

    // URL 编码 query 参数值(RFC 3986, 保留 A-Za-z0-9-_.~)
    // sn / filename 中可能含特殊字符, 拼接到 URL 前需编码避免破坏 query 结构
    static std::string UrlEncode(const std::string &s) {
        static constexpr const char* HexTable = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (char ch : s) {
            unsigned char c = static_cast<unsigned char>(ch);
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~') {
                out.push_back(static_cast<char>(c));
            } else {
                out.push_back('%');
                out.push_back(HexTable[c >> 4]);
                out.push_back(HexTable[c & 0x0F]);
            }
        }
        return out;
    }

    // HTTP(S) GET via system curl (separate process), write response body to outBody.
    // Using system curl avoids in-process libcurl + OpenSSL version conflicts
    // (ARM crash root cause: libcurl NEEDED libssl.so.3 vs deps libssl.so SONAME mismatch).
    // curl flags: -s silent, -L follow redirects, -f fail on HTTP 4xx/5xx
    // 启用 TLS 证书校验: 云端证书已可信, 不再跳过校验
    static bool HttpGetToString(const std::string &url, std::string &outBody, long timeoutSec = 30) {
        std::string cmd =
            "curl -sLf --connect-timeout 10 --max-time " + std::to_string(timeoutSec) + " " + ShellQuote(url);
        FILE* fp = popen(cmd.c_str(), "r");
        if (fp == nullptr) {
            SLOG_ERROR << "PkgNetDetector: popen curl failed, url=" << url;
            return false;
        }
        outBody.clear();
        std::array<char, 4096> buf {};
        while (size_t n = fread(buf.data(), 1, buf.size(), fp)) {
            outBody.append(buf.data(), n);
        }
        int rc = pclose(fp);
        if (rc != 0) {
            SLOG_ERROR << "PkgNetDetector: curl GET failed, url=" << url << ", rc=" << rc;
            return false;
        }
        return true;
    }

    // Download URL to local file via system curl (separate process).
    // Same rationale as HttpGetToString: avoid SSL library conflicts.
    // 启用 TLS 证书校验: 云端证书已可信, 不再跳过校验
    // timeoutSec 默认 7200s(2小时): 边缘网络带宽低, 大包(百MB级)下载耗时可能很长
    static bool DownloadToFile(const std::string &url, const std::string &localPath, long timeoutSec = 7200) {
        std::string cmd = "curl -sLf --connect-timeout 10 --max-time " + std::to_string(timeoutSec) + " -o " +
                          ShellQuote(localPath) + " " + ShellQuote(url);
        int rc = system(cmd.c_str());
        if (rc != 0) {
            SLOG_ERROR << "PkgNetDetector: curl download failed, url=" << url << ", rc=" << rc;
            std::error_code ec;
            std::filesystem::remove(localPath, ec);
            return false;
        }
        return true;
    }

    // AES-256-CBC + PBKDF2 解密: openssl enc -d -aes-256-cbc -pbkdf2 -in <enc> -out <plain> -pass pass:<pwd>
    // 使用系统 openssl 子进程, 避免在进程内直接绑定 OpenSSL 版本(与 HttpGetToString 选 curl 同理)
    // 成功后删除加密文件, 仅保留解密后的明文
    static bool AesDecrypt(const std::string &encPath, const std::string &plainPath, const std::string &password) {
        SLOG_INFO << "PkgNetDetector: aes decrypt start, enc=" << encPath << " -> plain=" << plainPath;
        std::string cmd = "openssl enc -d -aes-256-cbc -pbkdf2 -in " + ShellQuote(encPath) + " -out " +
                          ShellQuote(plainPath) + " -pass pass:" + ShellQuote(password);
        int rc = system(cmd.c_str());
        if (rc != 0) {
            SLOG_ERROR << "PkgNetDetector: aes decrypt failed, enc=" << encPath << ", plain=" << plainPath
                       << ", rc=" << rc;
            std::error_code ec;
            std::filesystem::remove(plainPath, ec);
            return false;
        }
        // 解密成功后删除加密包(仅保留明文 tar.gz 供后续校验/部署)
        std::error_code ec;
        std::filesystem::remove(encPath, ec);
        SLOG_INFO << "PkgNetDetector: aes decrypt ok, plain=" << plainPath;
        return true;
    }

    // 去掉末尾 '/' 的 cloud base url
    static std::string TrimCloudBase(const std::string &url) {
        std::string base = url;
        while (!base.empty() && base.back() == '/') {
            base.pop_back();
        }
        return base;
    }

    // 拼接下载 URL: {base}/download?sn=<sn>&filename=<filename>
    // filename 为云端 getNewVersion 返回的 soft_url 原值(可能含路径前缀如 "m50/xxx.aes"),
    // 原样拼接保留 '/', 不做 UrlEncode: 服务端按完整路径名定位文件;
    // 若编码为 %2F 服务端可能无法匹配 → 404
    // sn 可能含特殊字符, 仍需 UrlEncode 避免破坏 query 结构
    static std::string BuildDownloadUrl(const std::string &base, const std::string &sn, const std::string &filename) {
        return base + "/download?sn=" + UrlEncode(sn) + "&filename=" + filename;
    }

    // 从云端返回的 soft_url/sha256_url 中提取纯文件名(剥离路径前缀)
    // 云端返回 "m50/qifeng_upgrade_1.1.1.tar.gz.aes" 时, 本地保存路径需用纯文件名:
    // 避免 localDownloadPath 含未创建的子目录导致 curl -o 写入失败
    // (URL 拼接仍用完整值, 仅本地保存剥离)
    static std::string ExtractFileName(const std::string &pathLike) {
        auto pos = pathLike.find_last_of('/');
        if (pos == std::string::npos) {
            return pathLike;  // 无路径分隔符, 原样返回
        }
        return pathLike.substr(pos + 1);
    }

    // 去除 .aes 后缀得到明文文件名; 无 .aes 后缀时原样返回
    // (解密后文件名可能与下载原文件同名, 通过 tmp 目录隔离避免互相覆盖)
    static std::string StripAesSuffix(const std::string &encFileName) {
        const std::string kAesSuffix = ".aes";
        std::string plain = encFileName;
        if (plain.size() >= kAesSuffix.size() &&
            plain.compare(plain.size() - kAesSuffix.size(), kAesSuffix.size(), kAesSuffix) == 0) {
            plain.erase(plain.size() - kAesSuffix.size());
        }
        return plain;
    }

    PkgNetDetector::PkgNetDetector(std::string currentVersion, std::string savePath, std::string cloudBaseUrl,
                                   std::string sophonSn)
        : UpgradeDetectorBase(std::move(currentVersion), std::move(savePath)), mCloudBaseUrl(std::move(cloudBaseUrl)),
          mSophonSn(std::move(sophonSn)) {
    }

    std::vector<PackageInfo> PkgNetDetector::FindUpgradePackage() {
        std::vector<PackageInfo> candidates;
        // 查询新版本接口: {base}/getNewVersion?sn=<设备SN>&dev=<设备型号>
        std::string listUrl =
            TrimCloudBase(mCloudBaseUrl) + "/getNewVersion?sn=" + UrlEncode(mSophonSn) + "&dev=" + kDeviceModel;

        std::string body;
        SLOG_INFO << "PkgNetDetector: fetch version list, url=" << listUrl;
        if (!HttpGetToString(listUrl, body)) {
            SLOG_WARN << "PkgNetDetector: fetch list failed, url=" << listUrl;
            return candidates;
        }
        SLOG_INFO << "PkgNetDetector: fetch list ok, body_len=" << body.size();

        // parse JSON: {"version","soft_url","sha256_url","size","password"} (single object, not array)
        // soft_url / sha256_url 为文件名(非完整URL), 需与 download 接口拼接
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(body.c_str(), body.c_str() + body.size(), &root, &errs)) {
            SLOG_ERROR << "PkgNetDetector: parse version json failed: " << errs << ", body=" << body;
            return candidates;
        }

        PackageInfo info;
        info.source = "net";
        info.package_version = root.get("version", "").asString();
        info.package_path = root.get("soft_url", "").asString();   // Find phase: 下载文件名
        info.sha256_path = root.get("sha256_url", "").asString();  // Find phase: 下载文件名
        info.password = root.get("password", "").asString();       // AES 解密密钥(混淆密文)
        info.package_name = info.package_path;                     // 取文件名(便于后续本地路径命名)
        // 不强制 .aes 后缀: 下载后统一走解密, 解密失败视为非法包
        if (info.package_version.empty() || info.package_path.empty()) {
            SLOG_WARN << "PkgNetDetector: skip invalid package, version=" << info.package_version
                      << ", soft_url=" << info.package_path << ", password_empty=" << info.password.empty();
            return candidates;
        }
        // 先打印日志再 move: move 后 info 字符串会被清空, 无法再输出
        SLOG_INFO << "PkgNetDetector: found 1 package, version=" << info.package_version
                  << ", soft_url=" << info.package_path << ", sha256_url=" << info.sha256_path
                  << ", has_password=" << !info.password.empty();
        candidates.push_back(std::move(info));
        return candidates;
    }

    Status PkgNetDetector::PrepareUpgradePackage(const PackageInfo &candidate, PackageInfo &out) {
        out = candidate;
        out.source = "net";

        // 确保保存目录存在
        std::error_code ec;
        std::filesystem::create_directories(mSavePath, ec);
        if (ec) {
            SLOG_ERROR << "PkgNetDetector: create save dir failed, dir=" << mSavePath << ", err=" << ec.message();
            return Status {-1, "创建下载目录失败: " + ec.message()};
        }

        // 临时子目录: 解密输出先落在此, 避免解密后文件与下载原文件同名时互相覆盖
        std::string tmpDir = mSavePath + "/download_tmp";
        std::filesystem::create_directories(tmpDir, ec);
        if (ec) {
            SLOG_ERROR << "PkgNetDetector: create download tmp dir failed, dir=" << tmpDir << ", err=" << ec.message();
            return Status {-1, "创建临时目录失败: " + ec.message()};
        }

        std::string base = TrimCloudBase(mCloudBaseUrl);

        // 下载文件名:
        //   urlFileName  - 云端返回的完整值(含路径前缀如 m50/), 用于下载 URL 拼接
        //   localFileName - 剥离路径前缀的纯文件名, 用于本地保存路径(避免未创建子目录)
        // 不强制 .aes 后缀, 统一走解密流程, 解密失败视为非法包
        std::string urlFileName = candidate.package_path;
        if (urlFileName.empty()) {
            urlFileName = "ota_package.tar.gz";
        }
        std::string localFileName = ExtractFileName(urlFileName);

        // 1) 下载软件包到保存目录根
        std::string localDownloadPath = mSavePath + "/" + localFileName;
        std::string pkgUrl = BuildDownloadUrl(base, mSophonSn, urlFileName);
        SLOG_INFO << "PkgNetDetector: downloading package, url=" << pkgUrl << " -> " << localDownloadPath;
        if (!DownloadToFile(pkgUrl, localDownloadPath)) {
            std::filesystem::remove_all(tmpDir, ec);
            return Status {-1, "下载升级包失败"};
        }
        SLOG_INFO << "PkgNetDetector: package downloaded, path=" << localDownloadPath
                  << ", size=" << std::filesystem::file_size(localDownloadPath, ec);

        // 2) 下载 sha256 清单文件(清单内容为明文 tar.gz 的 SHA256)
        std::string shaUrlFileName = candidate.sha256_path;
        if (shaUrlFileName.empty()) {
            shaUrlFileName = urlFileName + ".sha256";  // 兜底: 与包同名 + .sha256
        }
        std::string localShaPath = mSavePath + "/" + ExtractFileName(shaUrlFileName);
        std::string shaUrl = BuildDownloadUrl(base, mSophonSn, shaUrlFileName);
        SLOG_INFO << "PkgNetDetector: downloading sha256 manifest, url=" << shaUrl << " -> " << localShaPath;
        if (!DownloadToFile(shaUrl, localShaPath)) {
            std::filesystem::remove(localDownloadPath, ec);
            std::filesystem::remove_all(tmpDir, ec);
            return Status {-1, "下载sha256清单文件失败"};
        }

        // 3) 解密: 还原 password 后 openssl AES-256-CBC 解密, 输出到 tmp 子目录
        //    (解密后文件名可能与下载原文件同名, 用 tmp 目录隔离避免覆盖; 解密失败视为非法包)
        if (candidate.password.empty()) {
            SLOG_ERROR << "PkgNetDetector: package but password empty, file=" << localFileName;
            std::filesystem::remove(localDownloadPath, ec);
            std::filesystem::remove(localShaPath, ec);
            std::filesystem::remove_all(tmpDir, ec);
            return Status {-1, "升级包缺少解密密钥"};
        }
        // API 返回的 password 为 OtaCipher(hex+字节移位) 混淆密文, 需先还原为明文再交给 openssl
        std::string realPassword = qifeng_ca::ota::OtaCipher::Decrypt(candidate.password);
        if (realPassword.empty()) {
            SLOG_ERROR << "PkgNetDetector: ota cipher decrypt password failed, password_field_len="
                       << candidate.password.size();
            std::filesystem::remove(localDownloadPath, ec);
            std::filesystem::remove(localShaPath, ec);
            std::filesystem::remove_all(tmpDir, ec);
            return Status {-1, "解密密码还原失败"};
        }
        std::string plainFileName = StripAesSuffix(localFileName);
        std::string tmpPlainPath = tmpDir + "/" + plainFileName;
        SLOG_INFO << "PkgNetDetector: decrypting package, enc=" << localDownloadPath << " -> " << tmpPlainPath;
        if (!AesDecrypt(localDownloadPath, tmpPlainPath, realPassword)) {
            // AesDecrypt 失败已清理 tmpPlainPath, 此处清理下载原文件与清单
            std::filesystem::remove(localDownloadPath, ec);
            std::filesystem::remove(localShaPath, ec);
            std::filesystem::remove_all(tmpDir, ec);
            return Status {-1, "解密升级包失败"};
        }

        // 4) 解密成功: 下载原文件已被 AesDecrypt 删除, 将解密后文件从 tmp 移出到保存目录根
        std::string localPkgPath = mSavePath + "/" + plainFileName;
        std::filesystem::rename(tmpPlainPath, localPkgPath, ec);
        if (ec) {
            SLOG_ERROR << "PkgNetDetector: move decrypted package failed, err=" << ec.message();
            std::filesystem::remove(tmpPlainPath, ec);
            std::filesystem::remove(localShaPath, ec);
            std::filesystem::remove_all(tmpDir, ec);
            return Status {-1, "移动解密后升级包失败"};
        }
        // 清理临时目录
        std::filesystem::remove_all(tmpDir, ec);

        out.package_name = plainFileName;
        out.package_path = localPkgPath;
        out.sha256_path = localShaPath;
        SLOG_INFO << "PkgNetDetector: prepare ok, pkg=" << localPkgPath << ", sha=" << localShaPath;
        return {};
    }

}  // namespace qifeng_ca
