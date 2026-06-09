#include "../../../include/utils/db/DbConnectionPool.h"
#include "../../../include/utils/db/DbException.h"
#include "../../../include/utils/LogUtil.h"
#include <muduo/base/Logging.h>

namespace http 
{
namespace db 
{

void DbConnectionPool::init(const std::string& host,
                          const std::string& user,
                          const std::string& password,
                          const std::string& database,
                          size_t poolSize) 
{
    // 连接池会被多个线程访问，所以操作其成员变量时需要加锁
    std::lock_guard<std::mutex> lock(mutex_);
    // 确保只初始化一次
    if (initialized_) 
    {
        return;
    }

    host_ = host;
    user_ = user;
    password_ = password;
    database_ = database;

    // 创建连接
    for (size_t i = 0; i < poolSize; ++i) 
    {
        connections_.push(createConnection());
    }

    initialized_ = true;
    LOG_INFO << "Database connection pool initialized with " << poolSize << " connections";
    LOG_UTIL_INFO("DB connection pool initialized: " << poolSize << " connections");
}

DbConnectionPool::DbConnectionPool() 
{
    checkThread_ = std::thread(&DbConnectionPool::checkConnections, this);
    checkThread_.detach();
}

DbConnectionPool::~DbConnectionPool() 
{
    std::lock_guard<std::mutex> lock(mutex_);
    while (!connections_.empty())
    {
        connections_.pop();
    }
    LOG_INFO << "Database connection pool destroyed";
    LOG_UTIL_INFO("DB connection pool destroyed");
}

// 获取连接
std::shared_ptr<DbConnection> DbConnectionPool::getConnection() 
{
    std::shared_ptr<DbConnection> conn;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        while (connections_.empty()) 
        {
            if (!initialized_) 
            {
                throw DbException("Connection pool not initialized");
            }
            LOG_INFO << "Waiting for available connection...";
            cv_.wait(lock);
        }
        
        // queue FIFO
        // A conn ref = 2
        conn = connections_.front();
        connections_.pop();
    } // 释放锁
      
    try
    {
        // 在锁外检查连接
        // 连接意外断开（超时或异常）时，尝试重新连接
        if (!conn->ping())
        {
            LOG_WARN << "Connection lost, attempting to reconnect...";
            conn->reconnect();
        }
        
        // shared_ptr 构造函数的第二参数是一个可调用对象，引用计数归零时调用它
        // return后conn离开作用域被销毁，A ref-1 = 1，B ref = 1
        // B 使用完成ref = 0调用LAMBDA函数，将连接返回连接池并唤醒等待线程
        return std::shared_ptr<DbConnection>(conn.get(),
            [this, conn](DbConnection*) {
                std::lock_guard<std::mutex> lock(mutex_);
                connections_.push(conn);
                cv_.notify_one();
            });
    }
    catch (const std::exception& e)
    {
        LOG_ERROR << "Failed to get connection: " << e.what();
        LOG_UTIL_ERROR("DB pool: failed to get connection - " << e.what());
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.push(conn);
            cv_.notify_one();
        }
        throw;
    }
}

std::shared_ptr<DbConnection> DbConnectionPool::createConnection() 
{
    return std::make_shared<DbConnection>(host_, user_, password_, database_);
}

// 检查连接
// 两层防护：借出时检查+心跳检测
void DbConnectionPool::checkConnections() 
{
    while (true) 
    {
        try 
        {
            std::vector<std::shared_ptr<DbConnection>> connsToCheck;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (connections_.empty()) 
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                
                auto temp = connections_;
                while (!temp.empty()) 
                {
                    connsToCheck.push_back(temp.front());
                    temp.pop();
                }
            }
            
            // 在锁外检查连接，避免长时间阻塞主线程
            for (auto& conn : connsToCheck) 
            {
                if (!conn->ping()) 
                {
                    try 
                    {
                        conn->reconnect();
                    } 
                    catch (const std::exception& e)
                    {
                        LOG_ERROR << "Failed to reconnect: " << e.what();
                        LOG_UTIL_ERROR("DB pool check: reconnect failed - " << e.what());
                    }
                }
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(60));
        } 
        catch (const std::exception& e)
        {
            LOG_ERROR << "Error in check thread: " << e.what();
            LOG_UTIL_ERROR("DB pool check thread error: " << e.what());
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

} // namespace db
} // namespace http