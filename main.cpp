#include <csignal>
#include <map>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <stdexcept>

#include "core/globals.hpp"
#include "core/command.hpp"
#include "core/server.hpp"
#include "utils/logging.hpp"
#include "utils/address.hpp"
#include "utils/string.h"
#include "backtracpp/sig-handler.h"

namespace {

    class Configuration {
        std::map<std::string, std::string> _config;
    public:
        Configuration(char const* file)
        {
            std::map<std::string, std::string> config;
            std::ifstream conf_file(file);
            if (!conf_file.good()) {
                std::cerr << "Fail to read config file " << file << std::endl;
                exit(1);
            }
            std::string line;
            while (!conf_file.eof()) {
                std::getline(conf_file, line);
                if (line.empty()) {
                    continue;
                }
                std::vector<std::string> kv(util::split_str(line, " ", true));
                if (kv.size() != 2) {
                    std::cerr << "Invalid configure line " << line << std::endl;
                    exit(1);
                }
                config[kv[0]] = kv[1];
            }
            _config = std::move(config);
        }

        bool contains(std::string const& k) const
        {
            return _config.find(k) != _config.end();
        }

        std::string const& get(std::string const& k) const
        {
            try {
                return _config.at(k);
            } catch (std::out_of_range&) {
                throw std::runtime_error("Not configured: " + k);
            }
        }

        std::string get(std::string const& k, std::string def) const
        {
            try {
                return _config.at(k);
            } catch (std::out_of_range&) {
                return std::move(def);
            }
        }
    };

    void exit_on_int(int)
    {
        LOG(INFO) << "C-c Exit.";
        exit(0);
    }

    void run(Configuration const& config)
    {
        if (config.get("read-slave", "") == "1") {
            LOG(INFO) << "Readonly proxy, use slaves for reading if possible";
            cerb::Server::send_readonly_for_each_conn();
            cerb::stats_set_read_slave();
            cerb::SlotMap::select_slave_if_possible();
        } else {
            LOG(INFO) << "Writable proxy";
            cerb::Command::allow_write_commands();
        }

        int bind_port = util::atoi(config.get("bind"));
        int thread_count = util::atoi(config.get("thread", "1"));
        if (thread_count <= 0) {
            LOG(ERROR) << "Invalid thread count";
            exit(1);
        }

        if (config.contains("node")) {
            cerb_global::set_remotes({util::Address::from_host_port(config.get("node"))});
        } else {
            LOG(WARNING) << "Remote is not set in config file; to set it by command,"
                            " use `SETREMOTES <host> <port>' in a redis-cli prompt";
        }

        for (int i = 0; i < thread_count; ++i) {
            cerb_global::all_threads.push_back(cerb::ListenThread(bind_port));
        }
        for (auto& t: cerb_global::all_threads) {
            t.run();
        }
        LOG(INFO) << "Started; listen to port " << bind_port
                  << " thread=" << thread_count;
        for (auto& t: cerb_global::all_threads) {
            t.join();
        }
    }

}

int main(int argc, char* argv[])
{
    std::cerr << "Cerberus version " VERSION
                 " Copyright (c) HunanTV Platform developers" << std::endl;
    if (argc == 1) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "    cerberus CONFIG_FILE" << std::endl;
        return 1;
    }
    Configuration config(argv[1]);

    signal(SIGINT, exit_on_int);
    logging::init();
    trac::trace_on_seg_fault();
    trac::trace_on_fpe();

    try {
        run(config);
        return 0;
    } catch (std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
