#include"../../include/session/SessionManager.h"
#include <iostream>
#include <sstream>

namespace http
{
namespace session
{
// session在具体业务逻辑被调用（route部分）

// 初始化会话管理器，设置会话存储对象（哈希表）和随机数生成器
SessionManager::SessionManager(std::unique_ptr<SessionStorage> storage)
    : storage_(std::move(storage)) 
    , rng_(std::random_device{}()) // 初始化随机数生成器，用于生成随机的会话ID
{}

// 浏览器不会主动生成cookieid，需要服务器端生成
// 浏览器带着旧id，会话管理器会根据id加载会话，会话不存在会创建新的会话并返回新id
// 从请求中获取或创建会话，也就是说，如果请求中包含会话ID，则从存储中加载会话，否则创建一个新的会话
std::shared_ptr<Session> SessionManager::getSession(const HttpRequest& req, HttpResponse* resp)
{
    std::string sessionId = getSessionIdFromCookie(req);
    
    std::shared_ptr<Session> session;

    if (!sessionId.empty())
    {
        session = storage_->load(sessionId);
    }

    if (!session || session->isExpired())
    {
        sessionId = generateSessionId();
        session = std::make_shared<Session>(sessionId, this);
        setSessionCookie(sessionId, resp);
    }
    else
    {
        session->setManager(this); // 为现有会话设置管理器
    }

    session->refresh();
    storage_->save(session);  // 这里可能有问题，需要确保正确保存会话
    return session;
}

// 生成唯一的会话标识符，确保会话的唯一性和安全性
std::string SessionManager::generateSessionId()
{
    std::stringstream ss;
    std::uniform_int_distribution<> dist(0, 15);

    // 生成32个字符的会话ID，每个字符是一个十六进制数字
    for (int i = 0; i < 32; ++i)
    {
        ss << std::hex << dist(rng_);
    }
    return ss.str();
}

void SessionManager::destroySession(const std::string& sessionId)
{
    storage_->remove(sessionId);
}

void SessionManager::cleanExpiredSessions()
{
    storage_->cleanExpired();
}

std::string SessionManager::getSessionIdFromCookie(const HttpRequest& req)
{
    std::string sessionId;
    std::string cookie = req.getHeader("Cookie");

    if (!cookie.empty())
    {
        size_t pos = cookie.find("sessionId=");
        if (pos != std::string::npos)
        {
            pos += 10; // 跳过"sessionId="
            size_t end = cookie.find(';', pos);
            if (end != std::string::npos)
            {
                sessionId = cookie.substr(pos, end - pos);
            }
            else
            {
                sessionId = cookie.substr(pos);
            }
        }
    }
    return sessionId;
}

// 设置cookie作为响应，浏览器保存会话ID到cookie，实现持久对话
void SessionManager::setSessionCookie(const std::string& sessionId, HttpResponse* resp)
{
    // 设置会话ID到响应头中，作为Cookie
    std::string cookie = "sessionId=" + sessionId + "; Path=/; HttpOnly";
    resp->addHeader("Set-Cookie", cookie);
}

} // namespace session
} // namespace http