/**
 * SQL 注入防护测试
 *
 * 编译方式（在项目根目录）：
 *   g++ -std=c++17 -I HttpServer/include -I /usr/include/mysql-cppconn-8 -I /usr/include/mysql \
 *       tests/test_sql_injection.cpp HttpServer/src/utils/db/*.cpp \
 *       -lmysqlcppconn -lmysqlclient -lmuduo_net -lmuduo_base -lpthread \
 *       -o build/test_sql_injection
 *
 * 运行前确保：
 *   1. MySQL 运行中
 *   2. 已执行下面的建表 SQL：
 *        CREATE DATABASE IF NOT EXISTS sqli_test;
 *        USE sqli_test;
 *        CREATE TABLE users (
 *            id INT AUTO_INCREMENT PRIMARY KEY,
 *            username VARCHAR(100) NOT NULL,
 *            password VARCHAR(100) NOT NULL
 *        );
 *        INSERT INTO users VALUES (1, 'admin', 'secret123');
 *   3. 根据本地 MySQL 配置修改下面的连接参数
 */

#include "utils/db/DbConnection.h"
#include "utils/db/DbException.h"
#include <iostream>
#include <cassert>
#include <string>

using namespace http::db;

// ---------------------------------------------------------------------------
// 固定测试库的连接参数（与生产库隔离）
// ---------------------------------------------------------------------------
static const char* TEST_HOST     = "tcp://127.0.0.1:3306";
static const char* TEST_USER     = "root";
static const char* TEST_PASSWORD = "123456";
static const char* TEST_DATABASE = "sqli_test";

// ---------------------------------------------------------------------------
// 工具函数：创建独立连接（不走连接池，每个测试用例一条连接）
// ---------------------------------------------------------------------------
DbConnection newConnection() {
    return DbConnection(TEST_HOST, TEST_USER, TEST_PASSWORD, TEST_DATABASE);
}

// 查询 users 表的行数（用于验证副作用）
int countUsers(DbConnection& conn) {
    auto* rs = conn.executeQuery("SELECT COUNT(*) FROM users");
    rs->next();
    int n = rs->getInt(1);
    delete rs;
    return n;
}

void printResult(const char* label, bool blocked, const std::string& detail = "") {
    std::cout << "  [" << (blocked ? "BLOCKED" : "VULNERABLE") << "] "
              << label;
    if (!detail.empty()) std::cout << " -> " << detail;
    std::cout << std::endl;
}

// ===================================================================
// 第一组：参数化查询（executeQuery / executeUpdate） — 预期全部拦截
// ===================================================================
namespace ParameterizedTests {

// --- 经典登录绕过攻击 ---
void test1_tautology_bypass() {
    auto conn = newConnection();
    // 攻击输入：万能密码
    std::string username = "admin";
    std::string password = "' OR '1'='1";

    auto* rs = conn.executeQuery(
        "SELECT id FROM users WHERE username = ? AND password = ?",
        username, password);   // ← 两个 ? 分别绑定 username 和 password

    // PreparedStatement 将 password 整体当作字符串值
    // 实际执行: WHERE username='admin' AND password='\' OR \'1\'=\'1'
    // '1'='1' 不会被执行，因为它只是 password 字段值的字面内容
    bool hasRow = rs->next();
    delete rs;

    printResult("万能密码绕过登录 (' OR '1'='1)", !hasRow,
                hasRow ? "注入成功！" : "注入被阻断（预期）");
}

// --- 注释截断攻击 ---
void test2_comment_truncation() {
    auto conn = newConnection();
    std::string username = "admin' -- ";
    std::string password = "anything";

    auto* rs = conn.executeQuery(
        "SELECT id FROM users WHERE username = ? AND password = ?",
        username, password);
    // 实际执行: WHERE username='admin\' -- ' AND password='anything'
    // '-- ' 是字段值的一部分，不是 SQL 注释

    bool hasRow = rs->next();
    delete rs;

    printResult("注释截断绕过 (admin' -- )", !hasRow,
                hasRow ? "注入成功！" : "注入被阻断（预期）");
}

// --- 堆叠查询攻击 ---
void test3_stacked_query() {
    auto conn = newConnection();
    int before = countUsers(conn);

    std::string username = "hacker";
    std::string password = "x'); DROP TABLE users; -- ";

    try {
        conn.executeUpdate(
            "INSERT INTO users (username, password) VALUES (?, ?)",
            username, password);
        // password 只是字符串，不会执行 DROP TABLE
        int after = countUsers(conn);
        printResult("堆叠查询删表 (DROP TABLE in value)", before == after,
                    "行数变化: " + std::to_string(before) + " -> " + std::to_string(after));
    } catch (const DbException& e) {
        printResult("堆叠查询删表 (DROP TABLE in value)", true,
                    std::string("SQL 执行失败（预期）: ") + e.what());
    }
}

// --- UNION 注入 ---
void test4_union_injection() {
    auto conn = newConnection();
    // 尝试用 UNION 拖出额外数据
    std::string username = "' UNION SELECT 1,2,3 -- ";
    std::string password = "x";

    auto* rs = conn.executeQuery(
        "SELECT id FROM users WHERE username = ? AND password = ?",
        username, password);
    // username 整体作为字符串匹配，UNION 关键字不生效

    bool hasRow = rs->next();
    delete rs;

    printResult("UNION 注入拖数据", !hasRow,
                hasRow ? "注入成功！" : "注入被阻断（预期）");
}

// --- 布尔盲注 ---
void test5_boolean_blind() {
    auto conn = newConnection();
    std::string username = "admin' AND SUBSTRING(password,1,1)='s";
    std::string password = "x";

    auto* rs = conn.executeQuery(
        "SELECT id FROM users WHERE username = ? AND password = ?",
        username, password);

    bool hasRow = rs->next();
    delete rs;

    printResult("布尔盲注猜密码", !hasRow,
                hasRow ? "注入成功（可逐字符猜解密码）！" : "注入被阻断（预期）");
}

// --- 时间盲注 ---
void test6_time_based() {
    auto conn = newConnection();
    std::string username = "admin";
    std::string password = "' OR IF(1=1, SLEEP(3), 0) -- ";

    auto* rs = conn.executeQuery(
        "SELECT id FROM users WHERE username = ? AND password = ?",
        username, password);

    bool hasRow = rs->next();
    delete rs;

    printResult("时间盲注 SLEEP", !hasRow,
                hasRow ? "注入成功（可通过延时逐字符猜解）！" : "注入被阻断（预期）");
}

void test7_insert_data_exfiltration() {
    auto conn = newConnection();
    // 尝试通过 INSERT 插入恶意子查询
    std::string username = "' || (SELECT GROUP_CONCAT(password) FROM users)) -- ";
    std::string password = "p";

    conn.executeUpdate(
        "INSERT INTO users (username, password) VALUES (?, ?)",
        username, password);

    // 检查实际插入的值是什么 — 应该是恶意字符串本身，而不是子查询的结果
    auto* rs = conn.executeQuery(
        "SELECT username FROM users WHERE password = ?", std::string("p"));
    bool hasRow = rs->next();
    std::string actualUsername;
    if (hasRow) actualUsername = rs->getString(1);
    delete rs;

    bool injected = (actualUsername.find("secret") != std::string::npos);
    printResult("INSERT 子查询数据窃取", !injected,
                injected ? "注入成功，窃取到其他用户密码！"
                         : "注入被阻断，子查询被当作字面值（预期）");
}

} // namespace ParameterizedTests

// ===================================================================
// 第二组：字符串拼接（模拟 ChatServer 漏洞写法）— 预期全部成功注入
// ===================================================================
namespace ConcatenationTests {

// 注意：这里只能调用 executeUpdate(sql) 单参版本（执行拼接好的完整 SQL）
// 你需要先在 DbConnection 中添加一个 rawQuery 方法，或者直接用 mysqlclient 裸调
//
// 为了测试，这里使用 conn_->createStatement()->executeQuery() 绕过参数化。
// 实际验证时，建议写一个"裸执行"辅助方法在 DbConnection 中临时使用。

// 由于 DbConnection 没有暴露裸 Statement 执行接口，这部分需要手动验证
// 方法：直接修改 ChatServer::insertUser() / isUserExist() 让其打印拼接后的 SQL

void demo_vulnerability() {
    std::cout << "\n--- 字符串拼接漏洞演示（仅展示拼接结果，不实际执行） ---\n" << std::endl;

    // 模拟 ChatServer::isUserExist() 的拼接方式
    {
        std::string username = "admin' OR '1'='1";
        std::string sql = "SELECT id FROM users WHERE username = '" + username + "'";
        std::cout << "  isUserExist 朴素拼接 SQL:\n    " << sql << std::endl;
        // → 结果: SELECT id FROM users WHERE username = 'admin' OR '1'='1'
        //   '1'='1' 成为 SQL 逻辑的一部分 → 返回所有行 → 绕过认证
    }
    std::cout << std::endl;

    // 模拟 ChatServer::insertUser() 的拼接方式
    {
        std::string username = "x";
        std::string password = "'); DROP TABLE users; -- ";
        std::string sql = "INSERT INTO users (username, password) VALUES ('"
                        + username + "', '" + password + "')";
        std::cout << "  insertUser 朴素拼接 SQL:\n    " << sql << std::endl;
        // → 结果: INSERT INTO users (username, password) VALUES ('x', ''); DROP TABLE users; -- ')
        //   分号后的 DROP TABLE users 成为独立语句，表被删
    }
    std::cout << std::endl;

    // 模拟 AIHelper::pushMessageToMysql() 的拼接方式
    {
        std::string safeUserName = "alice";
        std::string safeUserInput = "hello'); DELETE FROM users WHERE 1=1; -- ";
        std::string sql = "INSERT INTO chat_message (id, username, session_id, is_user, content, ts) VALUES ("
                        + std::to_string(1) + ", "
                        + "'" + safeUserName + "', "
                        + std::to_string(100) + ", "
                        + std::to_string(1) + ", "
                        + "'" + safeUserInput + "', "
                        + std::to_string(9999999) + ")";
        std::cout << "  AIHelper 拼接 SQL:\n    " << sql << std::endl;
    }
}

} // namespace ConcatenationTests

// ===================================================================
// 主函数
// ===================================================================
int main() {
    std::cout << "============================================================\n";
    std::cout << "  SQL 注入防护测试\n";
    std::cout << "  数据库: " << TEST_DATABASE << "\n";
    std::cout << "============================================================\n\n";

    // -------- 参数化查询测试 --------
    std::cout << "【第一组】参数化查询 (PreparedStatement) — 预期: 全部 BLOCKED\n\n";

    ParameterizedTests::test1_tautology_bypass();
    ParameterizedTests::test2_comment_truncation();
    ParameterizedTests::test3_stacked_query();
    ParameterizedTests::test4_union_injection();
    ParameterizedTests::test5_boolean_blind();
    ParameterizedTests::test6_time_based();
    ParameterizedTests::test7_insert_data_exfiltration();

    // -------- 字符串拼接演示 --------
    std::cout << "\n【第二组】字符串拼接 (ChatServer 实际写法) — 预期: 全部 VULNERABLE\n";
    ConcatenationTests::demo_vulnerability();

    std::cout << "\n============================================================\n";
    std::cout << "  测试完成\n";
    std::cout << "============================================================\n";
    return 0;
}
