//
// Created by Hanhong Wong on 2026/6/6.
//
// 大文件分片上传 + 断点续传 — 应用层协议
// 每个 chunk 是一次标准 POST，无需改 I/O 层
//
// API:
//   POST /api/upload/init             创建上传会话
//   POST /api/upload/<id>/chunk/<n>   上传第 n 片
//   GET  /api/upload/<id>/status      查询上传进度
//   POST /api/upload/<id>/complete    合并所有片段
//

#ifndef MYWEBSERVER_UPLOAD_ROUTES_H
#define MYWEBSERVER_UPLOAD_ROUTES_H

#include <string>
#include <fstream>
#include <sstream>
#include <random>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>

#include "Router.h"
#include "RouteHandler.h"
#include "../http_conn/HttpRequest.h"
#include "../http_conn/HttpResponse.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

// 安全 JSON 解析：解析失败返回 null，不抛异常
inline json safe_parse(std::string_view body) {
    try { return json::parse(body); }
    catch (...) { return {}; }
}

// POSIX 文件操作（避免 <filesystem> 的兼容问题）
namespace {
    bool dir_exists(const std::string& path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    bool file_exists(const std::string& path) {
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }
    bool make_dir(const std::string& path) {
        std::string cmd = "mkdir -p \"" + path + "\"";
        return system(cmd.c_str()) == 0;
    }
    bool remove_dir(const std::string& path) {
        std::string cmd = "rm -rf \"" + path + "\"";
        return system(cmd.c_str()) == 0;
    }
    uintmax_t file_size(const std::string& path) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0) return st.st_size;
        return 0;
    }
}

// =============================================================================
// 工具函数
// =============================================================================

// 生成 32 字符 hex ID（上传会话标识）
inline std::string make_id() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen)
        << std::setw(16) << dis(gen);
    return oss.str();
}

// 读取 / 写入 manifest.json

inline json load_manifest(const std::string& dir) {
    std::ifstream f(dir + "/manifest.json");
    if (!f) return {};
    try { return json::parse(f); }
    catch (...) { return {}; }
}

inline void save_manifest(const std::string& dir, const json& m) {
    std::ofstream f(dir + "/manifest.json");
    f << m.dump();
}

inline std::string chunk_dir(const std::string& upload_id) {
    return "./root/files/chunks/" + upload_id;
}

// =============================================================================
// POST /api/upload/init — 创建上传会话
// =============================================================================
class UploadInitHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        json info = safe_parse(req.get_content());
        std::string filename  = info.value("filename", "unknown");
        size_t total_size     = info.value("total_size", 0);
        size_t chunk_size     = info.value("chunk_size", 5ULL * 1024 * 1024);
        size_t total_chunks   = info.value("total_chunks", 0);

        if (total_chunks == 0 && chunk_size > 0) {
            total_chunks = (total_size + chunk_size - 1) / chunk_size;
        }

        std::string upload_id = make_id();
        std::string dir = chunk_dir(upload_id);

        if (!make_dir(dir)) {
            json err = {{"status","error"},{"message","cannot create directory"}};
            auto s = err.dump();
            return resp.send_body(s.c_str(), s.size(), "application/json");
        }

        json manifest = {
            {"filename", filename},
            {"total_size", total_size},
            {"chunk_size", chunk_size},
            {"total_chunks", total_chunks},
            {"uploaded_chunks", json::array()},
            {"created_at", std::time(nullptr)}
        };
        save_manifest(dir, manifest);

        json reply = {
            {"status", "ok"},
            {"upload_id", upload_id},
            {"total_chunks", total_chunks},
            {"chunk_size", chunk_size}
        };
        auto s = reply.dump();
        return resp.send_body(s.c_str(), s.size(), "application/json");
    }
};

// =============================================================================
// POST /api/upload/<id>/chunk/<n> — 上传第 n 片
// =============================================================================
class UploadChunkHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto upload_id = req.get_path_parameters("upload_id");
        auto chunk_n   = req.get_path_parameters("chunk_n");
        auto dir = chunk_dir(upload_id);

        // 检查会话是否存在
        if (!file_exists(dir + "/manifest.json")) {
            json err = {{"status","error"},{"message","session not found"}};
            auto s = err.dump();
            return resp.send_body(s.c_str(), s.size(), "application/json");
        }

        // 把 body 写入 chunk 文件
        auto body = req.get_content();
        std::string chunk_path = dir + "/chunk_" + chunk_n;
        std::ofstream f(chunk_path, std::ios::binary);
        f.write(body.data(), body.size());

        // 更新 manifest
        json m = load_manifest(dir);
        if (m.is_null()) {
            json err = {{"status","error"},{"message","manifest corrupted"}};
            auto s = err.dump();
            return resp.send_body(s.c_str(), s.size(), "application/json");
        }

        int n = std::stoi(chunk_n);
        auto& arr = m["uploaded_chunks"];
        if (!arr.is_array()) arr = json::array();
        bool found = false;
        for (auto& v : arr) { if (v == n) { found = true; break; } }
        if (!found) arr.push_back(n);
        save_manifest(dir, m);

        json reply = {{"status","ok"},{"chunk",n},{"size",body.size()}};
        auto s = reply.dump();
        return resp.send_body(s.c_str(), s.size(), "application/json");
    }
};

// =============================================================================
// GET /api/upload/<id>/status — 查询进度（断点续传的核心）
// =============================================================================
class UploadStatusHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto upload_id = req.get_path_parameters("upload_id");
        auto dir = chunk_dir(upload_id);
        json m = load_manifest(dir);

        if (m.is_null()) {
            json err = {{"status","error"},{"message","session not found"}};
            auto s = err.dump();
            return resp.send_body(s.c_str(), s.size(), "application/json");
        }

        // 扫描实际存在的 chunk 文件（比 manifest 更可靠）
        json uploaded = json::array();
        json missing  = json::array();
        int total = m["total_chunks"];

        for (int i = 0; i < total; i++) {
            if (file_exists(dir + "/chunk_" + std::to_string(i))) {
                uploaded.push_back(i);
            } else {
                missing.push_back(i);
            }
        }

        json reply = {
            {"status", "ok"},
            {"upload_id", upload_id},
            {"total_chunks", total},
            {"uploaded", uploaded},
            {"missing", missing},
            {"progress_pct", uploaded.size() * 100.0 / total}
        };
        auto s = reply.dump();
        return resp.send_body(s.c_str(), s.size(), "application/json");
    }
};

// =============================================================================
// POST /api/upload/<id>/complete — 合并所有片段 → 最终文件
// =============================================================================
class UploadCompleteHandler : public RouteHandler {
public:
    bool handle(HttpRequest& req, HttpResponse& resp) override {
        auto upload_id = req.get_path_parameters("upload_id");
        auto dir = chunk_dir(upload_id);
        json m = load_manifest(dir);

        if (m.is_null()) {
            json err = {{"status","error"},{"message","session not found"}};
            auto s = err.dump();
            return resp.send_body(s.c_str(), s.size(), "application/json");
        }

        // 检查是否所有 chunk 都上传完成
        int total = m["total_chunks"];
        for (int i = 0; i < total; i++) {
            if (!file_exists(dir + "/chunk_" + std::to_string(i))) {
                json err = {{"status","error"},
                            {"message","missing chunk " + std::to_string(i)}};
                auto s = err.dump();
                return resp.send_body(s.c_str(), s.size(), "application/json");
            }
        }

        // 合并所有 chunk 到最终文件
        std::string final_uuid = make_id();
        std::string final_path = "./root/files/" + final_uuid;
        make_dir("./root/files/");

        std::ofstream out(final_path, std::ios::binary);
        size_t total_bytes = 0;
        for (int i = 0; i < total; i++) {
            std::string path = dir + "/chunk_" + std::to_string(i);
            std::ifstream in(path, std::ios::binary);
            out << in.rdbuf();
            total_bytes += file_size(path);
        }

        // 清理 chunk 目录
        remove_dir(dir);

        json reply = {
            {"status", "ok"},
            {"uuid", final_uuid},
            {"filename", m["filename"]},
            {"size_bytes", total_bytes}
        };
        auto s = reply.dump();
        return resp.send_body(s.c_str(), s.size(), "application/json");
    }
};

// =============================================================================
// 注册所有上传相关路由
// =============================================================================
inline void registerUploadRoutes(Router& router) {
    router.addRoute(Method::POST, "/api/upload/init",
                    std::make_unique<UploadInitHandler>());
    router.addRoute(Method::POST, "/api/upload/:upload_id/chunk/:chunk_n",
                    std::make_unique<UploadChunkHandler>());
    router.addRoute(Method::GET, "/api/upload/:upload_id/status",
                    std::make_unique<UploadStatusHandler>());
    router.addRoute(Method::POST, "/api/upload/:upload_id/complete",
                    std::make_unique<UploadCompleteHandler>());
}

#endif //MYWEBSERVER_UPLOAD_ROUTES_H
