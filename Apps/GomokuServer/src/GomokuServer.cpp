#include "../include/GomokuServer.h"
#include "../../../HttpServer/include/http/HttpRequest.h"
#include "../../../HttpServer/include/http/HttpResponse.h"
#include "../../../HttpServer/include/http/HttpServer.h"
#include "../../../HttpServer/include/utils/LogUtil.h"

using namespace http;

GomokuServer::GomokuServer(int port,
                           const std::string &name,
                           muduo::net::TcpServer::Option option)
    : httpServer_(port, name, option), maxOnline_(0)
{
    initialize();
}

void GomokuServer::setThreadNum(int numThreads)
{
    httpServer_.setThreadNum(numThreads);
}

void GomokuServer::start()
{
    httpServer_.start();
}

void GomokuServer::initialize()
{
    // 初始化数据库连接池
    http::MysqlUtil::init("tcp://127.0.0.1:3306", "root", "Root@123123", "Gomoku", 10);
    // 初始化会话
    initializeSession();
    // 初始化中间件
    initializeMiddleware();
    // 初始化路由
    initializeRouter();
}

void GomokuServer::initializeSession()
{
    // 创建会话存储
    auto sessionStorage = std::make_unique<http::session::MemorySessionStorage>();
    // 创建会话管理器
    auto sessionManager = std::make_unique<http::session::SessionManager>(std::move(sessionStorage));
    // 设置会话管理器
    setSessionManager(std::move(sessionManager));
}

void GomokuServer::initializeMiddleware()
{
    // 创建中间件
    auto corsMiddleware = std::make_shared<http::middleware::CorsMiddleware>();
    // 添加中间件
    httpServer_.addMiddleware(corsMiddleware);
}

// session管理
// handleEntry (237)	        没有	返回静态 HTML 入口页，谁都能看
// handleRegister (363)	        没有	注册新用户，还没有账号，更没有 session
// getBackendData (144)	        没有	返回总在线人数/注册人数，公共数据
// handleGameBackend (756)	    没有	返回静态后台页面
// handleLogin (281)	        有	    登录成功后要把 userId/username 写入 session
// handleLogout (441)	        有	    需要清空并销毁当前用户的 session
// handleMenu (491)	            有	    需要校验 isLoggedIn，拒绝未登录用户
// handleAiGameStart (550)	    有	    需要 userId 来创建该用户的棋局
// handleAiGameMove (596)	    有	    需要 userId 找到该用户的棋局、执行落子
// restartChessGameVsAi (111)	有	    同上

// 注册回调函数
// httpServer是HttpServer类型，.Get()是HttpServer的函数
// 把 lambda 以 {GET, "/"} 为 key 存入 callbacks_。
void GomokuServer::initializeRouter()
{
    // 登录注册入口页面
    httpServer_.Get("/", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleEntry(req, resp);
    });
    httpServer_.Get("/entry", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleEntry(req, resp);
    });
    // 登录
    httpServer_.Post("/login", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleLogin(req, resp);
    });
    // 注册
    httpServer_.Post("/register", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleRegister(req, resp);
    });
    // 登出
    httpServer_.Post("/user/logout", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleLogout(req, resp);
    });
    // 菜单页面
    httpServer_.Get("/menu", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleMenu(req, resp);
    });
    // 开始对战ai
    httpServer_.Get("/aiBot/start", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleAiGameStart(req, resp);
    });
    // 下棋
    httpServer_.Post("/aiBot/move", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleAiGameMove(req, resp);
    });
    // 重新开始对战ai
    httpServer_.Get("/aiBot/restart",
    [this](const http::HttpRequest& req, http::HttpResponse* resp) {
            restartChessGameVsAi(req, resp);
    });

    // 后台界面
    httpServer_.Get("/backend", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        handleGameBackend(req, resp);
    });
    // 后台数据获取
    httpServer_.Get("/backend_data", [this](const http::HttpRequest& req, http::HttpResponse* resp) {
        getBackendData(req, resp);
    });
    // 网站图标
    httpServer_.Get("/favicon.ico", [](const http::HttpRequest& req, http::HttpResponse* resp) {
        std::string reqFile("../Apps/GomokuServer/resource/favicon.ico");
        FileUtil fileOperater(reqFile);
        if (!fileOperater.isValid())
        {
            resp->setStatusCode(http::HttpResponse::k404NotFound);
            resp->setCloseConnection(true);
            return;
        }
        std::vector<char> buffer(fileOperater.size());
        fileOperater.readFile(buffer);
        std::string bufStr(buffer.data(), buffer.size());
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("image/x-icon");
        resp->setContentLength(bufStr.size());
        resp->setBody(bufStr);
    });
}

void GomokuServer::restartChessGameVsAi(const http::HttpRequest &req, http::HttpResponse *resp)
{
    // 解析请求体
    auto session = getSessionManager()->getSession(req, resp);
    if (session->getValue("isLoggedIn") != "true")
    {
        // 用户未登录，返回未授权错误
        LOG_UTIL_WARN("Game restart: unauthorized access attempt");
        json errorResp;
        errorResp["status"] = "error";
        errorResp["message"] = "Unauthorized";
        std::string errorBody = errorResp.dump(4);

        packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                    "Unauthorized", true, "application/json", errorBody.size(),
                    errorBody, resp);
        return;
    }

    int userId = std::stoi(session->getValue("userId"));
    {
        // 重新开始ai对战
        std::lock_guard<std::mutex> lock(mutexForAiGames_);
        if (aiGames_.find(userId) != aiGames_.end())
            aiGames_.erase(userId);
        aiGames_[userId] = std::make_shared<AiGame>(userId);
    }
    LOG_UTIL_INFO("Game restarted: userId=" << userId);

    json successResp;
    successResp["status"] = "ok";
    successResp["message"] = "restart successful";
    successResp["userId"] = userId;
    std::string successBody = successResp.dump(4);
    packageResp(req.getVersion(), http::HttpResponse::k200Ok, "OK", false, "application/json", successBody.size(), successBody, resp);
}

// 获取后台数据
void GomokuServer::getBackendData(const http::HttpRequest &req, http::HttpResponse *resp)
{
    try
    {
        // 获取数据
        int curOnline = getCurOnline();
        LOG_INFO << "当前在线人数: " << curOnline;

        int maxOnline = getMaxOnline();
        LOG_INFO << "历史最高在线人数: " << maxOnline;

        int totalUser = getUserCount();
        LOG_INFO << "已注册用户总数: " << totalUser;

        // 构造 JSON 响应
        nlohmann::json respBody;
        respBody = {
            {"curOnline", curOnline},
            {"maxOnline", maxOnline},
            {"totalUser", totalUser}
        };

        // 转换为字符串
        std::string responseStr = respBody.dump(4);

        // 设置响应
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setContentType("application/json");
        resp->setBody(responseStr);
        resp->setContentLength(responseStr.size());
        resp->setCloseConnection(false);

        LOG_INFO << "Backend data response prepared successfully";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Error in getBackendData: " << e.what();
        LOG_UTIL_ERROR("Backend data query failed: " << e.what());

        // 错误响应
        nlohmann::json errorBody = {
            {"error", "Internal Server Error"},
            {"message", e.what()}
        };

        std::string errorStr = errorBody.dump();
        resp->setStatusCode(http::HttpResponse::k500InternalServerError);
        resp->setStatusMessage("Internal Server Error");
        resp->setContentType("application/json");
        resp->setBody(errorStr);
        resp->setContentLength(errorStr.size());
        resp->setCloseConnection(true);
    }
}

void GomokuServer::packageResp(const std::string &version,
                             http::HttpResponse::HttpStatusCode statusCode,
                             const std::string &statusMsg,
                             bool close,
                             const std::string &contentType,
                             int contentLen,
                             const std::string &body,
                             http::HttpResponse *resp)
{
    if (resp == nullptr)
    {
        LOG_ERROR << "Response pointer is null";
        return;
    }

    try
    {
        resp->setVersion(version);
        resp->setStatusCode(statusCode);
        resp->setStatusMessage(statusMsg);
        resp->setCloseConnection(close);
        resp->setContentType(contentType);
        resp->setContentLength(contentLen);
        resp->setBody(body);

        LOG_INFO << "Response packaged successfully";
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Error in packageResp: " << e.what();
        // 设置一个基本的错误响应
        resp->setStatusCode(http::HttpResponse::k500InternalServerError);
        resp->setStatusMessage("Internal Server Error");
        resp->setCloseConnection(true);
    }
}

// ==================== 原 Handler 逻辑 ====================

void GomokuServer::handleEntry(const http::HttpRequest& req, http::HttpResponse* resp)
{
    std::string reqFile;
    reqFile.append("../Apps/GomokuServer/resource/entry.html");
    FileUtil fileOperater(reqFile);
    if (!fileOperater.isValid())
    {
        LOG_WARN << reqFile << " not exist";
        LOG_UTIL_WARN("File not found: " << reqFile << ", using 404 page");
        fileOperater.resetDefaultFile(); // 404 NOT FOUND
    }

    std::vector<char> buffer(fileOperater.size());
    fileOperater.readFile(buffer); // 读出文件数据
    std::string bufStr = std::string(buffer.data(), buffer.size());

    resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
    resp->setCloseConnection(false);
    resp->setContentType("text/html");
    resp->setContentLength(bufStr.size());
    resp->setBody(bufStr);
}

void GomokuServer::handleLogin(const http::HttpRequest& req, http::HttpResponse* resp)
{
    auto contentType = req.getHeader("Content-Type");
    if (contentType.empty() || contentType != "application/json" || req.getBody().empty())
    {
        LOG_INFO << "content" << req.getBody();
        LOG_UTIL_WARN("Login request rejected: invalid Content-Type or empty body");
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(0);
        resp->setBody("");
        return;
    }

    try
    {
        json parsed = json::parse(req.getBody());
        std::string username = parsed["username"];
        std::string password = parsed["password"];
        int userId = queryUserId(username, password);
        if (userId != -1)
        {
            auto session = getSessionManager()->getSession(req, resp);
            session->setValue("userId", std::to_string(userId));
            session->setValue("username", username);
            session->setValue("isLoggedIn", "true");
            if (onlineUsers_.find(userId) == onlineUsers_.end() || onlineUsers_[userId] == false)
            {
                {
                    std::lock_guard<std::mutex> lock(mutexForOnlineUsers_);
                    onlineUsers_[userId] = true;
                }

                updateMaxOnline(onlineUsers_.size());
                LOG_INFO << "User " << userId << " (" << username
                         << ") logged in, online: " << onlineUsers_.size();
                LOG_UTIL_INFO("User login: userId=" << userId
                              << " username=" << username
                              << " online=" << onlineUsers_.size());

                json successResp;
                successResp["success"] = true;
                successResp["userId"] = userId;
                std::string successBody = successResp.dump(4);

                resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
                resp->setCloseConnection(false);
                resp->setContentType("application/json");
                resp->setContentLength(successBody.size());
                resp->setBody(successBody);
                return;
            }
            else
            {
                LOG_WARN << "User " << userId << " (" << username
                         << ") already online, rejecting login";
                LOG_UTIL_WARN("User login rejected (already online): userId=" << userId
                              << " username=" << username);
                json failureResp;
                failureResp["success"] = false;
                failureResp["error"] = "账号已在其他地方登录";
                std::string failureBody = failureResp.dump(4);

                resp->setStatusLine(req.getVersion(), http::HttpResponse::k403Forbidden, "Forbidden");
                resp->setCloseConnection(true);
                resp->setContentType("application/json");
                resp->setContentLength(failureBody.size());
                resp->setBody(failureBody);
                return;
            }
        }
        else
        {
            LOG_UTIL_WARN("Login failed: invalid credentials for username=" << username);
            json failureResp;
            failureResp["status"] = "error";
            failureResp["message"] = "Invalid username or password";
            std::string failureBody = failureResp.dump(4);

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k401Unauthorized, "Unauthorized");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(failureBody.size());
            resp->setBody(failureBody);
            return;
        }
    }
    catch (const std::exception &e)
    {
        LOG_UTIL_ERROR("Login handler exception: " << e.what());
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
        return;
    }
}

int GomokuServer::queryUserId(const std::string &username, const std::string &password)
{
    std::string sql = "SELECT id FROM users WHERE username = ? AND password = ?";
    sql::ResultSet* res = mysqlUtil_.executeQuery(sql, username, password);
    if (res->next())
    {
        int id = res->getInt("id");
        return id;
    }
    return -1;
}

void GomokuServer::handleRegister(const http::HttpRequest& req, http::HttpResponse* resp)
{
    json parsed = json::parse(req.getBody());
    std::string username = parsed["username"];
    std::string password = parsed["password"];

    int userId = insertUser(username, password);
    if (userId != -1)
    {
        json successResp;
        successResp["status"] = "success";
        successResp["message"] = "Register successful";
        successResp["userId"] = userId;
        std::string successBody = successResp.dump(4);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(successBody.size());
        resp->setBody(successBody);
    }
    else
    {
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = "username already exists";
        std::string failureBody = failureResp.dump(4);

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k409Conflict, "Conflict");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}

int GomokuServer::insertUser(const std::string &username, const std::string &password)
{
    if (!isUserExist(username))
    {
        std::string sql = "INSERT INTO users (username, password) VALUES (?, ?)";
        mysqlUtil_.executeUpdate(sql, username, password);
        std::string sql2 = "SELECT id FROM users WHERE username = ?";
        sql::ResultSet* res = mysqlUtil_.executeQuery(sql2, username);
        if (res->next())
        {
            return res->getInt("id");
        }
    }
    return -1;
}

bool GomokuServer::isUserExist(const std::string &username)
{
    std::string sql = "SELECT id FROM users WHERE username = ?";
    sql::ResultSet* res = mysqlUtil_.executeQuery(sql, username);
    if (res->next())
    {
        return true;
    }
    return false;
}

void GomokuServer::handleLogout(const http::HttpRequest& req, http::HttpResponse* resp)
{
    auto contentType = req.getHeader("Content-Type");
    if (contentType.empty() || contentType.find("application/json") == std::string::npos || req.getBody().empty())
    {
        LOG_WARN << "Logout request rejected: invalid Content-Type or empty body";
        LOG_UTIL_WARN("Logout request rejected: invalid Content-Type or empty body");
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(0);
        resp->setBody("");
        return;
    }

    try
    {
        json parsed = json::parse(req.getBody());
        int userId = parsed["userId"];
        int gameType = parsed["gameType"];
        std::string type = parsed.value("type", "unknown");

        LOG_INFO << "Logout request: userId=" << userId << " gameType=" << gameType
                 << " type=" << type;

        // 清理 session（best-effort）
        try
        {
            auto session = getSessionManager()->getSession(req, resp);
            if (session && !session->getValue("userId").empty())
            {
                session->clear();
                getSessionManager()->destroySession(session->getId());
            }
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Session cleanup failed: " << e.what();
            LOG_UTIL_WARN("Logout session cleanup failed: " << e.what());
        }

        // 使用请求体中的 userId 清理在线状态
        {
            std::lock_guard<std::mutex> lock(mutexForOnlineUsers_);
            onlineUsers_.erase(userId);
        }
        LOG_INFO << "User " << userId << " logged out, online: " << onlineUsers_.size();
        LOG_UTIL_INFO("User logout: userId=" << userId
                      << " online=" << onlineUsers_.size());

        if (gameType == GomokuServer::MAN_VS_AI)
        {
            std::lock_guard<std::mutex> lock(mutexForAiGames_);
            aiGames_.erase(userId);
            LOG_INFO << "AI game cleaned up for user " << userId;
        }

        json response;
        response["message"] = "logout successful";
        std::string responseBody = response.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(responseBody.size());
        resp->setBody(responseBody);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Logout failed: " << e.what();
        LOG_UTIL_ERROR("Logout handler exception: " << e.what());
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}

void GomokuServer::handleMenu(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = getSessionManager()->getSession(req, resp);
        LOG_INFO << "session->getValue(\"isLoggedIn\") = " << session->getValue("isLoggedIn");
        if (session->getValue("isLoggedIn") != "true")
        {
            LOG_UTIL_WARN("Menu access: unauthorized attempt");
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "Unauthorized";
            std::string errorBody = errorResp.dump(4);

            packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                        "Unauthorized", true, "application/json", errorBody.size(),
                         errorBody, resp);
            return;
        }

        int userId = std::stoi(session->getValue("userId"));
        std::string username = session->getValue("username");

        std::string reqFile("../Apps/GomokuServer/resource/menu.html");
        FileUtil fileOperater(reqFile);
        if (!fileOperater.isValid())
        {
            LOG_WARN << reqFile << "not exist.";
            fileOperater.resetDefaultFile();
        }

        std::vector<char> buffer(fileOperater.size());
        fileOperater.readFile(buffer);
        std::string htmlContent(buffer.data(), buffer.size());

        size_t headEnd = htmlContent.find("</head>");
        if (headEnd != std::string::npos)
        {
            std::string script = "<script>const userId = '" + std::to_string(userId) + "';</script>";
            htmlContent.insert(headEnd, script);
        }

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("text/html");
        resp->setContentLength(htmlContent.size());
        resp->setBody(htmlContent);
    }
    catch (const std::exception &e)
    {
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}

void GomokuServer::handleAiGameStart(const http::HttpRequest& req, http::HttpResponse* resp)
{
    auto session = getSessionManager()->getSession(req, resp);
    if (session->getValue("isLoggedIn") != "true")
    {
        LOG_UTIL_WARN("Game start: unauthorized attempt");
        json errorResp;
        errorResp["status"] = "error";
        errorResp["message"] = "Unauthorized";
        std::string errorBody = errorResp.dump(4);

        packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                     "Unauthorized", true, "application/json", errorBody.size(),
                     errorBody, resp);
        return;
    }

    int userId = std::stoi(session->getValue("userId"));
    LOG_UTIL_INFO("AI game started: userId=" << userId);

    {
        std::lock_guard<std::mutex> lock(mutexForAiGames_);
        if (aiGames_.find(userId) != aiGames_.end())
            aiGames_.erase(userId);
        aiGames_[userId] = std::make_shared<AiGame>(userId);
    }

    std::string reqFile("../Apps/GomokuServer/resource/ChessGameVsAi.html");
    FileUtil fileOperater(reqFile);
    if (!fileOperater.isValid())
    {
        LOG_WARN << reqFile << "not exist.";
        fileOperater.resetDefaultFile();
    }

    std::vector<char> buffer(fileOperater.size());
    fileOperater.readFile(buffer);
    std::string htmlContent(buffer.data(), buffer.size());

    resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
    resp->setCloseConnection(false);
    resp->setContentType("text/html");
    resp->setContentLength(htmlContent.size());
    resp->setBody(htmlContent);
}

void GomokuServer::handleAiGameMove(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = getSessionManager()->getSession(req, resp);
        if (session->getValue("isLoggedIn") != "true")
        {
            LOG_UTIL_WARN("Game move: unauthorized attempt");
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "Unauthorized";
            std::string errorBody = errorResp.dump(4);

            packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                         "Unauthorized", true, "application/json", errorBody.size(),
                         errorBody, resp);
            return;
        }

        int userId = std::stoi(session->getValue("userId"));
        json request = json::parse(req.getBody());
        int x = request["x"];
        int y = request["y"];
        LOG_UTIL_INFO("Game move: userId=" << userId << " pos=(" << x << "," << y << ")");

        if (aiGames_.find(userId) == aiGames_.end())
        {
            std::lock_guard<std::mutex> lock(mutexForAiGames_);
            aiGames_[userId] = std::make_shared<AiGame>(userId);
        }
        auto &game = aiGames_[userId];

        if (!game->humanMove(x, y))
        {
            LOG_UTIL_WARN("Game move invalid: userId=" << userId << " pos=(" << x << "," << y << ")");
            json response = {
                {"status", "error"},
                {"message", "Invalid move"}};
            std::string responseBody = response.dump();

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(responseBody.size());
            resp->setBody(responseBody);
            return;
        }

        if (game->isGameOver())
        {
            json response = {
                {"status", "ok"},
                {"board", game->getBoard()},
                {"winner", "human"},
                {"next_turn", "none"}};
            std::string responseBody = response.dump();

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(responseBody.size());
            resp->setBody(responseBody);

            {
                std::lock_guard<std::mutex> lock(mutexForAiGames_);
                aiGames_.erase(userId);
            }
            return;
        }

        if (game->isDraw())
        {
            json response = {
                {"status", "ok"},
                {"board", game->getBoard()},
                {"winner", "draw"},
                {"next_turn", "none"}};
            std::string responseBody = response.dump();

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(responseBody.size());
            resp->setBody(responseBody);

            {
                std::lock_guard<std::mutex> lock(mutexForAiGames_);
                aiGames_.erase(userId);
            }
            return;
        }

        // AI移动
        game->aiMove();

        if (game->isGameOver())
        {
            json response = {
                {"status", "ok"},
                {"board", game->getBoard()},
                {"winner", "ai"},
                {"next_turn", "none"},
                {"last_move", {{"x", game->getLastMove().first}, {"y", game->getLastMove().second}}}};
            std::string responseBody = response.dump();

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(responseBody.size());
            resp->setBody(responseBody);

            {
                std::lock_guard<std::mutex> lock(mutexForAiGames_);
                aiGames_.erase(userId);
            }
            return;
        }

        if (game->isDraw())
        {
            json response = {
                {"status", "ok"},
                {"board", game->getBoard()},
                {"winner", "draw"},
                {"next_turn", "none"},
                {"last_move", {{"x", game->getLastMove().first}, {"y", game->getLastMove().second}}}};
            std::string responseBody = response.dump();

            resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(responseBody.size());
            resp->setBody(responseBody);

            {
                std::lock_guard<std::mutex> lock(mutexForAiGames_);
                aiGames_.erase(userId);
            }
            return;
        }

        // 游戏继续
        json response = {
            {"status", "ok"},
            {"board", game->getBoard()},
            {"winner", "none"},
            {"next_turn", "human"},
            {"last_move", {{"x", game->getLastMove().first}, {"y", game->getLastMove().second}}}};

        std::string responseBody = response.dump();

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(responseBody.size());
        resp->setBody(responseBody);
    }
    catch (const std::exception &e)
    {
        LOG_UTIL_ERROR("Game move handler exception: " << e.what());
        json response = {
            {"status", "error"},
            {"message", e.what()}};
        std::string responseBody = response.dump();
        packageResp(req.getVersion(), http::HttpResponse::k500InternalServerError, "Internal Server Error", false, "application/json", responseBody.size(), responseBody, resp);
    }
}

void GomokuServer::handleGameBackend(const http::HttpRequest& req, http::HttpResponse* resp)
{
    std::string reqFile("../Apps/GomokuServer/resource/Backend.html");
    FileUtil fileOperater(reqFile);
    if (!fileOperater.isValid())
    {
        LOG_WARN << reqFile << "not exist.";
        fileOperater.resetDefaultFile();
    }

    std::vector<char> buffer(fileOperater.size());
    fileOperater.readFile(buffer);
    std::string htmlContent(buffer.data(), buffer.size());

    resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
    resp->setCloseConnection(false);
    resp->setContentType("text/html");
    resp->setContentLength(htmlContent.size());
    resp->setBody(htmlContent);
}
