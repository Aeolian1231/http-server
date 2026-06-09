#include "../../include/router/Router.h"
#include "../../include/utils/LogUtil.h"
#include <muduo/base/Logging.h>

namespace http
{
namespace router
{

// 注册回调函数
// 在业务逻辑（GomokuServer.cpp）的 initializeRouter 方法中调用，用于添加路由处理逻辑
void Router::registerCallback(HttpRequest::Method method, const std::string &path, const HandlerCallback &callback)
{
    RouteKey key{method, path};
    callbacks_[key] = std::move(callback);
}

// 核心路由分发函数，在httpserver中调用，根据请求方法和路径匹配回调函数
// 按优先级依次查找：精确 callback → 动态 callback
// 返回 true 表示找到并执行了对应处理器，false 表示无匹配路由（应返回 404）
bool Router::route(const HttpRequest &req, HttpResponse *resp)
{
    RouteKey key{req.method(), req.path()};

    // 精确匹配回调函数
    auto it = callbacks_.find(key);
    if (it != callbacks_.end())
    {
        // 执行具体业务逻辑，it->second是Lambda函数
        it->second(req, resp);
        LOG_UTIL_INFO("Route matched (exact): method=" << static_cast<int>(req.method())
                      << " path=" << req.path());
        return true;
    }

    // 动态路由匹配
    // URL 包含可变部分，如用户 ID，不能穷举所有值，必须用正则：
    // GET /user/10086   → 动态匹配 "/user/:id"，提取 param1="10086"
    // GET /user/42      → 动态匹配 "/user/:id"，提取 param1="42"
    for (const auto &[method, pathRegex, callback] : regexCallbacks_)
    {
        std::smatch match;
        std::string pathStr(req.path());
        if (method == req.method() && std::regex_match(pathStr, match, pathRegex))
        {
            // 提取路径参数并注入到请求副本中
            HttpRequest newReq(req);
            extractPathParameters(match, newReq);

            callback(newReq, resp);
            LOG_UTIL_INFO("Route matched (dynamic): method=" << static_cast<int>(req.method())
                          << " path=" << req.path());
            return true;
        }
    }

    LOG_UTIL_WARN("No route matched: method=" << static_cast<int>(req.method())
                  << " path=" << req.path());
    return false;
}

} // namespace router
} // namespace http
