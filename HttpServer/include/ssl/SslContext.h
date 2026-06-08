#pragma once
#include "SslConfig.h"
#include <openssl/ssl.h>
#include <memory>
#include <muduo/base/noncopyable.h>

namespace ssl 
{

class SslContext : muduo::noncopyable 
{
public:
    explicit SslContext(const SslConfig& config);  // 保存配置，ctx_ 初始为空
    ~SslContext();                                 // 释放 SSL_CTX

    bool initialize();                             // 完整初始化：OpenSSL → CTX → 证书 → 协议 → 会话缓存
    SSL_CTX* getNativeHandle() { return ctx_; }     // 获取原生 OpenSSL 上下文句柄

private:
    bool loadCertificates();                       // 加载证书、私钥（校验匹配）及可选证书链
    bool setupProtocol();                          // 设置协议版本范围和加密套件列表
    void setupSessionCache();                      // 启用服务端会话缓存及超时配置
    static void handleSslError(const char* msg);   // 获取 OpenSSL 错误队列并记录日志

private:
    SSL_CTX*  ctx_; // SSL上下文
    SslConfig config_; // SSL配置
};

} // namespace ssl