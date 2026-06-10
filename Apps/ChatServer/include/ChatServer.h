#pragma once

#include <memory>
#include <unordered_map>
#include <mutex>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "../../../HttpServer/include/http/HttpServer.h"
#include "../../../HttpServer/include/utils/MysqlUtil.h"
#include "AIUtil/AIHelper.h"
#include "AIUtil/ImageRecognizer.h"
#include "AIUtil/MQManager.h"

class ChatServer {
public:
	ChatServer(int port,
		const std::string& name,
		muduo::net::TcpServer::Option option = muduo::net::TcpServer::kNoReusePort);

	void setThreadNum(int numThreads);
	void start();
	void initChatMessage();

private:
	void initialize();
	void initializeSession();
	void initializeRouter();
	void initializeMiddleware();

	void readDataFromMySQL();

	void packageResp(const std::string& version, http::HttpResponse::HttpStatusCode statusCode,
		const std::string& statusMsg, bool close, const std::string& contentType,
		int contentLen, const std::string& body, http::HttpResponse* resp);

	void setSessionManager(std::unique_ptr<http::session::SessionManager> manager)
	{
		httpServer_.setSessionManager(std::move(manager));
	}
	http::session::SessionManager* getSessionManager() const
	{
		return httpServer_.getSessionManager();
	}

	// ---- handler 函数 ----
	void handleEntry(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleLogin(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleRegister(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleLogout(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleChat(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleChatSend(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleMenu(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleUpload(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleUploadSend(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleChatHistory(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleChatCreateAndSend(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleChatSessions(const http::HttpRequest& req, http::HttpResponse* resp);
	void handleChatTTS(const http::HttpRequest& req, http::HttpResponse* resp);
	static void handleFavicon(const http::HttpRequest& req, http::HttpResponse* resp);

	// ---- handler 辅助函数 ----
	int queryUserId(const std::string& username, const std::string& password);
	int insertUser(const std::string& username, const std::string& password);
	bool isUserExist(const std::string& username);

	http::HttpServer    httpServer_;
	http::MysqlUtil     mysqlUtil_;

	std::unordered_map<int, bool> onlineUsers_;
	std::mutex mutexForOnlineUsers_;

	std::unordered_map<int, std::unordered_map<std::string, std::shared_ptr<AIHelper>>> chatInformation;
	std::mutex mutexForChatInformation;

	std::unordered_map<int, std::shared_ptr<ImageRecognizer>> ImageRecognizerMap;
	std::mutex mutexForImageRecognizerMap;

	std::unordered_map<int, std::vector<std::string>> sessionsIdsMap;
	std::mutex mutexForSessionsId;
};
