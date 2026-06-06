#include "../../include/http/HttpResponse.h"

namespace http
{

// 封装响应头，具体响应体部分在业务逻辑部分封装
// HTTP/1.1 200 OK\r\n           ← 状态行
// Connection: Keep-Alive\r\n    ← 连接模式
// Content-Type: text/html\r\n   ← 自定义 Header
// Content-Length: 13\r\n
// \r\n                           ← 空行分隔
// <html>...</html>               ← Body
void HttpResponse::appendToBuffer(muduo::net::Buffer* outputBuf) const
{
    // HttpResponse封装的信息格式化输出
    char buf[32]; 
    // 为什么不把状态信息放入格式化字符串中，因为状态信息有长有短，不方便定义一个固定大小的内存存储
    snprintf(buf, sizeof buf, "%s %d ", httpVersion_.c_str(), statusCode_);
    
    outputBuf->append(buf);
    outputBuf->append(statusMessage_);
    outputBuf->append("\r\n");

    if (closeConnection_)
    {
        outputBuf->append("Connection: close\r\n");
    }
    else
    {
        //snprintf(buf, sizeof buf, "Content-Length: %zd\r\n", body_.size());
        //outputBuf->append(buf);
        outputBuf->append("Connection: Keep-Alive\r\n");
    }

    for (const auto& header : headers_)
    { // 为什么这里不用格式化字符串？因为key和value的长度不定
        outputBuf->append(header.first);
        outputBuf->append(": "); 
        outputBuf->append(header.second);
        outputBuf->append("\r\n");
    }
    outputBuf->append("\r\n");
    
    outputBuf->append(body_);
}

void HttpResponse::setStatusLine(const std::string& version,
                                 HttpStatusCode statusCode,
                                 const std::string& statusMessage)
{
    httpVersion_ = version;
    statusCode_ = statusCode;
    statusMessage_ = statusMessage;
}

} // namespace http