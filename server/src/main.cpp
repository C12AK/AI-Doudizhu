#include "config_parse.h"
#include "log_record.h"
#include "mysql_handle.h"
#include "net/http_ws_server.h"
#include "table.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const char* ini = argc > 1 ? argv[1] : "deploy/config/sysparam.ini";
    Config cfg;
    if (!cfg.load(ini)) {
        std::cerr << "failed to load config: " << ini << "\n";
        return 1;
    }

    init_log(cfg.log_file, cfg.log_file_size_mb, cfg.log_max_files, cfg.log_level);
    log_info("ddz-server starting");
    log_info("www_root=" + cfg.www_root);
    log_info("mysql " + cfg.mysql_user + "@" + cfg.mysql_host + ":" + std::to_string(cfg.mysql_port) +
             "/" + cfg.mysql_database);

    MysqlHandle db;
    if (!db.connect(cfg.mysql_host, cfg.mysql_port, cfg.mysql_user, cfg.mysql_password, cfg.mysql_database)) {
        log_error("mysql connect failed");
        return 1;
    }
    log_info("mysql connected");

    HttpWsServer net;
    Table table(cfg, db, net);
    net.set_www_root(cfg.www_root);
    net.set_on_log([](const std::string& s) { log_info(s); });
    net.set_on_open([&table](auto id) { table.on_open(id); });
    net.set_on_close([&table](auto id) { table.on_close(id); });
    net.set_on_message([&table](auto id, const std::string& m) { table.on_message(id, m); });

    if (!net.listen(cfg.listen_addr, cfg.http_port)) {
        log_error("listen failed");
        return 1;
    }
    table.start();
    log_info("running");
    net.run();
    return 0;
}
