#include "../../include/ssl/SslConfig.h"

namespace ssl
{

// 初始化 SSL 配置默认值，默认使用 TLS 1.2
SslConfig::SslConfig()
    : version_(SSLVersion::TLS_1_2)
    , cipherList_("HIGH:!aNULL:!MDS")
    , verifyClient_(false)
    , verifyDepth_(4)
    , sessionTimeout_(300)
    , sessionCacheSize_(20480L)
{
}

};