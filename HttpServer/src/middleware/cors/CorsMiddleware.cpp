#include "../../../include/middleware/cors/CorsMiddleware.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <muduo/base/Logging.h>

namespace http 
{
namespace middleware 
{

CorsMiddleware::CorsMiddleware(const CorsConfig& config) : config_(config) {}

// 处理请求前的中间件：检查是否为CORS预检请求（OPTIONS），不与业务处理混杂
// 如果是预检请求，直接返回 204 No Content
// 如果不是预检请求，继续处理后续中间件
void CorsMiddleware::before(HttpRequest& request) 
{
    LOG_DEBUG << "CorsMiddleware::before - Processing request";
    
    if (request.method() == HttpRequest::Method::kOptions) 
    {
        LOG_INFO << "Processing CORS preflight request";
        HttpResponse response;
        handlePreflightRequest(request, response);
        throw response;
    }
}

// 给正常业务响应追加 CORS 响应头，让浏览器放行跨域请求
void CorsMiddleware::after(HttpResponse& response) 
{
    LOG_DEBUG << "CorsMiddleware::after - Processing response";
    
    // 直接添加CORS头，简化处理逻辑
    if (!config_.allowedOrigins.empty()) 
    {
        // 如果允许所有源
        if (std::find(config_.allowedOrigins.begin(), config_.allowedOrigins.end(), "*") 
            != config_.allowedOrigins.end()) 
        {
            addCorsHeaders(response, "*");
        }
        else 
        {
            // 添加第一个允许的源
            addCorsHeaders(response, config_.allowedOrigins[0]);
        }
    }
}

// 检查源是否被允许
// 如果配置中没有指定允许的源（空），或允许所有源（*），或请求头中包含允许的源，返回 true
bool CorsMiddleware::isOriginAllowed(const std::string& origin) const 
{
    return config_.allowedOrigins.empty() || 
           std::find(config_.allowedOrigins.begin(), 
                    config_.allowedOrigins.end(), "*") != config_.allowedOrigins.end() ||
           std::find(config_.allowedOrigins.begin(), 
                    config_.allowedOrigins.end(), origin) != config_.allowedOrigins.end();
}

void CorsMiddleware::handlePreflightRequest(const HttpRequest& request, 
                                          HttpResponse& response) 
{
    const std::string& origin = request.getHeader("Origin");
    
    if (!isOriginAllowed(origin)) 
    {
        LOG_WARN << "Origin not allowed: " << origin;
        response.setStatusCode(HttpResponse::k403Forbidden);
        return;
    }

    addCorsHeaders(response, origin);
    response.setStatusCode(HttpResponse::k204NoContent);
    LOG_INFO << "Preflight request processed successfully";
}

void CorsMiddleware::addCorsHeaders(HttpResponse& response, 
                                  const std::string& origin) 
{
    try 
    {
        response.addHeader("Access-Control-Allow-Origin", origin);
        
        if (config_.allowCredentials) 
        {
            response.addHeader("Access-Control-Allow-Origin", "true");
        }
        
        if (!config_.allowedMethods.empty()) 
        {
            response.addHeader("Access-Control-Allow-Methods", 
                             join(config_.allowedMethods, ", "));
        }
        
        if (!config_.allowedHeaders.empty()) 
        {
            response.addHeader("Access-Control-Allow-Headers", 
                             join(config_.allowedHeaders, ", "));
        }
        
        response.addHeader("Access-Control-Max-Age", 
                          std::to_string(config_.maxAge));
        
        LOG_DEBUG << "CORS headers added successfully";
    }
    catch (const std::exception& e) 
    {
        LOG_ERROR << "Error adding CORS headers: " << e.what();
    }
}

// 工具函数：将字符串数组连接成单个字符串
std::string CorsMiddleware::join(const std::vector<std::string>& strings, const std::string& delimiter) 
{
    std::ostringstream result;
    for (size_t i = 0; i < strings.size(); ++i) 
    {
        if (i > 0) result << delimiter;
        result << strings[i];
    }
    return result.str();
}

} // namespace middleware
} // namespace http