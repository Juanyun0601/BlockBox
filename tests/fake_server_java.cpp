/**
 * @file   fake_server_java.cpp
 * @brief  本地开服探针用的假 Java：既装成 Minecraft 服务端，也装成 Forge 安装器
 *
 * 探针里不能真跑 Minecraft 服务端 / Forge 安装器（要下几百 MB 还会跑很久），
 * 所以用这个可执行文件顶替 java.exe：
 *   - 参数里有 --installServer 时当安装器：建一份 libraries/.../win_args.txt，
 *     打印几行安装日志后正常退出（版本号从环境变量 FAKE_FORGE_MC / FAKE_FORGE_VERSION 取）；
 *   - 否则当服务端：先打印启动日志，再打印 "Done (...)! For help, type "help""，
 *     读到 stdin 上的 stop 就正常退出（模拟服务端存盘退出）。
 */
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#endif

namespace {

std::string envOr(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::string(fallback);
}

void makeDirs(const std::string &relativePath)
{
    std::string current;
    for (size_t i = 0; i < relativePath.size(); ++i) {
        const char ch = relativePath[i];
        current.push_back(ch);
        if (ch != '/' && ch != '\\')
            continue;
        if (current.size() <= 1)
            continue;
#ifdef _WIN32
        _mkdir(current.c_str());
#else
        mkdir(current.c_str(), 0755);
#endif
    }
}

} // namespace

int main(int argc, char **argv)
{
    std::string joined;
    for (int i = 1; i < argc; ++i) {
        joined += ' ';
        joined += argv[i];
    }

    // ── 当安装器：建出 Forge / NeoForge 安装后的启动配置 ──
    if (joined.find("--installServer") != std::string::npos) {
        const std::string mc = envOr("FAKE_FORGE_MC", "1.20.1");
        const std::string version = envOr("FAKE_FORGE_VERSION", "47.2.0");

        std::cout << "[installer] Extracting json data" << std::endl;
        std::cout << "[installer] Downloading libraries" << std::endl;

        const std::string forgeDir =
            "libraries/net/minecraftforge/forge/" + mc + "-" + version + "/";
        const std::string neoDir = "libraries/net/neoforged/neoforge/" + version + "/";
        const std::string dir = joined.find("neoforge") != std::string::npos ? neoDir : forgeDir;

        makeDirs(dir + "win_args.txt");
        {
            std::ofstream args(dir + "win_args.txt");
            args << "-p libraries/cpw/mods/bootstraplauncher/1.1.2/bootstraplauncher-1.1.2.jar\n"
                 << "net.minecraftforge.bootstrap.ForgeBootstrap\n";
        }
        {
            std::ofstream args(dir + "unix_args.txt");
            args << "-p libraries/cpw/mods/bootstraplauncher/1.1.2/bootstraplauncher-1.1.2.jar\n"
                 << "net.minecraftforge.bootstrap.ForgeBootstrap\n";
        }
        // 顺手放一枚服务端 jar：1.13~1.16 的安装器会留下这个
        {
            std::ofstream jar("forge-" + mc + "-" + version + "-server.jar", std::ios::binary);
            jar << "PK\3\4";
        }
        std::cout << "[installer] Server installed successfully" << std::endl;
        return 0;
    }

    // ── 当服务端 ──
    std::cout << "[12:00:00] [main/INFO]: Starting minecraft server version 1.20.4" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[12:00:00] [Server thread/INFO]: Preparing level \"world\"" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << "[12:00:02] [Server thread/INFO]: Done (1.234s)! For help, type \"help\""
              << std::endl;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "stop" || line == "stop ") {
            std::cout << "[12:00:05] [Server thread/INFO]: Stopping the server" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << "[12:00:06] [Server thread/INFO]: ThreadedAnvilChunkStorage: All dimensions are saved"
                      << std::endl;
            return 0;
        }
        if (!line.empty())
            std::cout << "[12:00:03] [Server thread/INFO]: <Player> " << line << std::endl;
    }
    return 0;
}
