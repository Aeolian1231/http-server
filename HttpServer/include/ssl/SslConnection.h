#pragma once
#include "SslContext.h"
#include <muduo/net/TcpConnection.h>
#include <muduo/net/Buffer.h>
#include <muduo/base/noncopyable.h>
#include <openssl/ssl.h>
#include <memory>

namespace ssl 
{

// 添加消息回调函数类型定义
using MessageCallback = std::function<void(const std::shared_ptr<muduo::net::TcpConnection>&,
                                         muduo::net::Buffer*,
                                         muduo::Timestamp)>;

class SslConnection : muduo::noncopyable 
{
public:
    using TcpConnectionPtr = std::shared_ptr<muduo::net::TcpConnection>;
    using BufferPtr = muduo::net::Buffer*;
    
    SslConnection(const TcpConnectionPtr& conn, SslContext* ctx); // 创建 SSL 对象和 BIO，绑定回调
    ~SslConnection();                                              // 释放 SSL 对象及关联 BIO

    void startHandshake();                                         // 启动 TLS 握手
    void send(const void* data, size_t len);                       // 发送：SSL加密 → BIO取密文 → TCP发送
    void onRead(const TcpConnectionPtr& conn, BufferPtr buf, muduo::Timestamp time); // 接收：握手态推BIO，已建立则解密回调
    bool isHandshakeCompleted() const { return state_ == SSLState::ESTABLISHED; }     // 握手是否已完成
    muduo::net::Buffer* getDecryptedBuffer() { return &decryptedBuffer_; }            // 获取解密后数据缓冲区
    void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }     // 设置解密后的消息回调
private:
    void handleHandshake();                                        // 执行握手：成功/等待/失败关闭

private:
    SSL*                ssl_; // SSL 连接
    SslContext*         ctx_; // SSL 上下文
    TcpConnectionPtr    conn_; // TCP 连接
    SSLState            state_; // SSL 状态
    BIO*                readBio_;   // 网络数据 -> SSL
    BIO*                writeBio_;  // SSL -> 网络数据
    muduo::net::Buffer  decryptedBuffer_; // 解密后的数据
    MessageCallback     messageCallback_; // 消息回调
};

} // namespace ssl